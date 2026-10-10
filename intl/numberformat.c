/*
 * QuickJS Intl.NumberFormat implementation
 */

#include <string.h>
#include <ctype.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "intl_private.h"

JSClassID js_numberformat_class_id;
/* ---- Forward declarations ---- */
typedef struct JSNumberFormatData JSNumberFormatData;

/* ---- Normalized Intl Mathematical Value ---- */

typedef enum {
    INTL_MV_NORMAL = 0,
    INTL_MV_INFINITY,
    INTL_MV_NAN,
} IntlMVKind;

typedef struct {
    IntlMVKind kind;
    int sign;        /* 1 = negative, 0 = positive (applies to normal and infinity) */
    char *digits;    /* normalized digits: no leading zeros, no trailing zeros,
                        "0" for zero values */
    int num_digits;
    int exponent;    /* decimal exponent: value = digits_as_int * 10^exponent */
} IntlMV;

/* Sanctioned simple unit identifiers per ECMA-402 Table 2 */
const char *sanctioned_simple_units[] = {
    "acre", "bit", "byte", "celsius", "centimeter", "day", "degree",
    "fahrenheit", "fluid-ounce", "foot", "gallon", "gigabit", "gigabyte",
    "gram", "hectare", "hour", "inch", "kilobit", "kilobyte", "kilogram",
    "kilometer", "liter", "megabit", "megabyte", "meter", "microsecond",
    "mile", "mile-scandinavian", "milliliter", "millimeter", "millisecond",
    "minute", "month", "nanosecond", "ounce", "percent", "petabyte",
    "pound", "second", "stone", "terabit", "terabyte", "week", "yard", "year",
};
const int sanctioned_simple_units_count = countof(sanctioned_simple_units);

static int is_sanctioned_simple_unit(const char *str)
{
    int i, n = countof(sanctioned_simple_units);
    for (i = 0; i < n; i++) {
        if (!strcmp(str, sanctioned_simple_units[i]))
            return 1;
    }
    return 0;
}

/* Validate unit identifier per ECMA-402 IsWellFormedUnitIdentifier */
static int is_well_formed_unit_identifier(const char *str)
{
    const char *p = str;
    char buf[64];
    int len;
    
    if (!str || !str[0])
        return 0;
    
    /* Check for compound unit: split on -per- */
    p = strstr(str, "-per-");
    if (p) {
        len = (int)(p - str);
        if ((size_t)len >= sizeof(buf)) return 0;
        memcpy(buf, str, (size_t)len);
        buf[len] = '\0';
        if (!is_sanctioned_simple_unit(buf))
            return 0;
        if (!is_sanctioned_simple_unit(p + 5))
            return 0;
        return 1;
    }
    
    return is_sanctioned_simple_unit(str);
}

static void intl_mv_free(JSContext *ctx, IntlMV *mv)
{
    js_free(ctx, mv->digits);
    mv->digits = NULL;
}

/* Parse a decimal string (as returned by Number::toString or BigInt::toString)
   into normalized IntlMV form.  Handles forms like:
     "0", "123", "1.23", "1.23e+10", "1.23e-10", "-1.5e+3"
   Returns 0 on success, -1 on parse error. */
static int intl_mv_parse(JSContext *ctx, IntlMV *mv, const char *str)
{
    const char *p = str;
    int neg = 0;
    char *buf;
    int buf_len;
    int exp = 0;

    /* Skip leading whitespace */
    while (isspace((unsigned char)*p)) p++;
    /* Skip trailing whitespace will be handled by the final *p != '\0' check */

    mv->kind = INTL_MV_NORMAL;
    mv->sign = 0;
    mv->digits = NULL;
    mv->num_digits = 0;
    mv->exponent = 0;

    if (*p == '-') {
        neg = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    /* Read integer part (including leading zeros) */
    const char *int_start = p;
    while (isdigit((unsigned char)*p)) p++;
    int int_len = (int)(p - int_start);

    /* Read fraction part */
    int frac_count = 0;
    const char *frac_start = NULL;
    if (*p == '.') {
        p++;
        frac_start = p;
        while (isdigit((unsigned char)*p)) p++;
        frac_count = (int)(p - frac_start);
    }

    /* Read optional exponent */
    if (*p == 'e' || *p == 'E') {
        p++;
        int exp_neg = 0;
        if (*p == '+') p++;
        else if (*p == '-') { exp_neg = 1; p++; }
        int exp_val = 0;
        while (isdigit((unsigned char)*p)) {
            exp_val = exp_val * 10 + (*p - '0');
            p++;
        }
        if (exp_neg) exp_val = -exp_val;
        exp += exp_val;
    }

    /* Skip trailing whitespace */
    while (isspace((unsigned char)*p)) p++;

    if (*p != '\0') {
        mv->kind = INTL_MV_NAN;
        return 0;
    }

    /* Build full digit buffer (integer + fraction, no stripping yet) */
    buf_len = int_len + frac_count;
    if (buf_len == 0) {
        mv->sign = neg;
        mv->digits = js_strdup(ctx, "0");
        mv->num_digits = 1;
        mv->exponent = 0;
        return 0;
    }

    buf = js_malloc(ctx, (size_t)buf_len + 1);
    if (!buf) return -1;
    memcpy(buf, int_start, (size_t)int_len);
    if (frac_count > 0)
        memcpy(buf + int_len, frac_start, (size_t)frac_count);
    buf[buf_len] = '\0';

    /* Base exponent: decimal point is after int_len digits */
    exp -= frac_count;

    /* Strip leading zeros (no exponent adjustment needed) */
    {
        int i = 0;
        while (i < buf_len && buf[i] == '0') i++;
        if (i == buf_len) {
            /* All zeros */
            js_free(ctx, buf);
            mv->sign = neg;
            mv->digits = js_strdup(ctx, "0");
            mv->num_digits = 1;
            mv->exponent = 0;
            return 0;
        }
        if (i > 0) {
            memmove(buf, buf + i, (size_t)(buf_len - i + 1));
            buf_len -= i;
        }
    }

    /* Strip trailing zeros (adjust exponent) */
    {
        int k = (int)(buf_len - 1);
        while (k > 0 && buf[k] == '0') k--;
        if (k < buf_len - 1) {
            int dropped = (int)(buf_len - 1 - k);
            buf[k + 1] = '\0';
            buf_len = k + 1;
            exp += dropped;
        }
    }

    mv->sign = neg;
    mv->digits = buf;
    mv->num_digits = buf_len;
    mv->exponent = exp;
    return 0;
}

/* Initialize IntlMV from a JSValue following ToIntlMathematicalValue.
   Returns 0 on success, -1 on error (exception set in ctx). */
static int intl_mv_init_from_value(JSContext *ctx, JSValueConst val, IntlMV *mv)
{
    JSValue prim;
    const char *str;
    double d;
    int ret;

    mv->kind = INTL_MV_NORMAL;
    mv->sign = 0;
    mv->digits = NULL;
    mv->num_digits = 0;
    mv->exponent = 0;

    /* Step 1: ToPrimitive(value, number) */
    prim = JS_ToPrimitive(ctx, val, JS_HINT_NUMBER);
    if (JS_IsException(prim))
        return -1;

    if (JS_IsBigInt(ctx, prim)) {
        /* Step 2: BigInt path */
        str = JS_ToCString(ctx, prim);
        JS_FreeValue(ctx, prim);
        if (!str) return -1;
        ret = intl_mv_parse(ctx, mv, str);
        JS_FreeCString(ctx, str);
        return ret;
    }

    if (JS_IsString(prim)) {
        /* Step 3: String path */
        str = JS_ToCString(ctx, prim);
        JS_FreeValue(ctx, prim);
        if (!str) return -1;
        if (strcmp(str, "NaN") == 0) {
            mv->kind = INTL_MV_NAN;
            JS_FreeCString(ctx, str);
            return 0;
        }
        if (strcmp(str, "Infinity") == 0) {
            mv->kind = INTL_MV_INFINITY;
            JS_FreeCString(ctx, str);
            return 0;
        }
        if (strcmp(str, "-Infinity") == 0) {
            mv->kind = INTL_MV_INFINITY;
            mv->sign = 1;
            JS_FreeCString(ctx, str);
            return 0;
        }
        ret = intl_mv_parse(ctx, mv, str);
        JS_FreeCString(ctx, str);
        return ret;
    }

    /* Step 4: ToNumber(primValue) then Number::toString(x, 10) */
    if (JS_ToFloat64(ctx, &d, prim)) {
        JS_FreeValue(ctx, prim);
        return -1;
    }
    JS_FreeValue(ctx, prim);

    if (isnan(d)) {
        mv->kind = INTL_MV_NAN;
        return 0;
    }
    if (isinf(d)) {
        mv->kind = INTL_MV_INFINITY;
        mv->sign = (d < 0) ? 1 : 0;
        return 0;
    }

    /* Detect negative zero */
    if (signbit(d) && d == 0.0) {
        mv->sign = 1;
        mv->digits = js_strdup(ctx, "0");
        if (!mv->digits)
            return -1;
        mv->num_digits = 1;
        mv->exponent = 0;
        return 0;
    }

    {
        JSValue tmp = JS_NewFloat64(ctx, d);
        str = JS_ToCString(ctx, tmp);
        JS_FreeValue(ctx, tmp);
        if (!str) return -1;
        ret = intl_mv_parse(ctx, mv, str);
        JS_FreeCString(ctx, str);
        return ret;
    }
}

/* Format a normal IntlMV in standard notation.
   Returns a JSValue string on success, JS_EXCEPTION on error. */

/* Compute N mod M where N is a decimal digit string.  M must be positive. */
static int64_t intl_mv_mod_string(const char *digits, int num_digits, int64_t M)
{
    int64_t rem = 0;
    int i;
    for (i = 0; i < num_digits; i++) {
        rem = (rem * 10 + (digits[i] - '0')) % M;
    }
    return rem;
}

/* Add small positive integer v to a digit string (in-place).
   Returns 0 on success, -1 if overflow (all 9s → would need an extra digit). */
static int intl_mv_add_small_inplace(char *d, int len, int64_t v)
{
    int i;
    for (i = len - 1; i >= 0 && v > 0; i--) {
        int digit = (int)(d[i] - '0');
        int add = (int)(v % 10);
        v /= 10;
        digit += add;
        if (digit >= 10) {
            digit -= 10;
            if (i == 0)
                return -1;
            d[i - 1]++;
        }
        d[i] = (char)(digit + '0');
    }
    /* Propagate carries leftwards */
    for (i = len - 1; i >= 0; i--) {
        if (d[i] > '9') {
            if (i == 0)
                return -1;
            d[i] -= 10;
            d[i - 1]++;
        }
    }
    return 0;
}

/* Subtract small positive integer v from a digit string (in-place).
   Returns 0 on success, -1 if underflow (would go negative). */
static int intl_mv_sub_small_inplace(char *d, int len, int64_t v)
{
    int i;
    for (i = len - 1; i >= 0 && v > 0; i--) {
        int digit = (int)(d[i] - '0');
        int sub = (int)(v % 10);
        v /= 10;
        digit -= sub;
        if (digit < 0) {
            digit += 10;
            if (i == 0)
                return -1;
            d[i - 1]--;
        }
        d[i] = (char)(digit + '0');
    }
    /* Propagate borrows leftwards */
    for (i = len - 1; i >= 0; i--) {
        if (d[i] < '0') {
            if (i == 0)
                return -1;
            d[i] += 10;
            d[i - 1]--;
        }
    }
    return 0;
}

enum {
    NF_STYLE_DECIMAL = 0,
    NF_STYLE_PERCENT,
    NF_STYLE_CURRENCY,
    NF_STYLE_UNIT,
};
static const char *nf_style_names[] = {"decimal", "percent", "currency", "unit"};

enum {
    NF_GROUPING_AUTO = 0,
    NF_GROUPING_MIN2,
    NF_GROUPING_ALWAYS,
    NF_GROUPING_FALSE_BOOL,
};
static const char *nf_grouping_names[] = {"auto", "min2", "always"};

enum {
    NF_NOTATION_STANDARD = 0,
    NF_NOTATION_SCIENTIFIC,
    NF_NOTATION_ENGINEERING,
    NF_NOTATION_COMPACT,
};
static const char *nf_notation_names[] = {"standard", "scientific", "engineering", "compact"};

enum {
    NF_CURRENCYDISPLAY_CODE = 0,
    NF_CURRENCYDISPLAY_SYMBOL,
    NF_CURRENCYDISPLAY_NARROW,
    NF_CURRENCYDISPLAY_NAME,
};
static const char *nf_currencydisplay_names[] = {"code", "symbol", "narrowSymbol", "name"};

enum {
    NF_CURRENCYSIGN_STANDARD = 0,
    NF_CURRENCYSIGN_ACCOUNTING,
};
static const char *nf_currencysign_names[] = {"standard", "accounting"};

enum {
    NF_UNITDISPLAY_SHORT = 0,
    NF_UNITDISPLAY_NARROW,
    NF_UNITDISPLAY_LONG,
};
static const char *nf_unitdisplay_names[] = {"short", "narrow", "long"};

enum {
    NF_SIGNDISPLAY_AUTO = 0,
    NF_SIGNDISPLAY_NEVER,
    NF_SIGNDISPLAY_ALWAYS,
    NF_SIGNDISPLAY_EXCEPTZERO,
    NF_SIGNDISPLAY_NEGATIVE,
};
static const char *nf_signdisplay_names[] = {"auto", "never", "always", "exceptZero", "negative"};

enum {
    NF_ROUNDINGMODE_CEIL = 0,
    NF_ROUNDINGMODE_FLOOR,
    NF_ROUNDINGMODE_EXPAND,
    NF_ROUNDINGMODE_TRUNC,
    NF_ROUNDINGMODE_HALFCEIL,
    NF_ROUNDINGMODE_HALFFLOOR,
    NF_ROUNDINGMODE_HALFEXPAND,
    NF_ROUNDINGMODE_HALFTRUNC,
    NF_ROUNDINGMODE_HALFEVEN,
};
static const char *nf_roundingmode_names[] = {"ceil", "floor", "expand", "trunc", "halfCeil", "halfFloor", "halfExpand", "halfTrunc", "halfEven"};

enum {
    NF_ROUNDINGPRIORITY_AUTO = 0,
    NF_ROUNDINGPRIORITY_MOREPRECISION,
    NF_ROUNDINGPRIORITY_LESSPRECISION,
};
static const char *nf_roundingpriority_names[] = {"auto", "morePrecision", "lessPrecision"};

enum {
    NF_TRAILINGZERO_AUTO = 0,
    NF_TRAILINGZERO_STRIPIFINTEGER,
};
static const char *nf_trailingzero_names[] = {"auto", "stripIfInteger"};

enum {
    NF_COMPACT_SHORT = 0,
    NF_COMPACT_LONG,
    NF_COMPACT_UNSET,
};
static const char *nf_compact_names[] = {"short", "long"};

/* Compute parity of last quotient digit when dividing decimal string by divisor.
   Used for half-even rounding tie-breaking. */
static int string_quotient_parity(const char *str, int len, int64_t divisor)
{
    int64_t rem = 0;
    int i;
    int q_parity = 0;
    for (i = 0; i < len; i++) {
        rem = rem * 10 + (str[i] - '0');
        int64_t q_digit = rem / divisor;
        rem = rem % divisor;
        q_parity = q_digit & 1;
    }
    return q_parity;
}

/* Round IntlMV to fraction-digit constraints.
   Handles maxFD/minFD, roundingIncrement, and all rounding modes.
   Returns 0 on success, -1 on error. */
static int intl_mv_round_fraction(JSContext *ctx, IntlMV *mv, int maxFD, int minFD,
                                   int roundingIncrement, int roundingMode)
{
    int64_t M, rem, add_val;
    int scale, k, i, round_up;
    char *scaled_digits;
    int scaled_len;
    int is_positive;

    if (mv->kind != INTL_MV_NORMAL)
        return 0;
    if (mv->num_digits == 1 && mv->digits[0] == '0') {
        mv->exponent = -maxFD;
        return 0;
    }

    is_positive = !mv->sign;
    scale = mv->exponent + maxFD;

    if (scale >= 0) {
        k = 0;
        scaled_len = mv->num_digits + scale;
        scaled_digits = js_malloc(ctx, (size_t)scaled_len + 1);
        if (!scaled_digits) return -1;
        memcpy(scaled_digits, mv->digits, (size_t)mv->num_digits);
        for (i = 0; i < scale; i++)
            scaled_digits[mv->num_digits + i] = '0';
        scaled_digits[scaled_len] = '\0';
    } else {
        k = -scale;
        scaled_len = mv->num_digits;
        scaled_digits = js_strdup(ctx, mv->digits);
        if (!scaled_digits) return -1;
    }

    /* Compute M = roundingIncrement * 10^k */
    M = roundingIncrement;
    for (i = 0; i < k; i++) {
        if (M > INT64_MAX / 10) {
            /* M overflows int64_t: handle with string logic */
            if (roundingIncrement > 1 || scaled_len < k) {
                /* Number is tiny compared to rounding unit: round to zero */
                js_free(ctx, scaled_digits);
                js_free(ctx, mv->digits);
                mv->digits = js_strdup(ctx, "0");
                mv->num_digits = 1;
                mv->exponent = -maxFD;
                return 0;
            }
            /* roundingIncrement == 1 and scaled_len >= k */
            {
                int prefix_len = (int)(scaled_len - k);
                const char *rem_start = scaled_digits + prefix_len;
                int rem_is_zero = 1;
                int rem_is_greater = 0;
                int rem_is_tie = 0;
                int j;
                for (j = 0; j < k; j++) {
                    if (rem_start[j] != '0') {
                        rem_is_zero = 0;
                        break;
                    }
                }
                if (!rem_is_zero) {
                    if (rem_start[0] > '5') {
                        rem_is_greater = 1;
                    } else if (rem_start[0] < '5') {
                        rem_is_greater = 0;
                    } else {
                        /* first digit is '5', check remaining */
                        int later_nonzero = 0;
                        for (j = 1; j < k; j++) {
                            if (rem_start[j] != '0') {
                                later_nonzero = 1;
                                break;
                            }
                        }
                        if (later_nonzero)
                            rem_is_greater = 1;
                        else
                            rem_is_tie = 1;
                    }
                }
                /* Determine round_up based on roundingMode */
                if (rem_is_zero) {
                    round_up = 0;
                } else {
                    switch (roundingMode) {
                    case NF_ROUNDINGMODE_CEIL:
                        round_up = is_positive;
                        break;
                    case NF_ROUNDINGMODE_FLOOR:
                        round_up = !is_positive;
                        break;
                    case NF_ROUNDINGMODE_EXPAND:
                        round_up = 1;
                        break;
                    case NF_ROUNDINGMODE_TRUNC:
                        round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFCEIL:
                        if (rem_is_greater) round_up = 1;
                        else if (rem_is_tie) round_up = is_positive;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFFLOOR:
                        if (rem_is_greater) round_up = 1;
                        else if (rem_is_tie) round_up = !is_positive;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFEXPAND:
                        if (rem_is_greater || rem_is_tie) round_up = 1;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFTRUNC:
                        if (rem_is_greater) round_up = 1;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFEVEN:
                        if (rem_is_greater) round_up = 1;
                        else if (rem_is_tie) {
                            /* q = floor(scaled_value / M). M = 10^k, q = prefix as integer. */
                            /* Check if prefix is odd */
                            int prefix_odd = 0;
                            if (prefix_len > 0) {
                                /* check last digit of prefix */
                                prefix_odd = (scaled_digits[prefix_len - 1] - '0') & 1;
                            }
                            round_up = prefix_odd;
                        } else {
                            round_up = 0;
                        }
                        break;
                    default:
                        round_up = 0;
                    }
                }
                if (round_up) {
                    /* Add 10^k - rem: zero out suffix, increment prefix */
                    char *new_digits;
                    int j2;
                    if (prefix_len == 0) {
                        new_digits = js_malloc(ctx, 2);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        new_digits[0] = '1';
                        new_digits[1] = '\0';
                    } else {
                        new_digits = js_malloc(ctx, (size_t)prefix_len + 1);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        memcpy(new_digits, scaled_digits, (size_t)prefix_len);
                        new_digits[prefix_len] = '\0';
                        for (j2 = prefix_len - 1; j2 >= 0; j2--) {
                            if (new_digits[j2] == '9') {
                                new_digits[j2] = '0';
                            } else {
                                new_digits[j2]++;
                                break;
                            }
                        }
                        if (j2 < 0) {
                            js_free(ctx, new_digits);
                            new_digits = js_malloc(ctx, (size_t)prefix_len + 2);
                            if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                            new_digits[0] = '1';
                            for (j2 = 0; j2 < prefix_len; j2++)
                                new_digits[j2 + 1] = '0';
                            new_digits[prefix_len + 1] = '\0';
                        }
                    }
                    js_free(ctx, scaled_digits);
                    js_free(ctx, mv->digits);
                    mv->digits = new_digits;
                    mv->num_digits = (int)strlen(new_digits);
                    mv->exponent = -maxFD;
                } else {
                    char *new_digits;
                    if (prefix_len == 0) {
                        new_digits = js_strdup(ctx, "0");
                    } else {
                        new_digits = js_malloc(ctx, (size_t)prefix_len + 1);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        memcpy(new_digits, scaled_digits, (size_t)prefix_len);
                        new_digits[prefix_len] = '\0';
                    }
                    js_free(ctx, scaled_digits);
                    js_free(ctx, mv->digits);
                    mv->digits = new_digits;
                    mv->num_digits = (int)strlen(new_digits);
                    if (mv->num_digits == 1 && mv->digits[0] == '0')
                        mv->exponent = 0;
                    else
                        mv->exponent = -maxFD;
                }
            }
            return 0;
        }
        M *= 10;
    }

    rem = intl_mv_mod_string(scaled_digits, scaled_len, M);

    if (rem == 0) {
        round_up = 0;
    } else {
        int is_greater, is_tie;
        /* Safe comparison without overflow */
        if (M % 2 == 1) {
            /* M is odd: tie is impossible */
            is_tie = 0;
            is_greater = (rem > M / 2);
        } else {
            is_tie = (rem == M / 2);
            is_greater = (rem > M / 2);
        }
        switch (roundingMode) {
        case NF_ROUNDINGMODE_CEIL:
            round_up = is_positive;
            break;
        case NF_ROUNDINGMODE_FLOOR:
            round_up = !is_positive;
            break;
        case NF_ROUNDINGMODE_EXPAND:
            round_up = 1;
            break;
        case NF_ROUNDINGMODE_TRUNC:
            round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFCEIL:
            if (is_greater) round_up = 1;
            else if (is_tie) round_up = is_positive;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFFLOOR:
            if (is_greater) round_up = 1;
            else if (is_tie) round_up = !is_positive;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFEXPAND:
            if (is_greater || is_tie) round_up = 1;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFTRUNC:
            if (is_greater) round_up = 1;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFEVEN:
            if (is_greater) {
                round_up = 1;
            } else if (is_tie) {
                int q_parity = string_quotient_parity(scaled_digits, scaled_len, M);
                round_up = q_parity;
            } else {
                round_up = 0;
            }
            break;
        default:
            round_up = 0;
        }
    }

    if (round_up) {
        add_val = M - rem;
        if (intl_mv_add_small_inplace(scaled_digits, scaled_len, add_val) < 0) {
            js_free(ctx, scaled_digits);
            scaled_len++;
            scaled_digits = js_malloc(ctx, (size_t)scaled_len + 1);
            if (!scaled_digits) return -1;
            scaled_digits[0] = '1';
            for (i = 1; i < scaled_len; i++)
                scaled_digits[i] = '0';
            scaled_digits[scaled_len] = '\0';
        }
    } else {
        if (rem > 0) {
            if (intl_mv_sub_small_inplace(scaled_digits, scaled_len, rem) < 0) {
                js_free(ctx, scaled_digits);
                return -1;
            }
        }
    }

    /* Strip leading zeros */
    i = 0;
    while (i < scaled_len && scaled_digits[i] == '0') i++;
    if (i == scaled_len) {
        js_free(ctx, scaled_digits);
        js_free(ctx, mv->digits);
        mv->digits = js_strdup(ctx, "0");
        mv->num_digits = 1;
        mv->exponent = -maxFD;
        return 0;
    }
    if (i > 0) {
        memmove(scaled_digits, scaled_digits + i, (size_t)(scaled_len - i + 1));
        scaled_len -= i;
    }

    /* For scale < 0, drop last k zeros (result is multiple of 10^k) */
    if (k > 0) {
        scaled_len -= k;
        scaled_digits[scaled_len] = '\0';
    }

    js_free(ctx, mv->digits);
    mv->digits = scaled_digits;
    mv->num_digits = scaled_len;
    mv->exponent = -maxFD;

    /* Pad to minimumFractionDigits */
    {
        int current_frac = (mv->exponent < 0) ? -mv->exponent : 0;
        if (current_frac < minFD) {
            int pad = (int)(minFD - current_frac);
            int new_len = mv->num_digits + pad;
            char *new_d = js_malloc(ctx, (size_t)new_len + 1);
            if (!new_d) return -1;
            memcpy(new_d, mv->digits, (size_t)mv->num_digits);
            for (i = 0; i < pad; i++)
                new_d[mv->num_digits + i] = '0';
            new_d[new_len] = '\0';
            js_free(ctx, mv->digits);
            mv->digits = new_d;
            mv->num_digits = new_len;
            mv->exponent = -minFD;
        }
    }

    return 0;
}

/* Round IntlMV to significant-digit constraints.
   Handles maxSD/minSD and all rounding modes.
   Returns 0 on success, -1 on error. */
static int intl_mv_round_significant(JSContext *ctx, IntlMV *mv, int maxSD, int minSD,
                                      int roundingMode)
{
    int64_t M, rem, add_val;
    int drop, i, round_up;
    char *scaled_digits;
    int scaled_len;
    int is_positive;

    if (mv->kind != INTL_MV_NORMAL)
        return 0;
    if (mv->num_digits == 1 && mv->digits[0] == '0') {
        /* Zero: pad to minSD if needed */
        if (minSD > 1) {
            int pad = (int)(minSD - 1);
            int new_len = 1 + pad;
            char *new_d = js_malloc(ctx, (size_t)new_len + 1);
            if (!new_d) return -1;
            new_d[0] = '0';
            for (i = 0; i < pad; i++)
                new_d[1 + i] = '0';
            new_d[new_len] = '\0';
            js_free(ctx, mv->digits);
            mv->digits = new_d;
            mv->num_digits = new_len;
            mv->exponent = -(minSD - 1);
        }
        return 0;
    }

    is_positive = !mv->sign;
    drop = mv->num_digits - maxSD;

    if (drop <= 0) {
        /* No rounding needed, but may need to pad for minSD */
        if (mv->num_digits < minSD) {
            int pad = (int)(minSD - mv->num_digits);
            int new_len = mv->num_digits + pad;
            char *new_d = js_malloc(ctx, (size_t)new_len + 1);
            if (!new_d) return -1;
            memcpy(new_d, mv->digits, (size_t)mv->num_digits);
            for (i = 0; i < pad; i++)
                new_d[mv->num_digits + i] = '0';
            new_d[new_len] = '\0';
            js_free(ctx, mv->digits);
            mv->digits = new_d;
            mv->num_digits = new_len;
            mv->exponent = mv->exponent - pad;
        }
        return 0;
    }

    /* Round the coefficient to nearest multiple of 10^drop */
    scaled_len = mv->num_digits;
    scaled_digits = js_strdup(ctx, mv->digits);
    if (!scaled_digits) return -1;

    M = 1;
    for (i = 0; i < drop; i++) {
        if (M > INT64_MAX / 10) {
            /* M overflows int64_t */
            int prefix_len = (int)(scaled_len - drop);
            if (prefix_len <= 0) {
                js_free(ctx, scaled_digits);
                js_free(ctx, mv->digits);
                mv->digits = js_strdup(ctx, "0");
                mv->num_digits = 1;
                mv->exponent = 0;
                return 0;
            }
            {
                const char *rem_start = scaled_digits + prefix_len;
                int rem_is_zero = 1;
                int rem_is_greater = 0;
                int rem_is_tie = 0;
                int j;
                for (j = 0; j < drop; j++) {
                    if (rem_start[j] != '0') {
                        rem_is_zero = 0;
                        break;
                    }
                }
                if (!rem_is_zero) {
                    if (rem_start[0] > '5') {
                        rem_is_greater = 1;
                    } else if (rem_start[0] < '5') {
                        rem_is_greater = 0;
                    } else {
                        int later_nonzero = 0;
                        for (j = 1; j < drop; j++) {
                            if (rem_start[j] != '0') {
                                later_nonzero = 1;
                                break;
                            }
                        }
                        if (later_nonzero)
                            rem_is_greater = 1;
                        else
                            rem_is_tie = 1;
                    }
                }
                if (rem_is_zero) {
                    round_up = 0;
                } else {
                    switch (roundingMode) {
                    case NF_ROUNDINGMODE_CEIL:
                        round_up = is_positive;
                        break;
                    case NF_ROUNDINGMODE_FLOOR:
                        round_up = !is_positive;
                        break;
                    case NF_ROUNDINGMODE_EXPAND:
                        round_up = 1;
                        break;
                    case NF_ROUNDINGMODE_TRUNC:
                        round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFCEIL:
                        if (rem_is_greater) round_up = 1;
                        else if (rem_is_tie) round_up = is_positive;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFFLOOR:
                        if (rem_is_greater) round_up = 1;
                        else if (rem_is_tie) round_up = !is_positive;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFEXPAND:
                        if (rem_is_greater || rem_is_tie) round_up = 1;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFTRUNC:
                        if (rem_is_greater) round_up = 1;
                        else round_up = 0;
                        break;
                    case NF_ROUNDINGMODE_HALFEVEN:
                        if (rem_is_greater) {
                            round_up = 1;
                        } else if (rem_is_tie) {
                            int prefix_odd = 0;
                            if (prefix_len > 0)
                                prefix_odd = (scaled_digits[prefix_len - 1] - '0') & 1;
                            round_up = prefix_odd;
                        } else {
                            round_up = 0;
                        }
                        break;
                    default:
                        round_up = 0;
                    }
                }
                if (round_up) {
                    char *new_digits;
                    int j2;
                    if (prefix_len == 0) {
                        new_digits = js_malloc(ctx, 2);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        new_digits[0] = '1';
                        new_digits[1] = '\0';
                    } else {
                        new_digits = js_malloc(ctx, (size_t)prefix_len + 1);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        memcpy(new_digits, scaled_digits, (size_t)prefix_len);
                        new_digits[prefix_len] = '\0';
                        for (j2 = prefix_len - 1; j2 >= 0; j2--) {
                            if (new_digits[j2] == '9') {
                                new_digits[j2] = '0';
                            } else {
                                new_digits[j2]++;
                                break;
                            }
                        }
                        if (j2 < 0) {
                            js_free(ctx, new_digits);
                            new_digits = js_malloc(ctx, (size_t)prefix_len + 2);
                            if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                            new_digits[0] = '1';
                            for (j2 = 0; j2 < prefix_len; j2++)
                                new_digits[j2 + 1] = '0';
                            new_digits[prefix_len + 1] = '\0';
                        }
                    }
                    js_free(ctx, scaled_digits);
                    js_free(ctx, mv->digits);
                    mv->digits = new_digits;
                    mv->num_digits = (int)strlen(new_digits);
                    mv->exponent = mv->exponent + drop;
                } else {
                    char *new_digits;
                    if (prefix_len == 0) {
                        new_digits = js_strdup(ctx, "0");
                    } else {
                        new_digits = js_malloc(ctx, (size_t)prefix_len + 1);
                        if (!new_digits) { js_free(ctx, scaled_digits); return -1; }
                        memcpy(new_digits, scaled_digits, (size_t)prefix_len);
                        new_digits[prefix_len] = '\0';
                    }
                    js_free(ctx, scaled_digits);
                    js_free(ctx, mv->digits);
                    mv->digits = new_digits;
                    mv->num_digits = (int)strlen(new_digits);
                    if (mv->num_digits == 1 && mv->digits[0] == '0')
                        mv->exponent = 0;
                    else
                        mv->exponent = mv->exponent + drop;
                }
            }
            return 0;
        }
        M *= 10;
    }

    rem = intl_mv_mod_string(scaled_digits, scaled_len, M);

    if (rem == 0) {
        round_up = 0;
    } else {
        int is_greater, is_tie;
        if (M % 2 == 1) {
            is_tie = 0;
            is_greater = (rem > M / 2);
        } else {
            is_tie = (rem == M / 2);
            is_greater = (rem > M / 2);
        }
        switch (roundingMode) {
        case NF_ROUNDINGMODE_CEIL:
            round_up = is_positive;
            break;
        case NF_ROUNDINGMODE_FLOOR:
            round_up = !is_positive;
            break;
        case NF_ROUNDINGMODE_EXPAND:
            round_up = 1;
            break;
        case NF_ROUNDINGMODE_TRUNC:
            round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFCEIL:
            if (is_greater) round_up = 1;
            else if (is_tie) round_up = is_positive;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFFLOOR:
            if (is_greater) round_up = 1;
            else if (is_tie) round_up = !is_positive;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFEXPAND:
            if (is_greater || is_tie) round_up = 1;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFTRUNC:
            if (is_greater) round_up = 1;
            else round_up = 0;
            break;
        case NF_ROUNDINGMODE_HALFEVEN:
            if (is_greater) {
                round_up = 1;
            } else if (is_tie) {
                int q_parity = string_quotient_parity(scaled_digits, scaled_len, M);
                round_up = q_parity;
            } else {
                round_up = 0;
            }
            break;
        default:
            round_up = 0;
        }
    }

    if (round_up) {
        add_val = M - rem;
        if (intl_mv_add_small_inplace(scaled_digits, scaled_len, add_val) < 0) {
            js_free(ctx, scaled_digits);
            scaled_len++;
            scaled_digits = js_malloc(ctx, (size_t)scaled_len + 1);
            if (!scaled_digits) return -1;
            scaled_digits[0] = '1';
            for (i = 1; i < scaled_len; i++)
                scaled_digits[i] = '0';
            scaled_digits[scaled_len] = '\0';
        }
    } else {
        if (rem > 0) {
            if (intl_mv_sub_small_inplace(scaled_digits, scaled_len, rem) < 0) {
                js_free(ctx, scaled_digits);
                return -1;
            }
        }
    }

    /* Strip leading zeros */
    i = 0;
    while (i < scaled_len && scaled_digits[i] == '0') i++;
    if (i == scaled_len) {
        js_free(ctx, scaled_digits);
        js_free(ctx, mv->digits);
        mv->digits = js_strdup(ctx, "0");
        mv->num_digits = 1;
        mv->exponent = 0;
        return 0;
    }
    if (i > 0) {
        memmove(scaled_digits, scaled_digits + i, (size_t)(scaled_len - i + 1));
        scaled_len -= i;
    }

    /* Drop the last 'drop' zeros */
    scaled_len -= drop;
    scaled_digits[scaled_len] = '\0';

    js_free(ctx, mv->digits);
    mv->digits = scaled_digits;
    mv->num_digits = scaled_len;
    mv->exponent = mv->exponent + drop;

    /* Pad to minimumSignificantDigits */
    if (mv->num_digits < minSD) {
        int pad = (int)(minSD - mv->num_digits);
        int new_len = mv->num_digits + pad;
        char *new_d = js_malloc(ctx, (size_t)new_len + 1);
        if (!new_d) return -1;
        memcpy(new_d, mv->digits, (size_t)mv->num_digits);
        for (i = 0; i < pad; i++)
            new_d[mv->num_digits + i] = '0';
        new_d[new_len] = '\0';
        js_free(ctx, mv->digits);
        mv->digits = new_d;
        mv->num_digits = new_len;
    }

    return 0;
}

/* ---- data struct ---- */

typedef struct JSNumberFormatData {
    char *locale;
    char *locale_canonical;
    int numberingSystem;
    int has_numberingSystem_option;
    int style;
    char *currency;
    int currencyDisplay;
    int currencySign;
    char *unit;
    int unitDisplay;
    int minimumIntegerDigits;
    int minimumFractionDigits;
    int maximumFractionDigits;
    int minimumSignificantDigits;
    int maximumSignificantDigits;
    int hasMinimumSignificantDigits;
    int hasMaximumSignificantDigits;
    int useGrouping;
    int notation;
    int compactDisplay;      /* -1 = unset */
    int signDisplay;
    int roundingMode;
    int roundingPriority;
    int roundingIncrement;
    int trailingZeroDisplay;
} JSNumberFormatData;

static void js_numberformat_finalizer(JSRuntime *rt, JSValue val)
{
    JSNumberFormatData *s = JS_GetOpaque(val, js_numberformat_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->locale_canonical);
        js_free_rt(rt, s->currency);
        js_free_rt(rt, s->unit);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_numberformat_class = {
    "NumberFormat",
    .finalizer = js_numberformat_finalizer,
};

/* UnwrapNumberFormat: handle fallback symbol for cross-realm objects */
static JSValue js_numberformat_unwrap(JSContext *ctx, JSValueConst this_val)
{
    JSNumberFormatData *s;
    JSValue ctor, sym, unwrapped;
    int is_instance;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeError(ctx, "NumberFormat object expected");

    s = JS_GetOpaque(this_val, js_numberformat_class_id);
    if (s)
        return JS_DupValue(ctx, this_val);

    /* No internal slot; check if it's an instance of NumberFormat */
    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
    is_instance = JS_OrdinaryIsInstanceOf(ctx, this_val, ctor);
    JS_FreeValue(ctx, ctor);
    if (is_instance < 0)
        return JS_EXCEPTION;
    if (!is_instance)
        return JS_ThrowTypeError(ctx, "NumberFormat object expected");

    /* Return the fallback symbol value */
    sym = JS_GetContextValue(ctx, JS_INTL_FALLBACK_SYMBOL);
    if (JS_IsException(sym))
        return sym;
    {
        JSAtom sym_atom = JS_ValueToAtom(ctx, sym);
        unwrapped = JS_GetProperty(ctx, this_val, sym_atom);
        JS_FreeAtom(ctx, sym_atom);
    }
    JS_FreeValue(ctx, sym);
    return unwrapped;
}

/* Helper: read number option with range validation */
JSValue js_numberformat_constructor(JSContext *ctx,
                                            JSValueConst func_obj,
                                            JSValueConst this_val,
                                            int argc, JSValueConst *argv,
                                            int flags)
{

    JSNumberFormatData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    JSValueConst new_target;
    int is_new;

    is_new = (flags & JS_CALL_FLAG_CONSTRUCTOR) != 0;

    if (is_new) {
        new_target = this_val;
    } else {
        /* Function call: implement ChainNumberFormat */
        new_target = func_obj;
    }

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    /* defaults */
    s->locale = js_strdup(ctx, "en-US");
    s->numberingSystem = NS_LATN;
    s->style = NF_STYLE_DECIMAL;
    s->currencyDisplay = NF_CURRENCYDISPLAY_SYMBOL;
    s->currencySign = NF_CURRENCYSIGN_STANDARD;
    s->unitDisplay = NF_UNITDISPLAY_SHORT;
    s->minimumIntegerDigits = 1;
    s->minimumFractionDigits = 0;
    s->maximumFractionDigits = 3;
    s->minimumSignificantDigits = 1;
    s->maximumSignificantDigits = 21;
    s->hasMinimumSignificantDigits = 0;
    s->hasMaximumSignificantDigits = 0;
    s->useGrouping = NF_GROUPING_AUTO;
    s->notation = NF_NOTATION_STANDARD;
    s->signDisplay = NF_SIGNDISPLAY_AUTO;
    s->roundingMode = NF_ROUNDINGMODE_HALFEXPAND;
    s->roundingPriority = NF_ROUNDINGPRIORITY_AUTO;
    s->roundingIncrement = 1;
    s->trailingZeroDisplay = NF_TRAILINGZERO_AUTO;
    s->compactDisplay = NF_COMPACT_UNSET;

    {
        char *canonical = NULL;
        if (js_canonicalizeLocaleList_first(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &canonical) < 0)
            goto fail;
        s->locale_canonical = canonical;
        if (canonical) {
            char *ns_str = js_intl_locale_get_numbering_system(ctx, canonical);
            if (ns_str) {
                int ns = js_intl_string_to_numbering_system(ns_str);
                if (ns != NS_UNKNOWN)
                    s->numberingSystem = ns;
                js_free(ctx, ns_str);
            }
            {
                char *stripped = js_intl_strip_unicode_extensions(ctx, canonical);
                if (stripped) {
                    js_free(ctx, s->locale);
                    s->locale = stripped;
                }
            }
        }
    }

    /* parse options */
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;

        /* ResolveOptions reads: localeMatcher, numberingSystem */
        {
            int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                                 intl_localeMatcher_names,
                                                 countof(intl_localeMatcher_names),
                                                 INTL_LOCALEMATCHER_BESTFIT);
            if (idx < 0)
                goto fail;
        }

        {
            JSValue ns_val = JS_GetPropertyStr(ctx, options, "numberingSystem");
            if (JS_IsException(ns_val))
                goto fail;
            if (!JS_IsUndefined(ns_val)) {
                const char *ns_str = JS_ToCString(ctx, ns_val);
                JS_FreeValue(ctx, ns_val);
                if (ns_str) {
                    int valid = 1;
                    int len = (int)strlen(ns_str);
                    int i = 0;
                    if (len == 0) valid = 0;
                    while (i < len && valid) {
                        int seg_len = 0;
                        while (i < len && ns_str[i] != '-') {
                            if (!isalnum((unsigned char)ns_str[i])) {
                                valid = 0;
                                break;
                            }
                            seg_len++;
                            i++;
                        }
                        if (seg_len < 3 || seg_len > 8) valid = 0;
                        if (i < len && ns_str[i] == '-') {
                            i++;
                            if (i >= len) valid = 0;
                        }
                    }
                    if (!valid) {
                        JS_FreeCString(ctx, ns_str);
                        JS_ThrowRangeError(ctx, "invalid numberingSystem option");
                        goto fail;
                    }
                    {
                        int ns = js_intl_string_to_numbering_system(ns_str);
                        if (ns != NS_UNKNOWN) {
                            s->has_numberingSystem_option = 1;
                            s->numberingSystem = ns;
                        }
                    }
                    JS_FreeCString(ctx, ns_str);
                }
            }
        }

        /* SetNumberFormatUnitOptions order: style, currency, currencyDisplay, currencySign, unit, unitDisplay */
        {
            int idx = js_intl_get_string_option(ctx, options, "style",
                                                 nf_style_names,
                                                 countof(nf_style_names),
                                                 NF_STYLE_DECIMAL);
            if (idx < 0)
                goto fail;
            s->style = idx;
        }

        {
            JSValue currency_val = JS_GetPropertyStr(ctx, options, "currency");
            if (JS_IsException(currency_val))
                goto fail;
            if (!JS_IsUndefined(currency_val)) {
                size_t slen;
                const char *currency_str = JS_ToCStringLen(ctx, &slen, currency_val);
                JS_FreeValue(ctx, currency_val);
                if (currency_str) {
                    if (slen == 3 && strlen(currency_str) == slen &&
                        isalpha((unsigned char)currency_str[0]) &&
                        isalpha((unsigned char)currency_str[1]) &&
                        isalpha((unsigned char)currency_str[2])) {
                        char upper_currency[4];
                        upper_currency[0] = (char)toupper((unsigned char)currency_str[0]);
                        upper_currency[1] = (char)toupper((unsigned char)currency_str[1]);
                        upper_currency[2] = (char)toupper((unsigned char)currency_str[2]);
                        upper_currency[3] = '\0';
                        js_free(ctx, s->currency);
                        s->currency = js_strdup(ctx, upper_currency);
                        JS_FreeCString(ctx, currency_str);
                    } else {
                        JS_FreeCString(ctx, currency_str);
                        JS_ThrowRangeError(ctx, "invalid currency code");
                        goto fail;
                    }
                }
            }
        }

        {
            int idx = js_intl_get_string_option(ctx, options, "currencyDisplay",
                                                 nf_currencydisplay_names,
                                                 countof(nf_currencydisplay_names),
                                                 NF_CURRENCYDISPLAY_SYMBOL);
            if (idx < 0)
                goto fail;
            s->currencyDisplay = idx;
        }

        {
            int idx = js_intl_get_string_option(ctx, options, "currencySign",
                                                 nf_currencysign_names,
                                                 countof(nf_currencysign_names),
                                                 NF_CURRENCYSIGN_STANDARD);
            if (idx < 0)
                goto fail;
            s->currencySign = idx;
        }

        /* Validate currency is present when style is currency */
        if (s->style == NF_STYLE_CURRENCY && !s->currency) {
            JS_ThrowTypeError(ctx, "currency option is required when style is currency");
            goto fail;
        }

        /* Only store currency when style is currency */
        if (s->style != NF_STYLE_CURRENCY) {
            js_free(ctx, s->currency);
            s->currency = NULL;
        }

        {
            JSValue unit_val = JS_GetPropertyStr(ctx, options, "unit");
            if (JS_IsException(unit_val))
                goto fail;
            if (!JS_IsUndefined(unit_val)) {
                const char *unit_str = JS_ToCString(ctx, unit_val);
                JS_FreeValue(ctx, unit_val);
                if (unit_str) {
                    js_free(ctx, s->unit);
                    s->unit = js_strdup(ctx, unit_str);
                    JS_FreeCString(ctx, unit_str);
                }
            }
        }

        /* Validate unit is present when style is unit, and well-formed */
        if (s->style == NF_STYLE_UNIT && !s->unit) {
            JS_ThrowTypeError(ctx, "unit option is required when style is unit");
            goto fail;
        }
        if (s->unit && !is_well_formed_unit_identifier(s->unit)) {
            JS_ThrowRangeError(ctx, "invalid unit identifier");
            goto fail;
        }

        {
            int idx = js_intl_get_string_option(ctx, options, "unitDisplay",
                                                 nf_unitdisplay_names,
                                                 countof(nf_unitdisplay_names),
                                                 NF_UNITDISPLAY_SHORT);
            if (idx < 0)
                goto fail;
            s->unitDisplay = idx;
        }

        /* notation */
        {
            int idx = js_intl_get_string_option(ctx, options, "notation",
                                                 nf_notation_names,
                                                 countof(nf_notation_names),
                                                 NF_NOTATION_STANDARD);
            if (idx < 0)
                goto fail;
            s->notation = idx;
        }

        /* Default fraction digits for currency and percent styles */
        int default_mnfd = s->minimumFractionDigits;
        int default_mxfd = s->maximumFractionDigits;

        if (s->style == NF_STYLE_CURRENCY && s->currency) {
            if (s->notation == NF_NOTATION_STANDARD) {
                int digits = js_intl_get_currency_digits(s->currency);
                default_mnfd = digits;
                default_mxfd = digits;
            } else {
                default_mnfd = 0;
                if (s->notation == NF_NOTATION_COMPACT)
                    default_mxfd = 0;
                else
                    default_mxfd = 3;
            }
        } else if (s->style == NF_STYLE_PERCENT) {
            default_mnfd = 0;
            default_mxfd = 0;
        }

        s->minimumFractionDigits = default_mnfd;
        s->maximumFractionDigits = default_mxfd;

        /* SetNumberFormatDigitOptions order: mnid, mnfd, mxfd, mnsd, mxsd, roundingIncrement, roundingMode, roundingPriority, trailingZeroDisplay */
        {
            int v, ret;
            int has_mnfd = 0, has_mxfd = 0;
            ret = js_intl_get_number_option(ctx, options, "minimumIntegerDigits", &v, 1, 21);
            if (ret < 0) goto fail;
            if (ret > 0) s->minimumIntegerDigits = v;

            ret = js_intl_get_number_option(ctx, options, "minimumFractionDigits", &v, 0, 100);
            if (ret < 0) goto fail;
            if (ret > 0) {
                s->minimumFractionDigits = v;
                has_mnfd = 1;
            }

            ret = js_intl_get_number_option(ctx, options, "maximumFractionDigits", &v, 0, 100);
            if (ret < 0) goto fail;
            if (ret > 0) {
                s->maximumFractionDigits = v;
                has_mxfd = 1;
            }

            /* Apply spec adjustment when only one of mnfd/mxfd is provided */
            if (has_mxfd && !has_mnfd) {
                s->minimumFractionDigits = s->maximumFractionDigits < default_mnfd ? s->maximumFractionDigits : default_mnfd;
            } else if (has_mnfd && !has_mxfd) {
                s->maximumFractionDigits = s->minimumFractionDigits > default_mxfd ? s->minimumFractionDigits : default_mxfd;
            }

            ret = js_intl_get_number_option(ctx, options, "minimumSignificantDigits", &v, 1, 21);
            if (ret < 0) goto fail;
            if (ret > 0) {
                s->minimumSignificantDigits = v;
                s->hasMinimumSignificantDigits = 1;
            }

            ret = js_intl_get_number_option(ctx, options, "maximumSignificantDigits", &v, 1, 21);
            if (ret < 0) goto fail;
            if (ret > 0) {
                s->maximumSignificantDigits = v;
                s->hasMaximumSignificantDigits = 1;
            }

            /* If either significant digits option is set, apply defaults and validate */
            if (s->hasMinimumSignificantDigits || s->hasMaximumSignificantDigits) {
                int mnsd = s->hasMinimumSignificantDigits ? s->minimumSignificantDigits : 1;
                int mxsd = s->hasMaximumSignificantDigits ? s->maximumSignificantDigits : 21;
                if (mxsd < mnsd) {
                    JS_ThrowRangeError(ctx, "maximumSignificantDigits must be >= minimumSignificantDigits");
                    goto fail;
                }
                s->minimumSignificantDigits = mnsd;
                s->maximumSignificantDigits = mxsd;
            }

            ret = js_intl_get_number_option(ctx, options, "roundingIncrement", &v, 1, 5000);
            if (ret < 0) goto fail;
            if (ret > 0) {
                static const int valid_increments[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2000, 2500, 5000};
                int valid = 0;
                int j;
                for (j = 0; j < 15; j++) {
                    if (valid_increments[j] == v) {
                        valid = 1;
                        break;
                    }
                }
                if (!valid) {
                    JS_ThrowRangeError(ctx, "roundingIncrement out of range");
                    goto fail;
                }
                s->roundingIncrement = v;
            }

            ret = js_intl_get_string_option(ctx, options, "roundingMode",
                                            nf_roundingmode_names,
                                            countof(nf_roundingmode_names),
                                            NF_ROUNDINGMODE_HALFEXPAND);
            if (ret < 0) goto fail;
            s->roundingMode = ret;

            ret = js_intl_get_string_option(ctx, options, "roundingPriority",
                                            nf_roundingpriority_names,
                                            countof(nf_roundingpriority_names),
                                            NF_ROUNDINGPRIORITY_AUTO);
            if (ret < 0) goto fail;
            s->roundingPriority = ret;

            /* Validate roundingIncrement constraints */
            if (s->roundingIncrement > 1) {
                if (s->roundingPriority != NF_ROUNDINGPRIORITY_AUTO) {
                    JS_ThrowTypeError(ctx, "roundingIncrement cannot be used with morePrecision or lessPrecision");
                    goto fail;
                }
                if (s->hasMinimumSignificantDigits || s->hasMaximumSignificantDigits) {
                    JS_ThrowTypeError(ctx, "roundingIncrement cannot be used with significant digits");
                    goto fail;
                }
                if (s->maximumFractionDigits != s->minimumFractionDigits) {
                    JS_ThrowRangeError(ctx, "maximumFractionDigits must equal minimumFractionDigits when roundingIncrement is used");
                    goto fail;
                }
            }

            ret = js_intl_get_string_option(ctx, options, "trailingZeroDisplay",
                                            nf_trailingzero_names,
                                            countof(nf_trailingzero_names),
                                            NF_TRAILINGZERO_AUTO);
            if (ret < 0) goto fail;
            s->trailingZeroDisplay = ret;

            /* Compact notation default digit options per SetNumberFormatDigitOptions */
            if (s->notation == NF_NOTATION_COMPACT &&
                s->roundingPriority == NF_ROUNDINGPRIORITY_AUTO &&
                !s->hasMinimumSignificantDigits && !s->hasMaximumSignificantDigits &&
                !has_mnfd && !has_mxfd) {
                s->minimumFractionDigits = 0;
                s->maximumFractionDigits = 0;
                s->minimumSignificantDigits = 1;
                s->maximumSignificantDigits = 2;
                s->roundingPriority = NF_ROUNDINGPRIORITY_MOREPRECISION;
            }
        }

        /* Back to constructor: compactDisplay, useGrouping, signDisplay */
        {
            int idx = js_intl_get_string_option(ctx, options, "compactDisplay",
                                                 nf_compact_names,
                                                 countof(nf_compact_names),
                                                 NF_COMPACT_UNSET);
            if (idx < 0)
                goto fail;
            s->compactDisplay = idx;
        }

        /* useGrouping: GetBooleanOrStringNumberFormatOption */
        {
            static const char *ug_string_values[] = {"min2", "auto", "always", "true", "false"};
            JSValue v = JS_GetPropertyStr(ctx, options, "useGrouping");
            if (JS_IsException(v))
                goto fail;
            if (JS_IsUndefined(v)) {
                JS_FreeValue(ctx, v);
                s->useGrouping = (s->notation == NF_NOTATION_COMPACT) ? NF_GROUPING_MIN2 : NF_GROUPING_AUTO;
            } else if (JS_IsBool(v) && JS_ToBool(ctx, v)) {
                JS_FreeValue(ctx, v);
                s->useGrouping = NF_GROUPING_ALWAYS;
            } else if (JS_ToBool(ctx, v) == 0) {
                JS_FreeValue(ctx, v);
                s->useGrouping = NF_GROUPING_FALSE_BOOL;
            } else {
                {
                    int i, found = 0;
                    const char *str = JS_ToCString(ctx, v);
                    JS_FreeValue(ctx, v);
                    if (!str)
                        goto fail;
                    for (i = 0; i < 5; i++) {
                        if (!strcmp(str, ug_string_values[i])) {
                            found = 1;
                            break;
                        }
                    }
                    JS_FreeCString(ctx, str);
                    if (!found) {
                        JS_ThrowRangeError(ctx, "invalid useGrouping option");
                        goto fail;
                    }
                    if (i == 3 || i == 4) { /* "true" or "false" -> fallback */
                        s->useGrouping = (s->notation == NF_NOTATION_COMPACT) ? NF_GROUPING_MIN2 : NF_GROUPING_AUTO;
                    } else if (i == 0) {
                        s->useGrouping = NF_GROUPING_MIN2;
                    } else if (i == 1) {
                        s->useGrouping = NF_GROUPING_AUTO;
                    } else {
                        s->useGrouping = NF_GROUPING_ALWAYS;
                    }
                }
            }
        }

        {
            int idx = js_intl_get_string_option(ctx, options, "signDisplay",
                                                 nf_signdisplay_names,
                                                 countof(nf_signdisplay_names),
                                                 NF_SIGNDISPLAY_AUTO);
            if (idx < 0)
                goto fail;
            s->signDisplay = idx;
        }

        JS_FreeValue(ctx, options);
        options = JS_UNDEFINED;
    }

    /* compactDisplay defaults to "short" when notation is compact */
    if (s->notation == NF_NOTATION_COMPACT && s->compactDisplay == NF_COMPACT_UNSET)
        s->compactDisplay = NF_COMPACT_SHORT;

    /* currency style must have currency option */
    if (s->style == NF_STYLE_CURRENCY && !s->currency) {
        JS_ThrowTypeError(ctx, "currency code is required with currency style");
        goto fail;
    }

    /* Rebuild locale with preserved Unicode extensions */
    if (s->locale_canonical) {
        int preserve_nu = 0;

        if (js_intl_locale_has_key(s->locale_canonical, "nu")) {
            char *ext_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
            if (ext_val) {
                int ext_ns = js_intl_string_to_numbering_system(ext_val);
                if (!s->has_numberingSystem_option || ext_ns == s->numberingSystem)
                    preserve_nu = 1;
                js_free(ctx, ext_val);
            }
        }

        if (preserve_nu) {
            DynBuf db;
            js_intl_dbuf_init(ctx, &db);
            dbuf_putstr(&db, s->locale);
            dbuf_put(&db, (uint8_t *)"-u", (size_t)2);
            {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-nu-", (size_t)4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            dbuf_put(&db, (uint8_t *)"", (size_t)1);
            if (dbuf_error(&db)) {
                dbuf_free(&db);
                return JS_EXCEPTION;
            }
            js_free(ctx, s->locale);
            s->locale = js_strdup(ctx, (char *)db.buf);
            dbuf_free(&db);
        }
    }

    obj = JS_CreateFromConstructor(ctx, new_target, js_numberformat_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);

    if (!is_new) {
        /* ChainNumberFormat: if this is a NumberFormat instance, define fallback symbol */
        JSValue ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
        int is_instance = JS_OrdinaryIsInstanceOf(ctx, this_val, ctor);
        JS_FreeValue(ctx, ctor);
        if (is_instance < 0) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        if (is_instance) {
            JSValue sym = JS_GetContextValue(ctx, JS_INTL_FALLBACK_SYMBOL);
            if (JS_IsException(sym)) {
                JS_FreeValue(ctx, obj);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(sym)) {
                JSAtom sym_atom = JS_ValueToAtom(ctx, sym);
                JS_DefinePropertyValue(ctx, this_val, sym_atom, JS_DupValue(ctx, obj), 0);
                JS_FreeAtom(ctx, sym_atom);
            }
            JS_FreeValue(ctx, sym);
            JS_FreeValue(ctx, obj);
            return JS_DupValue(ctx, this_val);
        }
    }
    return obj;
 fail:
    JS_FreeValue(ctx, options);
    if (s->locale) js_free(ctx, s->locale);
    if (s->locale_canonical) js_free(ctx, s->locale_canonical);
    if (s->currency) js_free(ctx, s->currency);
    if (s->unit) js_free(ctx, s->unit);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* Helper: format integer part with grouping */

typedef struct {
    const char *locale_prefix;
    const char *unit;
    const char *short_pattern;
    const char *short_singular;
    const char *narrow_pattern;
    const char *narrow_singular;
    const char *long_pattern;
    const char *long_singular;
} UnitPatternEntry;

static const UnitPatternEntry unit_patterns[] = {
    {"en", "percent", "{number}%", NULL, "{number}%", NULL, "{number} percent", NULL},
    {"en", "kilometer-per-hour", "{number} km/h", NULL, "{number}km/h", NULL, "{number} kilometers per hour", NULL},
    {"en", "day", "{number} days", "{number} day", "{number}d", NULL, "{number} days", "{number} day"},
    {"en", "hour", "{number} hr", NULL, "{number}h", NULL, "{number} hours", "{number} hour"},
    {"en", "minute", "{number} min", NULL, "{number}m", NULL, "{number} minutes", "{number} minute"},
    {"en", "second", "{number} sec", NULL, "{number}s", NULL, "{number} seconds", "{number} second"},
    {"en", "week", "{number} wks", "{number} wk", "{number}w", NULL, "{number} weeks", "{number} week"},
    {"en", "month", "{number} mths", "{number} mth", "{number}m", NULL, "{number} months", "{number} month"},
    {"en", "year", "{number} yrs", "{number} yr", "{number}y", NULL, "{number} years", "{number} year"},
    {"en-GB", "hour", "{number} hr", NULL, "{number}h", NULL, "{number} hours", "{number} hour"},
    {"en-GB", "minute", "{number} mins", "{number} min", "{number}m", NULL, "{number} minutes", "{number} minute"},
    {"en-GB", "second", "{number} secs", "{number} sec", "{number}s", NULL, "{number} seconds", "{number} second"},
    {"de", "kilometer-per-hour", "{number} km/h", NULL, "{number} km/h", NULL, "{number} Kilometer pro Stunde", NULL},
    {"de", "hour", "{number} Std.", NULL, "{number}h", NULL, "{number} Stunden", "{number} Stunde"},
    {"de", "minute", "{number} Min.", NULL, "{number} Min.", NULL, "{number} Minuten", "{number} Minute"},
    {"de", "second", "{number} Sek.", NULL, "{number} Sek.", NULL, "{number} Sekunden", "{number} Sekunde"},
    {"ja", "kilometer-per-hour", "{number} km/h", NULL, "{number}km/h", NULL, "時速 {number} キロメートル", NULL},
    {"ja", "hour", "{number} 時間", NULL, "{number}h", NULL, "{number} 時間", NULL},
    {"ja", "minute", "{number} 分", NULL, "{number}m", NULL, "{number} 分", NULL},
    {"ja", "second", "{number} 秒", NULL, "{number}s", NULL, "{number} 秒", NULL},
    {"ko", "kilometer-per-hour", "{number}km/h", NULL, "{number}km/h", NULL, "시속 {number}킬로미터", NULL},
    {"zh", "kilometer-per-hour", "{number} 公里/小時", NULL, "{number}公里/小時", NULL, "每小時 {number} 公里", NULL},
    {"zh", "hour", "{number}小时", NULL, "{number}h", NULL, "{number}小时", NULL},
    {"zh", "minute", "{number}分钟", NULL, "{number}m", NULL, "{number}分钟", NULL},
    {"zh", "second", "{number}秒", NULL, "{number}s", NULL, "{number}秒", NULL},
    {"zh-Hant", "hour", "{number} 小時", NULL, "{number}h", NULL, "{number} 小時", NULL},
    {"zh-Hant", "minute", "{number} 分鐘", NULL, "{number}m", NULL, "{number} 分鐘", NULL},
    {"zh-Hant", "second", "{number} 秒", NULL, "{number}s", NULL, "{number} 秒", NULL},
};

static int unit_value_is_one(const IntlMV *mv)
{
    int i;
    if (mv->num_digits == 0)
        return 0;
    if (mv->digits[0] != '1')
        return 0;
    for (i = 1; i < mv->num_digits; i++) {
        if (mv->digits[i] != '0')
            return 0;
    }
    return mv->exponent + mv->num_digits - 1 == 0;
}

static const char *get_unit_pattern(const UnitPatternEntry *e, int unitDisplay, int is_one)
{
    const char *pat;
    if (unitDisplay == NF_UNITDISPLAY_NARROW) {
        pat = is_one && e->narrow_singular ? e->narrow_singular : e->narrow_pattern;
    } else if (unitDisplay == NF_UNITDISPLAY_LONG) {
        pat = is_one && e->long_singular ? e->long_singular : e->long_pattern;
    } else {
        pat = is_one && e->short_singular ? e->short_singular : e->short_pattern;
    }
    return pat;
}

static const UnitPatternEntry *find_unit_pattern(const char *locale, const char *unit)
{
    int i;
    int locale_len = (int)strlen(locale);
    /* First pass: exact match */
    for (i = 0; i < (int)countof(unit_patterns); i++) {
        const UnitPatternEntry *e = &unit_patterns[i];
        if (!strcmp(locale, e->locale_prefix) && !strcmp(unit, e->unit))
            return e;
    }
    /* Second pass: prefix match (only if remainder is '\0' or '-') */
    for (i = 0; i < (int)countof(unit_patterns); i++) {
        const UnitPatternEntry *e = &unit_patterns[i];
        int prefix_len = (int)strlen(e->locale_prefix);
        if (locale_len >= prefix_len && !memcmp(locale, e->locale_prefix, (size_t)prefix_len) &&
            !strcmp(unit, e->unit)) {
            char next = locale[prefix_len];
            if (next == '\0' || next == '-')
                return e;
        }
    }
    return NULL;
}

static JSValue nf_finish_with_digits(JSContext *ctx, DynBuf *db, int numberingSystem)
{
    char *new_buf;
    int i, j;
    int len;
    JSValue ret;

    if (dbuf_error(db)) {
        dbuf_free(db);
        return JS_EXCEPTION;
    }

    if (numberingSystem == NS_LATN) {
        ret = JS_NewString(ctx, (char *)db->buf);
        dbuf_free(db);
        return ret;
    }

    len = (int)db->size - 1; /* exclude '\0' */
    new_buf = js_malloc(ctx, (size_t)(len * 4 + 1));
    if (!new_buf) {
        dbuf_free(db);
        return JS_EXCEPTION;
    }

    j = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = db->buf[i];
        if (c >= '0' && c <= '9') {
            uint32_t cp = nf_get_digit(numberingSystem, c - '0');
            if (cp != 0) {
                j += unicode_to_utf8((uint8_t *)new_buf + j, cp);
            } else {
                new_buf[j++] = (char)c;
            }
        } else {
            new_buf[j++] = (char)c;
        }
    }
    new_buf[j] = '\0';
    dbuf_free(db);
    ret = JS_NewString(ctx, new_buf);
    js_free(ctx, new_buf);
    return ret;
}

static void add_grouping(DynBuf *db, const char *digits, int len, const char *group_sep, int min2,
                         int group_size1, int group_size2)
{
    int first_group;
    int i;
    int remaining = len;
    int groups[20];
    int num_groups = 0;
    int gi;

    /* Determine group boundaries from right to left */
    if (remaining > 0) {
        int gs1 = group_size1 > 0 ? group_size1 : 3;
        int gs2 = group_size2 > 0 ? group_size2 : gs1;
        int first = remaining > gs1 ? gs1 : remaining;
        groups[num_groups++] = first;
        remaining -= first;
        while (remaining > 0) {
            int g = remaining > gs2 ? gs2 : remaining;
            groups[num_groups++] = g;
            remaining -= g;
        }
    }

    if (num_groups == 0) {
        dbuf_put(db, (const uint8_t *)digits, (size_t)len);
        return;
    }

    /* The most significant group is at the end of groups[] */
    first_group = groups[num_groups - 1];

    if (min2 && num_groups == 2 && first_group < 2) {
        /* min2: don't group if there are exactly 2 groups and the most significant group has < 2 digits */
        dbuf_put(db, (const uint8_t *)digits, (size_t)len);
        return;
    }

    /* Output groups from left to right */
    i = 0;
    for (gi = num_groups - 1; gi >= 0; gi--) {
        if (gi < num_groups - 1)
            dbuf_putstr(db, group_sep);
        dbuf_put(db, (const uint8_t *)digits + i, (size_t)groups[gi]);
        i += groups[gi];
    }
}

static void nf_add_part(JSContext *ctx, JSValue parts, const char *type, const char *value)
{
    JSValue part;
    int32_t len = 0;
    JSValue lv;

    if (JS_IsUndefined(parts))
        return;

    part = JS_NewObject(ctx);
    if (JS_IsException(part))
        return;
    JS_SetPropertyStr(ctx, part, "type", JS_NewString(ctx, type));
    JS_SetPropertyStr(ctx, part, "value", JS_NewString(ctx, value));

    lv = JS_GetPropertyStr(ctx, parts, "length");
    if (!JS_IsException(lv)) {
        JS_ToInt32(ctx, &len, lv);
        JS_FreeValue(ctx, lv);
    }
    JS_DefinePropertyValueUint32(ctx, parts, (uint32_t)len, part, JS_PROP_C_W_E);
}

static void nf_add_grouped_integer_parts(JSContext *ctx, JSValue parts, const char *digits, int len,
                                          const char *group_sep, int min2,
                                          int group_size1, int group_size2)
{
    int remaining = len;
    int groups[20];
    int num_groups = 0;
    int gi;
    int first_group;
    char *tmp;
    int i;

    if (remaining > 0) {
        int gs1 = group_size1 > 0 ? group_size1 : 3;
        int gs2 = group_size2 > 0 ? group_size2 : gs1;
        int first = remaining > gs1 ? gs1 : remaining;
        groups[num_groups++] = first;
        remaining -= first;
        while (remaining > 0) {
            int g = remaining > gs2 ? gs2 : remaining;
            groups[num_groups++] = g;
            remaining -= g;
        }
    }

    if (num_groups == 0) {
        tmp = js_malloc(ctx, (size_t)len + 1);
        if (!tmp) return;
        memcpy(tmp, digits, (size_t)len);
        tmp[len] = '\0';
        nf_add_part(ctx, parts, "integer", tmp);
        js_free(ctx, tmp);
        return;
    }

    first_group = groups[num_groups - 1];

    if (min2 && num_groups == 2 && first_group < 2) {
        tmp = js_malloc(ctx, (size_t)len + 1);
        if (!tmp) return;
        memcpy(tmp, digits, (size_t)len);
        tmp[len] = '\0';
        nf_add_part(ctx, parts, "integer", tmp);
        js_free(ctx, tmp);
        return;
    }

    i = 0;
    for (gi = num_groups - 1; gi >= 0; gi--) {
        if (gi < num_groups - 1)
            nf_add_part(ctx, parts, "group", group_sep);
        tmp = js_malloc(ctx, (size_t)(groups[gi] + 1));
        if (!tmp) continue;
        memcpy(tmp, digits + i, (size_t)groups[gi]);
        tmp[groups[gi]] = '\0';
        nf_add_part(ctx, parts, "integer", tmp);
        js_free(ctx, tmp);
        i += groups[gi];
    }
}

static JSValue intl_mv_format_standard(JSContext *ctx, IntlMV *mv, JSNumberFormatData *s,
                                        const LocaleData *ldata, int strip_trailing_zeros,
                                        JSValue parts)
{
    DynBuf db;
    JSValue ret;
    const char *pattern;
    int is_negative = mv->sign;
    int is_zero = (mv->num_digits == 1 && mv->digits[0] == '0');
    char *int_str = NULL;
    char *frac_str = NULL;
    int int_len = 0;
    int frac_len = 0;
    int i;

    js_intl_dbuf_init(ctx, &db);

    /* Build integer and fraction strings */
    if (mv->exponent >= 0) {
        /* Pure integer: digits followed by exponent zeros */
        int_len = mv->num_digits + mv->exponent;
        int_str = js_malloc(ctx, (size_t)int_len + 1);
        if (!int_str) goto fail;
        memcpy(int_str, mv->digits, (size_t)mv->num_digits);
        for (i = 0; i < mv->exponent; i++)
            int_str[mv->num_digits + i] = '0';
        int_str[int_len] = '\0';
        frac_len = 0;
    } else {
        int int_part = mv->num_digits + mv->exponent; /* may be <= 0 */
        if (int_part > 0) {
            int_len = int_part;
            int_str = js_malloc(ctx, (size_t)int_len + 1);
            if (!int_str) goto fail;
            memcpy(int_str, mv->digits, (size_t)int_len);
            int_str[int_len] = '\0';
            frac_len = -mv->exponent;
            frac_str = js_malloc(ctx, (size_t)frac_len + 1);
            if (!frac_str) goto fail;
            memcpy(frac_str, mv->digits + int_part, (size_t)frac_len);
            frac_str[frac_len] = '\0';
        } else if (int_part == 0) {
            int_len = 1;
            int_str = js_strdup(ctx, "0");
            if (!int_str) goto fail;
            frac_len = mv->num_digits;
            frac_str = js_malloc(ctx, (size_t)frac_len + 1);
            if (!frac_str) goto fail;
            memcpy(frac_str, mv->digits, (size_t)frac_len);
            frac_str[frac_len] = '\0';
        } else {
            /* int_part < 0: leading zeros in fraction */
            int leading_zeros = (int)(-mv->exponent - mv->num_digits);
            int_len = 1;
            int_str = js_strdup(ctx, "0");
            if (!int_str) goto fail;
            frac_len = -mv->exponent;
            frac_str = js_malloc(ctx, (size_t)frac_len + 1);
            if (!frac_str) goto fail;
            for (i = 0; i < leading_zeros; i++)
                frac_str[i] = '0';
            memcpy(frac_str + leading_zeros, mv->digits, (size_t)mv->num_digits);
            frac_str[frac_len] = '\0';
        }
    }

    /* Strip trailing zeros down to minimumFractionDigits */
    if (frac_len > 0 && strip_trailing_zeros) {
        while (frac_len > s->minimumFractionDigits && frac_str[frac_len - 1] == '0')
            frac_len--;
        if (frac_len == 0) {
            js_free(ctx, frac_str);
            frac_str = NULL;
        } else {
            frac_str[frac_len] = '\0';
        }
    }

    /* Pad integer part to minimumIntegerDigits */
    if (int_len < s->minimumIntegerDigits) {
        int pad = (int)(s->minimumIntegerDigits - int_len);
        char *padded = js_malloc(ctx, (size_t)(s->minimumIntegerDigits + 1));
        if (!padded) goto fail;
        memset(padded, '0', (size_t)pad);
        memcpy(padded + pad, int_str, (size_t)int_len);
        padded[s->minimumIntegerDigits] = '\0';
        js_free(ctx, int_str);
        int_str = padded;
        int_len = s->minimumIntegerDigits;
    }

    /* Select pattern based on signDisplay */
    {
        int use_negative_pattern = 0;
        int prepend_plus = 0;
        if (s->signDisplay == NF_SIGNDISPLAY_NEVER) {
            use_negative_pattern = 0;
        } else if (s->signDisplay == NF_SIGNDISPLAY_NEGATIVE) {
            use_negative_pattern = is_negative && !is_zero;
        } else if (s->signDisplay == NF_SIGNDISPLAY_EXCEPTZERO) {
            use_negative_pattern = is_negative && !is_zero;
            prepend_plus = !is_negative && !is_zero;
        } else if (s->signDisplay == NF_SIGNDISPLAY_ALWAYS) {
            use_negative_pattern = is_negative;
            prepend_plus = !is_negative;
        } else {
            /* auto */
            use_negative_pattern = is_negative;
        }
        if (prepend_plus) {
            dbuf_putstr(&db, NS_PLUS(ldata->symbols));
            if (!JS_IsUndefined(parts))
                nf_add_part(ctx, parts, "plusSign", NS_PLUS(ldata->symbols));
        }
        if (s->style == NF_STYLE_PERCENT) {
            pattern = use_negative_pattern ? NP_PCT_NEG(ldata->patterns) : NP_PCT_POS(ldata->patterns);
        } else if (s->style == NF_STYLE_CURRENCY) {
            if (s->currencySign == NF_CURRENCYSIGN_ACCOUNTING) {
                pattern = use_negative_pattern ? NP_ACC_NEG(ldata->patterns) : NP_ACC_POS(ldata->patterns);
            } else {
                pattern = use_negative_pattern ? NP_CUR_NEG(ldata->patterns) : NP_CUR_POS(ldata->patterns);
            }
        } else {
            pattern = use_negative_pattern ? NP_DEC_NEG(ldata->patterns) : NP_DEC_POS(ldata->patterns);
        }
    }

    /* Build grouped integer string */
    {
        char *ungrouped_int_str = int_str;
        int ungrouped_int_len = int_len;
        DynBuf group_db;
        js_intl_dbuf_init(ctx, &group_db);
        if (s->useGrouping != NF_GROUPING_FALSE_BOOL) {
            int min2 = (s->useGrouping == NF_GROUPING_MIN2);
            add_grouping(&group_db, int_str, int_len, NS_GROUP(ldata->symbols), min2,
                         ldata->groupSize1, ldata->groupSize2);
        } else {
            dbuf_putstr(&group_db, int_str);
        }
        dbuf_putc(&group_db, '\0');
        /* Don't free int_str; keep ungrouped copy for parts */
        int_str = (char *)group_db.buf;
        int_len = (int)group_db.size - 1;
        /* group_db.buf is now owned by int_str, don't free group_db separately */
        group_db.buf = NULL;
        group_db.size = 0;

        /* Apply pattern */
        {
            const char *p = pattern;
            const char *next;
            while (*p) {
                if (strstart(p, "{number}", &next)) {
                    dbuf_putstr(&db, int_str);
                    if (!JS_IsUndefined(parts)) {
                        if (s->useGrouping != NF_GROUPING_FALSE_BOOL) {
                            int min2 = (s->useGrouping == NF_GROUPING_MIN2);
                            nf_add_grouped_integer_parts(ctx, parts, ungrouped_int_str, ungrouped_int_len, NS_GROUP(ldata->symbols), min2,
                                                         ldata->groupSize1, ldata->groupSize2);
                        } else {
                            nf_add_part(ctx, parts, "integer", ungrouped_int_str);
                        }
                    }
                    if (frac_len > 0) {
                        dbuf_putstr(&db, NS_DECIMAL(ldata->symbols));
                        dbuf_putstr(&db, frac_str);
                        if (!JS_IsUndefined(parts)) {
                            nf_add_part(ctx, parts, "decimal", NS_DECIMAL(ldata->symbols));
                            nf_add_part(ctx, parts, "fraction", frac_str);
                        }
                    }
                    p = next;
                } else if (strstart(p, "{plusSign}", &next)) {
                    if (!is_negative) {
                        if (s->signDisplay == NF_SIGNDISPLAY_ALWAYS ||
                            (s->signDisplay == NF_SIGNDISPLAY_EXCEPTZERO && !is_zero)) {
                            dbuf_putstr(&db, NS_PLUS(ldata->symbols));
                            if (!JS_IsUndefined(parts))
                                nf_add_part(ctx, parts, "plusSign", NS_PLUS(ldata->symbols));
                        }
                    }
                    p = next;
                } else if (strstart(p, "{minusSign}", &next)) {
                    if (is_negative) {
                        int show = 0;
                        if (s->signDisplay == NF_SIGNDISPLAY_ALWAYS) show = 1;
                        else if (s->signDisplay == NF_SIGNDISPLAY_AUTO) show = 1;
                        else if (s->signDisplay == NF_SIGNDISPLAY_EXCEPTZERO && !is_zero) show = 1;
                        else if (s->signDisplay == NF_SIGNDISPLAY_NEGATIVE && !is_zero) show = 1;
                        if (show) {
                            dbuf_putstr(&db, NS_MINUS(ldata->symbols));
                            if (!JS_IsUndefined(parts))
                                nf_add_part(ctx, parts, "minusSign", NS_MINUS(ldata->symbols));
                        }
                    }
                    p = next;
                } else if (strstart(p, "{percentSign}", &next)) {
                    dbuf_putstr(&db, NS_PERCENT(ldata->symbols));
                    if (!JS_IsUndefined(parts))
                        nf_add_part(ctx, parts, "percentSign", NS_PERCENT(ldata->symbols));
                    p = next;
                } else if (strstart(p, "{currency}", &next)) {
                    if (s->currency) {
                        const char *currency_str;
                        if (s->currencyDisplay == NF_CURRENCYDISPLAY_CODE)
                            currency_str = s->currency;
                        else
                            currency_str = js_intl_get_currency_symbol(s->currency, s->locale);
                        dbuf_putstr(&db, currency_str);
                        if (!JS_IsUndefined(parts))
                            nf_add_part(ctx, parts, "currency", currency_str);
                    }
                    p = next;
                } else {
                    const char *lit_start = p;
                    while (*p && !strstart(p, "{", &next)) {
                        p++;
                    }
                    if (*p) {
                        dbuf_put(&db, (const uint8_t *)lit_start, (size_t)(p - lit_start));
                    } else {
                        dbuf_putstr(&db, lit_start);
                    }
                    if (!JS_IsUndefined(parts)) {
                        int lit_len = (int)(p - lit_start);
                        if (lit_len > 0) {
                            char *lit = js_malloc(ctx, (size_t)lit_len + 1);
                            if (lit) {
                                memcpy(lit, lit_start, (size_t)lit_len);
                                lit[lit_len] = '\0';
                                nf_add_part(ctx, parts, "literal", lit);
                                js_free(ctx, lit);
                            }
                        }
                    }
                }
            }
        }

        dbuf_putc(&db, '\0');
        ret = nf_finish_with_digits(ctx, &db, s->numberingSystem);
        js_free(ctx, ungrouped_int_str);
        goto cleanup;
    }

fail:
    ret = JS_EXCEPTION;
cleanup:
    dbuf_free(&db);
    js_free(ctx, frac_str);
    js_free(ctx, int_str);
    return ret;
}

/* Format scientific or engineering notation */
static JSValue intl_mv_format_scientific(JSContext *ctx, IntlMV *mv, JSNumberFormatData *s,
                                          const LocaleData *ldata, int is_engineering,
                                          JSValue parts)
{
    DynBuf db;
    JSValue ret;
    int total_exp = (int)(mv->exponent + mv->num_digits - 1);
    int eng_exp;
    int digits_before;
    int is_negative = mv->sign;
    int is_zero = (mv->num_digits == 1 && mv->digits[0] == '0');
    const char *sign_str = "";
    int i;

    js_intl_dbuf_init(ctx, &db);

    if (is_engineering) {
        if (total_exp >= 0) {
            eng_exp = (total_exp / 3) * 3;
        } else {
            eng_exp = ((total_exp - 2) / 3) * 3;
        }
        digits_before = total_exp - eng_exp + 1;
    } else {
        eng_exp = total_exp;
        digits_before = 1;
    }

    /* Determine sign */
    if (s->signDisplay == NF_SIGNDISPLAY_NEVER) {
        sign_str = "";
    } else if (s->signDisplay == NF_SIGNDISPLAY_ALWAYS) {
        sign_str = is_negative ? NS_MINUS(ldata->symbols) : NS_PLUS(ldata->symbols);
    } else if (s->signDisplay == NF_SIGNDISPLAY_EXCEPTZERO) {
        if (!is_zero)
            sign_str = is_negative ? NS_MINUS(ldata->symbols) : NS_PLUS(ldata->symbols);
    } else if (s->signDisplay == NF_SIGNDISPLAY_NEGATIVE) {
        if (is_negative && !is_zero)
            sign_str = NS_MINUS(ldata->symbols);
    } else {
        /* auto */
        if (is_negative)
            sign_str = NS_MINUS(ldata->symbols);
    }

    if (sign_str[0]) {
        dbuf_putstr(&db, sign_str);
        if (!JS_IsUndefined(parts))
            nf_add_part(ctx, parts, is_negative ? "minusSign" : "plusSign", sign_str);
    }

    /* Integer part of mantissa */
    {
        char *int_buf = js_malloc(ctx, (size_t)digits_before + 1);
        if (!int_buf) goto sci_fail;
        memcpy(int_buf, mv->digits, (size_t)digits_before);
        int_buf[digits_before] = '\0';
        dbuf_putstr(&db, int_buf);
        if (!JS_IsUndefined(parts))
            nf_add_part(ctx, parts, "integer", int_buf);
        js_free(ctx, int_buf);
    }

    /* Fraction part */
    if (mv->num_digits > digits_before) {
        int frac_len = (int)(mv->num_digits - digits_before);
        char *frac_buf = js_malloc(ctx, (size_t)frac_len + 1);
        if (!frac_buf) goto sci_fail;
        memcpy(frac_buf, mv->digits + digits_before, (size_t)frac_len);
        frac_buf[frac_len] = '\0';

        /* Strip trailing zeros down to minimumFractionDigits */
        if (frac_len > s->minimumFractionDigits) {
            while (frac_len > s->minimumFractionDigits && frac_buf[frac_len - 1] == '0')
                frac_len--;
            frac_buf[frac_len] = '\0';
        }

        if (frac_len > 0) {
            dbuf_putstr(&db, NS_DECIMAL(ldata->symbols));
            dbuf_putstr(&db, frac_buf);
            if (!JS_IsUndefined(parts)) {
                nf_add_part(ctx, parts, "decimal", NS_DECIMAL(ldata->symbols));
                nf_add_part(ctx, parts, "fraction", frac_buf);
            }
        }
        js_free(ctx, frac_buf);
    }

    /* Exponent */
    dbuf_putstr(&db, "E");
    if (!JS_IsUndefined(parts))
        nf_add_part(ctx, parts, "exponentSeparator", "E");

    if (eng_exp < 0) {
        dbuf_putstr(&db, "-");
        if (!JS_IsUndefined(parts))
            nf_add_part(ctx, parts, "exponentMinusSign", "-");
        eng_exp = -eng_exp;
    }

    {
        char exp_buf[32];
        int n = eng_exp;
        int exp_len = 0;
        if (n == 0) {
            exp_buf[0] = '0';
            exp_len = 1;
        } else {
            while (n > 0) {
                exp_buf[exp_len++] = (char)('0' + (n % 10));
                n /= 10;
            }
            for (i = 0; i < exp_len / 2; i++) {
                char t = exp_buf[i];
                exp_buf[i] = exp_buf[exp_len - 1 - i];
                exp_buf[exp_len - 1 - i] = t;
            }
        }
        exp_buf[exp_len] = '\0';
        dbuf_putstr(&db, exp_buf);
        if (!JS_IsUndefined(parts))
            nf_add_part(ctx, parts, "exponentInteger", exp_buf);
    }

    dbuf_putc(&db, '\0');
    ret = nf_finish_with_digits(ctx, &db, s->numberingSystem);
    goto sci_cleanup;

sci_fail:
    ret = JS_EXCEPTION;
sci_cleanup:
    dbuf_free(&db);
    return ret;
}

static int intl_mv_is_at_least_scale(IntlMV *mv, int scale_exp)
{
    int exp_diff = (int)(mv->exponent - scale_exp);
    if (exp_diff >= 0)
        return 1;
    int threshold_digit_count = -exp_diff + 1; /* number of digits in 10^(-exp_diff) */
    if (mv->num_digits > threshold_digit_count)
        return 1;
    if (mv->num_digits < threshold_digit_count)
        return 0;
    return 1;
}

static JSValue intl_mv_format_compact(JSContext *ctx, IntlMV *mv, JSNumberFormatData *s,
                                       const LocaleData *ldata, JSValue parts)
{
    const CompactDecimalPattern *cp = NULL;
    const char *pat_str = NULL;
    int scale_exp = 0;
    int i;
    JSNumberFormatData s2;
    IntlMV scaled_mv = {0};
    JSValue num_str = JS_UNDEFINED;
    const char *num_cstr = NULL;
    const char *suffix_start = NULL;
    JSValue ret = JS_EXCEPTION;
    DynBuf db;

    for (i = ldata->numCompactPatterns - 1; i >= 0; i--) {
        const CompactDecimalPattern *p = &ldata->compactPatterns[i];
        int use = 0;
        if (s->compactDisplay == NF_COMPACT_LONG) {
            if (p->longPattern) use = 1;
        } else {
            if (p->shortPattern) use = 1;
        }
        if (use && intl_mv_is_at_least_scale(mv, p->scale_exp)) {
            cp = p;
            scale_exp = p->scale_exp;
            pat_str = (s->compactDisplay == NF_COMPACT_LONG) ? p->longPattern : p->shortPattern;
            break;
        }
    }

    if (!cp) {
        return intl_mv_format_standard(ctx, mv, s, ldata, 1, parts);
    }

    scaled_mv.kind = mv->kind;
    scaled_mv.sign = mv->sign;
    scaled_mv.digits = js_strdup(ctx, mv->digits);
    if (!scaled_mv.digits)
        return JS_EXCEPTION;
    scaled_mv.num_digits = mv->num_digits;
    scaled_mv.exponent = mv->exponent - scale_exp;

    {
        int int_digits = scaled_mv.num_digits + scaled_mv.exponent;
        int compact_maxFD, compact_minFD;
        if (int_digits <= 0) int_digits = 0;
        if (int_digits == 1) {
            compact_maxFD = 1;
            compact_minFD = 0;
        } else {
            compact_maxFD = 0;
            compact_minFD = 0;
        }

        s2 = *s;
        s2.style = NF_STYLE_DECIMAL;
        s2.currency = NULL;
        s2.unit = NULL;
        s2.maximumFractionDigits = compact_maxFD;
        s2.minimumFractionDigits = compact_minFD;

        if (intl_mv_round_fraction(ctx, &scaled_mv, compact_maxFD, compact_minFD, 1, s->roundingMode)) {
            intl_mv_free(ctx, &scaled_mv);
            return JS_EXCEPTION;
        }
    }

    if (scaled_mv.num_digits == 1 && scaled_mv.digits[0] == '0') {
        intl_mv_free(ctx, &scaled_mv);
        return intl_mv_format_standard(ctx, mv, s, ldata, 1, parts);
    }

    num_str = intl_mv_format_standard(ctx, &scaled_mv, &s2, ldata, 1, parts);
    if (JS_IsException(num_str)) {
        intl_mv_free(ctx, &scaled_mv);
        return num_str;
    }
    num_cstr = JS_ToCString(ctx, num_str);
    if (!num_cstr) {
        intl_mv_free(ctx, &scaled_mv);
        JS_FreeValue(ctx, num_str);
        return JS_EXCEPTION;
    }

    js_intl_dbuf_init(ctx, &db);

    {
        const char *p = pat_str;
        while (*p) {
            if (p[0] == '{' && p[1] == '0' && p[2] == '}') {
                suffix_start = p + 3;
                break;
            }
            p++;
        }
        if (suffix_start) {
            dbuf_put(&db, (const uint8_t *)pat_str, (size_t)(suffix_start - pat_str - 3));
            dbuf_putstr(&db, num_cstr);
            dbuf_putstr(&db, suffix_start);
        } else {
            dbuf_putstr(&db, pat_str);
            dbuf_putstr(&db, num_cstr);
        }
    }

    dbuf_putc(&db, '\0');
    ret = nf_finish_with_digits(ctx, &db, s->numberingSystem);

    if (!JS_IsUndefined(parts) && suffix_start) {
        const char *p = suffix_start;
        while (*p) {
            if (*p == ' ') {
                nf_add_part(ctx, parts, "literal", " ");
                p++;
            } else if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xA0) {
                nf_add_part(ctx, parts, "literal", "\xC2\xA0");
                p += 2;
            } else {
                break;
            }
        }
        if (*p) {
            nf_add_part(ctx, parts, "compact", p);
        }
    }

    JS_FreeCString(ctx, num_cstr);
    JS_FreeValue(ctx, num_str);
    intl_mv_free(ctx, &scaled_mv);
    return ret;
}

static JSValue intl_mv_format_unit(JSContext *ctx, IntlMV *mv, JSNumberFormatData *s,
                                    const LocaleData *ldata, JSValue parts)
{
    const UnitPatternEntry *entry = find_unit_pattern(s->locale, s->unit);
    const char *unit_pattern;
    const char *num_pos;
    const char *prefix;
    int prefix_len;
    const char *suffix;
    JSValue num_str = JS_UNDEFINED;
    DynBuf db;
    JSValue ret = JS_UNDEFINED;
    int has_parts = !JS_IsUndefined(parts);

    if (entry)
        unit_pattern = get_unit_pattern(entry, s->unitDisplay, unit_value_is_one(mv));
    else
        unit_pattern = NULL;

    if (!unit_pattern) {
        /* Fallback: decimal + " " + unit identifier */
        JSNumberFormatData s2 = *s;
        s2.style = NF_STYLE_DECIMAL;
        num_str = intl_mv_format_standard(ctx, mv, &s2, ldata, 1, parts);
        if (JS_IsException(num_str))
            return num_str;

        js_intl_dbuf_init(ctx, &db);
        {
            const char *str = JS_ToCString(ctx, num_str);
            if (str) {
                dbuf_putstr(&db, str);
                dbuf_putstr(&db, " ");
                dbuf_putstr(&db, s->unit);
                JS_FreeCString(ctx, str);
            }
        }
        dbuf_putc(&db, '\0');
        if (dbuf_error(&db)) {
            dbuf_free(&db);
            JS_FreeValue(ctx, num_str);
            return JS_EXCEPTION;
        }
        ret = JS_NewString(ctx, (char *)db.buf);
        dbuf_free(&db);
        JS_FreeValue(ctx, num_str);

        if (has_parts) {
            nf_add_part(ctx, parts, "literal", " ");
            nf_add_part(ctx, parts, "unit", s->unit);
        }
        return ret;
    }

    num_pos = strstr(unit_pattern, "{number}");
    if (!num_pos) {
        JSNumberFormatData s2 = *s;
        s2.style = NF_STYLE_DECIMAL;
        return intl_mv_format_standard(ctx, mv, &s2, ldata, 1, parts);
    }

    prefix = unit_pattern;
    prefix_len = (int)(num_pos - unit_pattern);
    suffix = num_pos + strlen("{number}");

    js_intl_dbuf_init(ctx, &db);

    /* Prefix */
    if (prefix_len > 0)
        dbuf_put(&db, (const uint8_t *)prefix, (size_t)prefix_len);

    if (has_parts && prefix_len > 0) {
        const char *prefix_unit = prefix + prefix_len;
        while (prefix_unit > prefix && prefix_unit[-1] == ' ') prefix_unit--;
        int prefix_space_len = (int)(prefix_len - (prefix_unit - prefix));
        if (prefix_unit > prefix) {
            char *unit_str = js_strndup(ctx, prefix, (size_t)(prefix_unit - prefix));
            if (unit_str) {
                nf_add_part(ctx, parts, "unit", unit_str);
                js_free(ctx, unit_str);
            }
        }
        if (prefix_space_len > 0) {
            char *space_str = js_strndup(ctx, prefix_unit, (size_t)prefix_space_len);
            if (space_str) {
                nf_add_part(ctx, parts, "literal", space_str);
                js_free(ctx, space_str);
            }
        }
    }

    /* Number (formatted as decimal) */
    {
        JSNumberFormatData s2 = *s;
        s2.style = NF_STYLE_DECIMAL;
        num_str = intl_mv_format_standard(ctx, mv, &s2, ldata, 1, has_parts ? parts : JS_UNDEFINED);
        if (JS_IsException(num_str)) {
            dbuf_free(&db);
            return num_str;
        }
        {
            const char *str = JS_ToCString(ctx, num_str);
            if (str) {
                dbuf_putstr(&db, str);
                JS_FreeCString(ctx, str);
            }
        }
        JS_FreeValue(ctx, num_str);
    }

    /* Suffix */
    if (*suffix)
        dbuf_putstr(&db, suffix);
    dbuf_putc(&db, '\0');
    if (dbuf_error(&db)) {
        dbuf_free(&db);
        return JS_EXCEPTION;
    }
    ret = JS_NewString(ctx, (char *)db.buf);
    dbuf_free(&db);

    if (has_parts && *suffix) {
        const char *unit_text = suffix;
        while (*unit_text == ' ') unit_text++;
        int space_len = (int)(unit_text - suffix);
        if (space_len > 0) {
            char *space_str = js_strndup(ctx, suffix, (size_t)space_len);
            if (space_str) {
                nf_add_part(ctx, parts, "literal", space_str);
                js_free(ctx, space_str);
            }
        }
        if (*unit_text) {
            nf_add_part(ctx, parts, "unit", unit_text);
        }
    }

    return ret;
}

/* Internal format helper: returns JSValue string.
   If out_parts is non-NULL, also fills the parts array. */
static JSValue js_numberformat_format_internal2(JSContext *ctx, JSNumberFormatData *s,
                                                 int argc, JSValueConst *argv,
                                                 JSValue *out_parts)
{
    const LocaleData *ldata;
    IntlMV mv = {0};
    JSValue ret;
    DynBuf db;
    const char *sign_str = "";
    int is_negative, is_zero;
    int strip_trailing_zeros = 1;

    if (!s)
        return JS_EXCEPTION;

    ldata = js_intl_find_locale_data(s->locale);

    if (argc == 0) {
        mv.kind = INTL_MV_NAN;
    } else {
        if (intl_mv_init_from_value(ctx, argv[0], &mv))
            return JS_EXCEPTION;
    }

    JSValue parts = JS_UNDEFINED;
    if (out_parts) {
        parts = *out_parts;
    }

    /* Handle NaN and Infinity */
    if (mv.kind == INTL_MV_NAN || mv.kind == INTL_MV_INFINITY) {
        js_intl_dbuf_init(ctx, &db);
        is_negative = mv.sign;
        if (s->signDisplay == NF_SIGNDISPLAY_ALWAYS) {
            sign_str = is_negative ? NS_MINUS(ldata->symbols) : NS_PLUS(ldata->symbols);
        } else if (s->signDisplay == NF_SIGNDISPLAY_EXCEPTZERO) {
            if (mv.kind != INTL_MV_NAN)
                sign_str = is_negative ? NS_MINUS(ldata->symbols) : NS_PLUS(ldata->symbols);
        } else if (s->signDisplay == NF_SIGNDISPLAY_NEGATIVE) {
            if (is_negative)
                sign_str = NS_MINUS(ldata->symbols);
        } else if (s->signDisplay == NF_SIGNDISPLAY_NEVER) {
            sign_str = "";
        } else {
            if (is_negative)
                sign_str = NS_MINUS(ldata->symbols);
        }
        if (sign_str[0]) {
            dbuf_putstr(&db, sign_str);
            if (!JS_IsUndefined(parts))
                nf_add_part(ctx, parts, is_negative ? "minusSign" : "plusSign", sign_str);
        }
        {
            const char *sym = (mv.kind == INTL_MV_NAN) ?
                (NS_NAN(ldata->symbols) ? NS_NAN(ldata->symbols) : "NaN") :
                (NS_INFINITY(ldata->symbols) ? NS_INFINITY(ldata->symbols) : "∞");
            dbuf_putstr(&db, sym);
            if (!JS_IsUndefined(parts))
                nf_add_part(ctx, parts, mv.kind == INTL_MV_NAN ? "nan" : "infinity", sym);
        }
        dbuf_putc(&db, '\0');
        ret = nf_finish_with_digits(ctx, &db, s->numberingSystem);
        intl_mv_free(ctx, &mv);
        return ret;
    }

    is_negative = mv.sign;
    is_zero = (mv.num_digits == 1 && mv.digits[0] == '0');

    /* For percent style, multiply by 100 via exponent adjustment */
    if (s->style == NF_STYLE_PERCENT && !is_zero) {
        mv.exponent += 2;
    }

    /* Apply rounding based on roundingPriority and significant digits options */
    if (s->roundingPriority == NF_ROUNDINGPRIORITY_AUTO &&
        (s->hasMinimumSignificantDigits || s->hasMaximumSignificantDigits)) {
        /* auto with significant digits: ignore fraction digits */
        strip_trailing_zeros = 0;
        if (intl_mv_round_significant(ctx, &mv, s->maximumSignificantDigits,
                                       s->minimumSignificantDigits,
                                       s->roundingMode)) {
            intl_mv_free(ctx, &mv);
            return JS_EXCEPTION;
        }
    } else if (s->roundingPriority == NF_ROUNDINGPRIORITY_MOREPRECISION ||
               s->roundingPriority == NF_ROUNDINGPRIORITY_LESSPRECISION) {
        /* Compute both and pick based on RoundingMagnitude */
        IntlMV mv_sig = {0}, mv_frac = {0};
        int rm_sig, rm_frac;
        int fixed_is_more_precise;
        int use_frac;

        /* Copy mv for significant-digit rounding */
        mv_sig.kind = mv.kind;
        mv_sig.sign = mv.sign;
        mv_sig.digits = js_strdup(ctx, mv.digits);
        if (!mv_sig.digits) goto precision_fail;
        mv_sig.num_digits = mv.num_digits;
        mv_sig.exponent = mv.exponent;

        if (intl_mv_round_significant(ctx, &mv_sig, s->maximumSignificantDigits,
                                       s->minimumSignificantDigits,
                                       s->roundingMode)) {
            intl_mv_free(ctx, &mv_sig);
            goto precision_fail;
        }

        /* Copy mv for fraction-digit rounding */
        mv_frac.kind = mv.kind;
        mv_frac.sign = mv.sign;
        mv_frac.digits = js_strdup(ctx, mv.digits);
        if (!mv_frac.digits) {
            intl_mv_free(ctx, &mv_sig);
            goto precision_fail;
        }
        mv_frac.num_digits = mv.num_digits;
        mv_frac.exponent = mv.exponent;

        if (intl_mv_round_fraction(ctx, &mv_frac, s->maximumFractionDigits,
                                    s->minimumFractionDigits, s->roundingIncrement,
                                    s->roundingMode)) {
            intl_mv_free(ctx, &mv_sig);
            intl_mv_free(ctx, &mv_frac);
            goto precision_fail;
        }

        /* RoundingMagnitude for ToRawPrecision: e - p + 1 = exponent + num_digits - maxSD */
        rm_sig = mv_sig.exponent + mv_sig.num_digits - s->maximumSignificantDigits;
        /* RoundingMagnitude for ToRawFixed: -maxFD */
        rm_frac = -s->maximumFractionDigits;

        fixed_is_more_precise = (rm_frac < rm_sig);

        if (s->roundingPriority == NF_ROUNDINGPRIORITY_MOREPRECISION)
            use_frac = fixed_is_more_precise;
        else
            use_frac = !fixed_is_more_precise;

        if (use_frac) {
            intl_mv_free(ctx, &mv_sig);
            intl_mv_free(ctx, &mv);
            mv = mv_frac;
            strip_trailing_zeros = 1;
        } else {
            intl_mv_free(ctx, &mv_frac);
            intl_mv_free(ctx, &mv);
            mv = mv_sig;
            strip_trailing_zeros = 0;
        }
        goto precision_done;
    precision_fail:
        intl_mv_free(ctx, &mv);
        return JS_EXCEPTION;
    precision_done:
        ;
    } else {
        /* fraction digits only */
        if (s->notation != NF_NOTATION_SCIENTIFIC && s->notation != NF_NOTATION_ENGINEERING) {
            if (intl_mv_round_fraction(ctx, &mv, s->maximumFractionDigits,
                                        s->minimumFractionDigits, s->roundingIncrement,
                                        s->roundingMode)) {
                intl_mv_free(ctx, &mv);
                return JS_EXCEPTION;
            }
        }
    }

    /* Re-check zero after rounding */
    is_zero = (mv.num_digits == 1 && mv.digits[0] == '0');

    /* For scientific/engineering notation, round mantissa to desired significant digits */
    if (s->notation == NF_NOTATION_SCIENTIFIC || s->notation == NF_NOTATION_ENGINEERING) {
        int total_exp = (int)(mv.exponent + mv.num_digits - 1);
        int eng_exp;
        int digits_before;
        int desired_sig_digits;

        if (s->notation == NF_NOTATION_ENGINEERING) {
            if (total_exp >= 0) {
                eng_exp = (total_exp / 3) * 3;
            } else {
                eng_exp = ((total_exp - 2) / 3) * 3;
            }
            digits_before = total_exp - eng_exp + 1;
        } else {
            digits_before = 1;
        }

        if (s->hasMaximumSignificantDigits || s->hasMinimumSignificantDigits) {
            desired_sig_digits = s->maximumSignificantDigits;
        } else {
            desired_sig_digits = digits_before + s->maximumFractionDigits;
        }

        if (desired_sig_digits > 0 && desired_sig_digits < 100) {
            if (intl_mv_round_significant(ctx, &mv, desired_sig_digits,
                                           s->minimumSignificantDigits,
                                           s->roundingMode)) {
                intl_mv_free(ctx, &mv);
                return JS_EXCEPTION;
            }
        }
        /* Re-check zero after re-rounding */
        is_zero = (mv.num_digits == 1 && mv.digits[0] == '0');
        ret = intl_mv_format_scientific(ctx, &mv, s, ldata,
                                         s->notation == NF_NOTATION_ENGINEERING,
                                         parts);
        intl_mv_free(ctx, &mv);
        return ret;
    }

    if (s->notation == NF_NOTATION_COMPACT) {
        ret = intl_mv_format_compact(ctx, &mv, s, ldata, parts);
        intl_mv_free(ctx, &mv);
        return ret;
    }

    if (s->style == NF_STYLE_UNIT && s->unit) {
        ret = intl_mv_format_unit(ctx, &mv, s, ldata, parts);
        intl_mv_free(ctx, &mv);
        return ret;
    }

    /* Format using intl_mv_format_standard */
    ret = intl_mv_format_standard(ctx, &mv, s, ldata, strip_trailing_zeros, parts);
    intl_mv_free(ctx, &mv);
    return ret;
}

static JSValue js_numberformat_format_internal(JSContext *ctx, JSNumberFormatData *s,
                                                int argc, JSValueConst *argv)
{
    return js_numberformat_format_internal2(ctx, s, argc, argv, NULL);
}

/* Number format function - called via JS_NewCFunctionData */
static JSValue js_numberformat_bound_format(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv,
                                             int magic, JSValue *func_data)
{
    (void)this_val;
    (void)magic;
    JSNumberFormatData *s = JS_GetOpaque2(ctx, func_data[0], js_numberformat_class_id);
    if (!s)
        return JS_EXCEPTION;
    return js_numberformat_format_internal(ctx, s, argc, argv);
}

/* getter for Intl.NumberFormat.prototype.format */
static JSValue js_numberformat_get_format(JSContext *ctx, JSValueConst this_val)
{
    JSValue bound;
    JSNumberFormatData *s;
    JSValue unwrapped = js_numberformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    s = JS_GetOpaque2(ctx, unwrapped, js_numberformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    /* Check cached bound function */
    bound = JS_GetPropertyStr(ctx, unwrapped, "__boundFormat__");
    if (!JS_IsUndefined(bound) && JS_IsFunction(ctx, bound)) {
        JS_FreeValue(ctx, unwrapped);
        return bound;
    }
    JS_FreeValue(ctx, bound);

    /* Create bound function using func_data to store the instance */
    {
        JSValue data[1];
        JSValue ret;
        data[0] = unwrapped;
        bound = JS_NewCFunctionData(ctx, js_numberformat_bound_format, 1, 0, 1, data);
        if (JS_IsException(bound)) {
            JS_FreeValue(ctx, unwrapped);
            return bound;
        }
        /* Cache it */
        JS_SetPropertyStr(ctx, unwrapped, "__boundFormat__",
                          JS_DupValue(ctx, bound));
        ret = JS_DupValue(ctx, bound);
        JS_FreeValue(ctx, bound);
        JS_FreeValue(ctx, unwrapped);
        return ret;
    }
}

/* prototype method: formatToParts */
static JSValue js_numberformat_formatToParts(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    JSValue parts, str;
    JSNumberFormatData *s;
    JSValue unwrapped = js_numberformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    s = JS_GetOpaque2(ctx, unwrapped, js_numberformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    parts = JS_NewArray(ctx);
    if (JS_IsException(parts)) {
        JS_FreeValue(ctx, unwrapped);
        return parts;
    }

    str = js_numberformat_format_internal2(ctx, s, argc, argv, &parts);
    if (JS_IsException(str)) {
        JS_FreeValue(ctx, parts);
        JS_FreeValue(ctx, unwrapped);
        return str;
    }
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, unwrapped);
    return parts;
}

/* prototype method: formatRange */
static int js_numberformat_formatRange_validate(JSContext *ctx, int argc, JSValueConst *argv)
{
    int i;
    IntlMV mv;

    if (JS_IsUndefined(argv[0]) || JS_IsUndefined(argv[1]))
        return JS_ThrowTypeError(ctx, "start and end must be defined"), -1;

    for (i = 0; i < 2; i++) {
        JSValue prim = JS_ToPrimitive(ctx, argv[i], JS_HINT_NUMBER);
        if (JS_IsException(prim))
            return -1;
        if (JS_IsSymbol(prim)) {
            JS_FreeValue(ctx, prim);
            return JS_ThrowTypeError(ctx, "cannot convert symbol to number"), -1;
        }
        JS_FreeValue(ctx, prim);
    }

    /* Check for NaN per PartitionNumberRangePattern */
    for (i = 0; i < 2; i++) {
        if (intl_mv_init_from_value(ctx, argv[i], &mv))
            return -1;
        if (mv.kind == INTL_MV_NAN) {
            intl_mv_free(ctx, &mv);
            return JS_ThrowRangeError(ctx, "cannot format range with NaN"), -1;
        }
        intl_mv_free(ctx, &mv);
    }

    return 0;
}

static JSValue js_numberformat_formatRangeToParts(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv);

static JSValue js_numberformat_formatRange(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSNumberFormatData *s;
    JSValue unwrapped = js_numberformat_unwrap(ctx, this_val);
    JSValue start_parts = JS_UNDEFINED, end_parts = JS_UNDEFINED;
    JSValue start_str_val = JS_UNDEFINED, end_str_val = JS_UNDEFINED;
    DynBuf db;
    int start_len = 0, end_len = 0;
    int i;
    int prefix_len = 0, suffix_len = 0;
    int share_prefix = 0, share_suffix = 0;
    const char *start_cstr = NULL, *end_cstr = NULL;
    const char *sep;

    if (JS_IsException(unwrapped))
        return unwrapped;
    s = JS_GetOpaque2(ctx, unwrapped, js_numberformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    if (js_numberformat_formatRange_validate(ctx, argc, argv) < 0) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    start_parts = JS_NewArray(ctx);
    if (JS_IsException(start_parts))
        goto fail_range;
    end_parts = JS_NewArray(ctx);
    if (JS_IsException(end_parts))
        goto fail_range;

    start_str_val = js_numberformat_format_internal2(ctx, s, 1, &argv[0], &start_parts);
    if (JS_IsException(start_str_val))
        goto fail_range;
    end_str_val = js_numberformat_format_internal2(ctx, s, 1, &argv[1], &end_parts);
    if (JS_IsException(end_str_val))
        goto fail_range;

    start_cstr = JS_ToCString(ctx, start_str_val);
    end_cstr = JS_ToCString(ctx, end_str_val);
    if (!start_cstr || !end_cstr)
        goto fail_range;

    if (strcmp(start_cstr, end_cstr) == 0) {
        js_intl_dbuf_init(ctx, &db);
        dbuf_putstr(&db, "~");
        dbuf_putstr(&db, start_cstr);
        dbuf_putc(&db, '\0');
        if (dbuf_error(&db))
            goto fail_range;
        {
            JSValue ret = JS_NewString(ctx, (char *)db.buf);
            dbuf_free(&db);
            JS_FreeCString(ctx, start_cstr);
            JS_FreeCString(ctx, end_cstr);
            JS_FreeValue(ctx, start_str_val);
            JS_FreeValue(ctx, end_str_val);
            JS_FreeValue(ctx, start_parts);
            JS_FreeValue(ctx, end_parts);
            JS_FreeValue(ctx, unwrapped);
            return ret;
        }
    }

    {
        JSValue len_val = JS_GetPropertyStr(ctx, start_parts, "length");
        if (!JS_IsException(len_val)) {
            int64_t v;
            if (JS_ToInt64(ctx, &v, len_val) == 0)
                start_len = (int)v;
            JS_FreeValue(ctx, len_val);
        }
    }
    {
        JSValue len_val = JS_GetPropertyStr(ctx, end_parts, "length");
        if (!JS_IsException(len_val)) {
            int64_t v;
            if (JS_ToInt64(ctx, &v, len_val) == 0)
                end_len = (int)v;
            JS_FreeValue(ctx, len_val);
        }
    }

    for (i = 0; i < start_len && i < end_len; i++) {
        JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
        JSValue ep = JS_GetPropertyUint32(ctx, end_parts, (uint32_t)i);
        int eq = 0;
        if (!JS_IsException(sp) && !JS_IsException(ep)) {
            JSValue st = JS_GetPropertyStr(ctx, sp, "type");
            JSValue et = JS_GetPropertyStr(ctx, ep, "type");
            JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
            JSValue ev = JS_GetPropertyStr(ctx, ep, "value");
            if (!JS_IsException(st) && !JS_IsException(et) &&
                !JS_IsException(sv) && !JS_IsException(ev)) {
                const char *sct = JS_ToCString(ctx, st);
                const char *ect = JS_ToCString(ctx, et);
                const char *scv = JS_ToCString(ctx, sv);
                const char *ecv = JS_ToCString(ctx, ev);
                if (sct && ect && scv && ecv) {
                    if (!strcmp(sct, ect) && !strcmp(scv, ecv))
                        eq = 1;
                }
                JS_FreeCString(ctx, sct);
                JS_FreeCString(ctx, ect);
                JS_FreeCString(ctx, scv);
                JS_FreeCString(ctx, ecv);
            }
            JS_FreeValue(ctx, st);
            JS_FreeValue(ctx, et);
            JS_FreeValue(ctx, sv);
            JS_FreeValue(ctx, ev);
        }
        JS_FreeValue(ctx, sp);
        JS_FreeValue(ctx, ep);
        if (!eq)
            break;
        prefix_len++;
    }

    for (i = 0; i < start_len - prefix_len && i < end_len - prefix_len; i++) {
        JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)(start_len - 1 - i));
        JSValue ep = JS_GetPropertyUint32(ctx, end_parts, (uint32_t)(end_len - 1 - i));
        int eq = 0;
        if (!JS_IsException(sp) && !JS_IsException(ep)) {
            JSValue st = JS_GetPropertyStr(ctx, sp, "type");
            JSValue et = JS_GetPropertyStr(ctx, ep, "type");
            JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
            JSValue ev = JS_GetPropertyStr(ctx, ep, "value");
            if (!JS_IsException(st) && !JS_IsException(et) &&
                !JS_IsException(sv) && !JS_IsException(ev)) {
                const char *sct = JS_ToCString(ctx, st);
                const char *ect = JS_ToCString(ctx, et);
                const char *scv = JS_ToCString(ctx, sv);
                const char *ecv = JS_ToCString(ctx, ev);
                if (sct && ect && scv && ecv) {
                    if (!strcmp(sct, ect) && !strcmp(scv, ecv))
                        eq = 1;
                }
                JS_FreeCString(ctx, sct);
                JS_FreeCString(ctx, ect);
                JS_FreeCString(ctx, scv);
                JS_FreeCString(ctx, ecv);
            }
            JS_FreeValue(ctx, st);
            JS_FreeValue(ctx, et);
            JS_FreeValue(ctx, sv);
            JS_FreeValue(ctx, ev);
        }
        JS_FreeValue(ctx, sp);
        JS_FreeValue(ctx, ep);
        if (!eq)
            break;
        suffix_len++;
    }

    for (i = 0; i < prefix_len; i++) {
        JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
        if (!JS_IsException(sp)) {
            JSValue st = JS_GetPropertyStr(ctx, sp, "type");
            if (!JS_IsException(st)) {
                const char *sct = JS_ToCString(ctx, st);
                if (sct) {
                    if (!strcmp(sct, "plusSign") || !strcmp(sct, "minusSign"))
                        share_prefix = 1;
                }
                JS_FreeCString(ctx, sct);
            }
            JS_FreeValue(ctx, st);
        }
        JS_FreeValue(ctx, sp);
    }

    for (i = 0; i < suffix_len; i++) {
        JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)(start_len - suffix_len + i));
        if (!JS_IsException(sp)) {
            JSValue st = JS_GetPropertyStr(ctx, sp, "type");
            if (!JS_IsException(st)) {
                const char *sct = JS_ToCString(ctx, st);
                if (sct) {
                    if (!strcmp(sct, "currency") || !strcmp(sct, "percentSign") || !strcmp(sct, "unit"))
                        share_suffix = 1;
                }
                JS_FreeCString(ctx, sct);
            }
            JS_FreeValue(ctx, st);
        }
        JS_FreeValue(ctx, sp);
    }

    if (strstart(s->locale, "pt", NULL) || strstart(s->locale, "pt-", NULL)) {
        sep = " - ";
    } else {
        if (share_prefix || share_suffix) {
            sep = "\u2013";
        } else {
            if (s->style == NF_STYLE_CURRENCY || s->style == NF_STYLE_UNIT || s->style == NF_STYLE_PERCENT) {
                sep = " \u2013 ";
            } else {
                sep = "\u2013";
            }
        }
    }

    js_intl_dbuf_init(ctx, &db);

    for (i = 0; i < prefix_len; i++) {
        if (share_prefix) {
            JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
            if (!JS_IsException(sp)) {
                JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
                if (!JS_IsException(sv)) {
                    const char *scv = JS_ToCString(ctx, sv);
                    if (scv)
                        dbuf_putstr(&db, scv);
                    JS_FreeCString(ctx, scv);
                }
                JS_FreeValue(ctx, sv);
            }
            JS_FreeValue(ctx, sp);
        }
    }

    for (i = share_prefix ? prefix_len : 0; i < start_len - (share_suffix ? suffix_len : 0); i++) {
        JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
        if (!JS_IsException(sp)) {
            JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
            if (!JS_IsException(sv)) {
                const char *scv = JS_ToCString(ctx, sv);
                if (scv)
                    dbuf_putstr(&db, scv);
                JS_FreeCString(ctx, scv);
            }
            JS_FreeValue(ctx, sv);
        }
        JS_FreeValue(ctx, sp);
    }

    dbuf_putstr(&db, sep);

    for (i = share_prefix ? prefix_len : 0; i < end_len - (share_suffix ? suffix_len : 0); i++) {
        JSValue ep = JS_GetPropertyUint32(ctx, end_parts, (uint32_t)i);
        if (!JS_IsException(ep)) {
            JSValue ev = JS_GetPropertyStr(ctx, ep, "value");
            if (!JS_IsException(ev)) {
                const char *ecv = JS_ToCString(ctx, ev);
                if (ecv)
                    dbuf_putstr(&db, ecv);
                JS_FreeCString(ctx, ecv);
            }
            JS_FreeValue(ctx, ev);
        }
        JS_FreeValue(ctx, ep);
    }

    for (i = 0; i < suffix_len; i++) {
        if (share_suffix) {
            JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)(start_len - suffix_len + i));
            if (!JS_IsException(sp)) {
                JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
                if (!JS_IsException(sv)) {
                    const char *scv = JS_ToCString(ctx, sv);
                    if (scv)
                        dbuf_putstr(&db, scv);
                    JS_FreeCString(ctx, scv);
                }
                JS_FreeValue(ctx, sv);
            }
            JS_FreeValue(ctx, sp);
        }
    }

    dbuf_putc(&db, '\0');
    if (dbuf_error(&db)) {
        dbuf_free(&db);
        goto fail_range;
    }
    {
        JSValue ret = JS_NewString(ctx, (char *)db.buf);
        dbuf_free(&db);
        JS_FreeCString(ctx, start_cstr);
        JS_FreeCString(ctx, end_cstr);
        JS_FreeValue(ctx, start_str_val);
        JS_FreeValue(ctx, end_str_val);
        JS_FreeValue(ctx, start_parts);
        JS_FreeValue(ctx, end_parts);
        JS_FreeValue(ctx, unwrapped);
        return ret;
    }

fail_range:
    JS_FreeCString(ctx, start_cstr);
    JS_FreeCString(ctx, end_cstr);
    JS_FreeValue(ctx, start_str_val);
    JS_FreeValue(ctx, end_str_val);
    JS_FreeValue(ctx, start_parts);
    JS_FreeValue(ctx, end_parts);
    JS_FreeValue(ctx, unwrapped);
    return JS_EXCEPTION;
}

/* prototype method: formatRangeToParts */
static JSValue js_numberformat_formatRangeToParts(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    JSNumberFormatData *s;
    JSValue unwrapped = js_numberformat_unwrap(ctx, this_val);
    JSValue start_parts = JS_UNDEFINED, end_parts = JS_UNDEFINED, result = JS_EXCEPTION;
    int start_len = 0, end_len = 0;
    int i;
    int all_equal = 1;

    if (JS_IsException(unwrapped))
        return unwrapped;
    s = JS_GetOpaque2(ctx, unwrapped, js_numberformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    if (js_numberformat_formatRange_validate(ctx, argc, argv) < 0) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    start_parts = JS_NewArray(ctx);
    if (JS_IsException(start_parts))
        goto fail_parts;
    end_parts = JS_NewArray(ctx);
    if (JS_IsException(end_parts))
        goto fail_parts;

    {
        JSValue str1 = js_numberformat_format_internal2(ctx, s, 1, &argv[0], &start_parts);
        if (JS_IsException(str1))
            goto fail_parts;
        JS_FreeValue(ctx, str1);
    }
    {
        JSValue str2 = js_numberformat_format_internal2(ctx, s, 1, &argv[1], &end_parts);
        if (JS_IsException(str2))
            goto fail_parts;
        JS_FreeValue(ctx, str2);
    }

    {
        JSValue len_val = JS_GetPropertyStr(ctx, start_parts, "length");
        if (!JS_IsException(len_val)) {
            int64_t v;
            if (JS_ToInt64(ctx, &v, len_val) == 0)
                start_len = (int)v;
            JS_FreeValue(ctx, len_val);
        }
    }
    {
        JSValue len_val = JS_GetPropertyStr(ctx, end_parts, "length");
        if (!JS_IsException(len_val)) {
            int64_t v;
            if (JS_ToInt64(ctx, &v, len_val) == 0)
                end_len = (int)v;
            JS_FreeValue(ctx, len_val);
        }
    }

    if (start_len != end_len)
        all_equal = 0;
    else {
        for (i = 0; i < start_len; i++) {
            JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
            JSValue ep = JS_GetPropertyUint32(ctx, end_parts, (uint32_t)i);
            int eq = 0;
            if (!JS_IsException(sp) && !JS_IsException(ep)) {
                JSValue st = JS_GetPropertyStr(ctx, sp, "type");
                JSValue et = JS_GetPropertyStr(ctx, ep, "type");
                JSValue sv = JS_GetPropertyStr(ctx, sp, "value");
                JSValue ev = JS_GetPropertyStr(ctx, ep, "value");
                if (!JS_IsException(st) && !JS_IsException(et) &&
                    !JS_IsException(sv) && !JS_IsException(ev)) {
                    const char *sct = JS_ToCString(ctx, st);
                    const char *ect = JS_ToCString(ctx, et);
                    const char *scv = JS_ToCString(ctx, sv);
                    const char *ecv = JS_ToCString(ctx, ev);
                    if (sct && ect && scv && ecv) {
                        if (!strcmp(sct, ect) && !strcmp(scv, ecv))
                            eq = 1;
                    }
                    JS_FreeCString(ctx, sct);
                    JS_FreeCString(ctx, ect);
                    JS_FreeCString(ctx, scv);
                    JS_FreeCString(ctx, ecv);
                }
                JS_FreeValue(ctx, st);
                JS_FreeValue(ctx, et);
                JS_FreeValue(ctx, sv);
                JS_FreeValue(ctx, ev);
            }
            JS_FreeValue(ctx, sp);
            JS_FreeValue(ctx, ep);
            if (!eq) {
                all_equal = 0;
                break;
            }
        }
    }

    result = JS_NewArray(ctx);
    if (JS_IsException(result))
        goto fail_parts;

    if (all_equal && start_len > 0) {
        JSValue part = JS_NewObject(ctx);
        if (JS_IsException(part))
            goto fail_parts;
        JS_DefinePropertyValueStr(ctx, part, "type", JS_NewString(ctx, "approximatelySign"), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, part, "value", JS_NewString(ctx, "~"), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, part, "source", JS_NewString(ctx, "shared"), JS_PROP_C_W_E);
        JS_DefinePropertyValueUint32(ctx, result, (uint32_t)0, part, JS_PROP_C_W_E);

        for (i = 0; i < start_len; i++) {
            JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
            JSValue type_val = JS_GetPropertyStr(ctx, sp, "type");
            JSValue value_val = JS_GetPropertyStr(ctx, sp, "value");
            JSValue part2 = JS_NewObject(ctx);
            if (JS_IsException(part2)) {
                JS_FreeValue(ctx, type_val);
                JS_FreeValue(ctx, value_val);
                JS_FreeValue(ctx, sp);
                goto fail_parts;
            }
            JS_DefinePropertyValueStr(ctx, part2, "type", type_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "value", value_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "source", JS_NewString(ctx, "shared"), JS_PROP_C_W_E);
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)(i + 1), part2, JS_PROP_C_W_E);
            JS_FreeValue(ctx, sp);
        }
    } else {
        for (i = 0; i < start_len; i++) {
            JSValue sp = JS_GetPropertyUint32(ctx, start_parts, (uint32_t)i);
            JSValue type_val = JS_GetPropertyStr(ctx, sp, "type");
            JSValue value_val = JS_GetPropertyStr(ctx, sp, "value");
            JSValue part2 = JS_NewObject(ctx);
            if (JS_IsException(part2)) {
                JS_FreeValue(ctx, type_val);
                JS_FreeValue(ctx, value_val);
                JS_FreeValue(ctx, sp);
                goto fail_parts;
            }
            JS_DefinePropertyValueStr(ctx, part2, "type", type_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "value", value_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "source", JS_NewString(ctx, "startRange"), JS_PROP_C_W_E);
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)i, part2, JS_PROP_C_W_E);
            JS_FreeValue(ctx, sp);
        }

        {
            JSValue sep_part = JS_NewObject(ctx);
            const char *sep = (strstart(s->locale, "pt", NULL) || strstart(s->locale, "pt-", NULL)) ? " - " : " \u2013 ";
            if (JS_IsException(sep_part))
                goto fail_parts;
            JS_DefinePropertyValueStr(ctx, sep_part, "type", JS_NewString(ctx, "literal"), JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, sep_part, "value", JS_NewString(ctx, sep), JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, sep_part, "source", JS_NewString(ctx, "shared"), JS_PROP_C_W_E);
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)start_len, sep_part, JS_PROP_C_W_E);
        }

        for (i = 0; i < end_len; i++) {
            JSValue ep = JS_GetPropertyUint32(ctx, end_parts, (uint32_t)i);
            JSValue type_val = JS_GetPropertyStr(ctx, ep, "type");
            JSValue value_val = JS_GetPropertyStr(ctx, ep, "value");
            JSValue part2 = JS_NewObject(ctx);
            if (JS_IsException(part2)) {
                JS_FreeValue(ctx, type_val);
                JS_FreeValue(ctx, value_val);
                JS_FreeValue(ctx, ep);
                goto fail_parts;
            }
            JS_DefinePropertyValueStr(ctx, part2, "type", type_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "value", value_val, JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, part2, "source", JS_NewString(ctx, "endRange"), JS_PROP_C_W_E);
            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)(start_len + 1 + i), part2, JS_PROP_C_W_E);
            JS_FreeValue(ctx, ep);
        }
    }

    JS_FreeValue(ctx, start_parts);
    JS_FreeValue(ctx, end_parts);
    JS_FreeValue(ctx, unwrapped);
    return result;

fail_parts:
    JS_FreeValue(ctx, start_parts);
    JS_FreeValue(ctx, end_parts);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, unwrapped);
    return JS_EXCEPTION;
}

/* prototype method: resolvedOptions */
static JSValue js_numberformat_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSValue obj;
    JSNumberFormatData *s;
    JSValue unwrapped = js_numberformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    s = JS_GetOpaque2(ctx, unwrapped, js_numberformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "numberingSystem",
                               JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "style",
                               JS_NewString(ctx, nf_style_names[s->style]), JS_PROP_C_W_E);

    if (s->style == NF_STYLE_CURRENCY && s->currency) {
        JS_DefinePropertyValueStr(ctx, obj, "currency",
                                   JS_NewString(ctx, s->currency), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "currencyDisplay",
                                   JS_NewString(ctx, nf_currencydisplay_names[s->currencyDisplay]), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "currencySign",
                                   JS_NewString(ctx, nf_currencysign_names[s->currencySign]), JS_PROP_C_W_E);
    }
    if (s->style == NF_STYLE_UNIT && s->unit) {
        JS_DefinePropertyValueStr(ctx, obj, "unit",
                                   JS_NewString(ctx, s->unit), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "unitDisplay",
                                   JS_NewString(ctx, nf_unitdisplay_names[s->unitDisplay]), JS_PROP_C_W_E);
    }

    JS_DefinePropertyValueStr(ctx, obj, "minimumIntegerDigits",
                               JS_NewInt32(ctx, s->minimumIntegerDigits), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "minimumFractionDigits",
                               JS_NewInt32(ctx, s->minimumFractionDigits), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "maximumFractionDigits",
                               JS_NewInt32(ctx, s->maximumFractionDigits), JS_PROP_C_W_E);

    if (s->hasMinimumSignificantDigits || s->hasMaximumSignificantDigits) {
        JS_DefinePropertyValueStr(ctx, obj, "minimumSignificantDigits",
                                   JS_NewInt32(ctx, s->minimumSignificantDigits), JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "maximumSignificantDigits",
                                   JS_NewInt32(ctx, s->maximumSignificantDigits), JS_PROP_C_W_E);
    }

    if (s->useGrouping == NF_GROUPING_FALSE_BOOL) {
        JS_DefinePropertyValueStr(ctx, obj, "useGrouping",
                                   JS_NewBool(ctx, 0), JS_PROP_C_W_E);
    } else {
        JS_DefinePropertyValueStr(ctx, obj, "useGrouping",
                                   JS_NewString(ctx, nf_grouping_names[s->useGrouping]), JS_PROP_C_W_E);
    }
    JS_DefinePropertyValueStr(ctx, obj, "notation",
                               JS_NewString(ctx, nf_notation_names[s->notation]), JS_PROP_C_W_E);

    if (s->notation == NF_NOTATION_COMPACT) {
        JS_DefinePropertyValueStr(ctx, obj, "compactDisplay",
                                   JS_NewString(ctx, nf_compact_names[s->compactDisplay]), JS_PROP_C_W_E);
    }

    JS_DefinePropertyValueStr(ctx, obj, "signDisplay",
                               JS_NewString(ctx, nf_signdisplay_names[s->signDisplay]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "roundingIncrement",
                               JS_NewInt32(ctx, s->roundingIncrement), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "roundingMode",
                               JS_NewString(ctx, nf_roundingmode_names[s->roundingMode]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "roundingPriority",
                               JS_NewString(ctx, nf_roundingpriority_names[s->roundingPriority]), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "trailingZeroDisplay",
                               JS_NewString(ctx, nf_trailingzero_names[s->trailingZeroDisplay]), JS_PROP_C_W_E);

    JS_FreeValue(ctx, unwrapped);
    return obj;
}

/* static method: supportedLocalesOf */
static JSValue js_numberformat_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_numberformat_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.NumberFormat", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("format", js_numberformat_get_format, NULL),
    JS_CFUNC_DEF("formatToParts", 1, js_numberformat_formatToParts),
    JS_CFUNC_DEF("formatRange", 2, js_numberformat_formatRange),
    JS_CFUNC_DEF("formatRangeToParts", 2, js_numberformat_formatRangeToParts),
    JS_CFUNC_DEF("resolvedOptions", 0, js_numberformat_resolvedOptions),
};

static const JSCFunctionListEntry js_numberformat_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_numberformat_supportedLocalesOf),
};

int js_add_numberformat(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_numberformat_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_numberformat_class_id, &js_numberformat_class);

    JSCFunctionType ft = { .constructor_or_func_flags = js_numberformat_constructor };
    
    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_NUMBERFORMAT,
                                                js_numberformat_class_id, "NumberFormat",
                                                ft.generic, 0,
                                                JS_CFUNC_constructor_or_func_flags, 0,
                                                js_numberformat_static_funcs, countof(js_numberformat_static_funcs),
                                                js_numberformat_proto_funcs, countof(js_numberformat_proto_funcs))))
        return -1;

    return 0;
}

JSValue js_numberformat_format_number(JSContext *ctx, double d, int argc, JSValueConst *argv)
{
    JSValue ctor, obj, val, ret;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "NumberFormat not available");

    obj = JS_CallConstructor(ctx, ctor, argc, argv);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(obj))
        return obj;

    {
        JSNumberFormatData *s = JS_GetOpaque(obj, js_numberformat_class_id);
        if (!s) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        val = __JS_NewFloat64(ctx, d);
        ret = js_numberformat_format_internal(ctx, s, 1, &val);
        JS_FreeValue(ctx, val);
    }
    JS_FreeValue(ctx, obj);
    return ret;
}

JSValue js_intl_number_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    double d;
    if (JS_ToFloat64(ctx, &d, this_val))
        return JS_EXCEPTION;
    return js_numberformat_format_number(ctx, d, argc, argv);
}

JSValue js_intl_bigint_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSValue ctor, obj, func, ret;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "NumberFormat not available");

    obj = JS_CallConstructor(ctx, ctor, argc, argv);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(obj))
        return obj;

    func = JS_GetPropertyStr(ctx, obj, "format");
    if (JS_IsException(func)) {
        JS_FreeValue(ctx, obj);
        return func;
    }

    ret = JS_Call(ctx, func, obj, 1, &this_val);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, obj);
    return ret;
}
