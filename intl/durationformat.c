/*
 * QuickJS Intl.DurationFormat implementation
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "../dtoa.h"
#include "../temporal/temporal_private.h"
#include "intl_private.h"

typedef enum {
    DF_STYLE_LONG = 0,
    DF_STYLE_SHORT,
    DF_STYLE_NARROW,
    DF_STYLE_DIGITAL,
    DF_STYLE_NUMERIC,
    DF_STYLE_2DIGIT,
    DF_STYLE_FRACTIONAL,
    DF_STYLE_COUNT,
} DFStyle;

typedef enum {
    DF_DISPLAY_AUTO = 0,
    DF_DISPLAY_ALWAYS,
    DF_DISPLAY_COUNT,
} DFDisplay;

typedef enum {
    DF_UNIT_YEARS = 0,
    DF_UNIT_MONTHS,
    DF_UNIT_WEEKS,
    DF_UNIT_DAYS,
    DF_UNIT_HOURS,
    DF_UNIT_MINUTES,
    DF_UNIT_SECONDS,
    DF_UNIT_MILLISECONDS,
    DF_UNIT_MICROSECONDS,
    DF_UNIT_NANOSECONDS,
    DF_UNIT_COUNT,
} DFUnit;

typedef struct {
    char *locale;
    char *locale_canonical;
    int numberingSystem;
    int has_numberingSystem_option;
    int style;
    int unit_style[DF_UNIT_COUNT];
    int unit_display[DF_UNIT_COUNT];
    int has_fractionalDigits;
    int fractionalDigits_val;
} JSDurationFormatData;

static JSClassID js_durationformat_class_id;

static void js_durationformat_finalizer(JSRuntime *rt, JSValue val)
{
    JSDurationFormatData *s = JS_GetOpaque(val, js_durationformat_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->locale_canonical);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_durationformat_class = {
    "DurationFormat",
    .finalizer = js_durationformat_finalizer,
};

#define dn_is_alpha(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define dn_is_digit(c) ((c) >= '0' && (c) <= '9')

static const char *df_top_style_names[] = {"long", "short", "narrow", "digital"};
static const char *df_style_names[] = {"long", "short", "narrow", "digital", "numeric", "2-digit", "fractional"};
static const char *df_display_names[] = {"auto", "always"};

typedef struct {
    const char *unit;
    const char *displaySlot;
    const char **styles;
    int num_styles;
    int digitalBase;
    int unit_idx;
} UnitDef;

static const char *df_ysmwd_styles[] = {"long", "short", "narrow"};
static const char *df_hms_styles[] = {"long", "short", "narrow", "numeric", "2-digit"};
static const char *df_mms_styles[] = {"long", "short", "narrow", "numeric"};

static const UnitDef df_units[] = {
    {"years", "yearsDisplay", df_ysmwd_styles, 3, DF_STYLE_SHORT, DF_UNIT_YEARS},
    {"months", "monthsDisplay", df_ysmwd_styles, 3, DF_STYLE_SHORT, DF_UNIT_MONTHS},
    {"weeks", "weeksDisplay", df_ysmwd_styles, 3, DF_STYLE_SHORT, DF_UNIT_WEEKS},
    {"days", "daysDisplay", df_ysmwd_styles, 3, DF_STYLE_SHORT, DF_UNIT_DAYS},
    {"hours", "hoursDisplay", df_hms_styles, 5, DF_STYLE_NUMERIC, DF_UNIT_HOURS},
    {"minutes", "minutesDisplay", df_hms_styles, 5, DF_STYLE_NUMERIC, DF_UNIT_MINUTES},
    {"seconds", "secondsDisplay", df_hms_styles, 5, DF_STYLE_NUMERIC, DF_UNIT_SECONDS},
    {"milliseconds", "millisecondsDisplay", df_mms_styles, 4, DF_STYLE_NUMERIC, DF_UNIT_MILLISECONDS},
    {"microseconds", "microsecondsDisplay", df_mms_styles, 4, DF_STYLE_NUMERIC, DF_UNIT_MICROSECONDS},
    {"nanoseconds", "nanosecondsDisplay", df_mms_styles, 4, DF_STYLE_NUMERIC, DF_UNIT_NANOSECONDS},
};

/* Note: this function checks the Unicode Locale Identifier type nonterminal
   grammar (alphanum{3,8}) per DurationFormat spec step 7. It is intentionally
   different from intl.c's is_valid_numbering_system(), which validates against
   the known list of numbering systems and rejects unknown but syntactically
   valid identifiers. */
static int is_valid_numbering_system(const char *str)
{
    int len = (int)strlen(str);
    int i;
    if (len < 3 || len > 8)
        return 0;
    for (i = 0; i < len; i++) {
        char c = str[i];
        if (!dn_is_alpha(c) && !dn_is_digit(c))
            return 0;
    }
    return 1;
}

static int is_fractional_second_unit(int unit_idx)
{
    return unit_idx >= DF_UNIT_MILLISECONDS;
}

static int df_style_string_to_enum(const char *str)
{
    int i;
    for (i = 0; i < DF_STYLE_COUNT; i++) {
        if (!strcmp(str, df_style_names[i]))
            return i;
    }
    return -1;
}

static int df_display_string_to_enum(const char *str)
{
    if (!strcmp(str, "auto")) return DF_DISPLAY_AUTO;
    if (!strcmp(str, "always")) return DF_DISPLAY_ALWAYS;
    return -1;
}

static int read_duration_unit(JSContext *ctx, JSValueConst options, const UnitDef *def,
                               int baseStyle, int prevStyle,
                               int two_digit_hours,
                               JSDurationFormatData *s)
{
    JSValue val = JS_GetPropertyStr(ctx, options, def->unit);
    const char *style_cstr = NULL;
    const char *display_cstr = NULL;
    int style_enum = -1;
    int display_enum = -1;
    int display_default = DF_DISPLAY_ALWAYS;
    int i, found = 0;
    int ret = -1;

    if (JS_IsException(val))
        goto done;
    if (!JS_IsUndefined(val)) {
        style_cstr = JS_ToCStringNZ(ctx, val);
        JS_FreeValue(ctx, val);
        if (!style_cstr)
            goto done;
        for (i = 0; i < def->num_styles; i++) {
            if (!strcmp(style_cstr, def->styles[i])) {
                found = 1;
                break;
            }
        }
        if (!found) {
            JS_ThrowRangeError(ctx, "invalid %s", def->unit);
            goto done;
        }
        style_enum = df_style_string_to_enum(style_cstr);
    }
    /* Step 2-3: compute style and displayDefault if style is undefined */
    if (style_enum < 0) {
        if (baseStyle >= 0 && baseStyle == DF_STYLE_DIGITAL) {
            style_enum = def->digitalBase;
            if (def->unit_idx != DF_UNIT_HOURS && def->unit_idx != DF_UNIT_MINUTES && def->unit_idx != DF_UNIT_SECONDS)
                display_default = DF_DISPLAY_AUTO;
        } else if (prevStyle >= 0 && (prevStyle == DF_STYLE_FRACTIONAL ||
                                      prevStyle == DF_STYLE_NUMERIC ||
                                      prevStyle == DF_STYLE_2DIGIT)) {
            style_enum = DF_STYLE_NUMERIC;
            if (def->unit_idx != DF_UNIT_MINUTES && def->unit_idx != DF_UNIT_SECONDS)
                display_default = DF_DISPLAY_AUTO;
        } else if (baseStyle >= 0) {
            style_enum = baseStyle;
            display_default = DF_DISPLAY_AUTO;
        }
    }
    /* Step 4: If style is "numeric" and IsFractionalSecondUnitName(unit), set style to "fractional" */
    if (style_enum == DF_STYLE_NUMERIC && is_fractional_second_unit(def->unit_idx)) {
        style_enum = DF_STYLE_FRACTIONAL;
        display_default = DF_DISPLAY_AUTO;
    }
    /* Step 5: GetOption(options, unitDisplay, "string", ["auto", "always"], displayDefault) */
    {
        JSValue dval = JS_GetPropertyStr(ctx, options, def->displaySlot);
        if (JS_IsException(dval))
            goto done;
        if (!JS_IsUndefined(dval)) {
            display_cstr = JS_ToCStringNZ(ctx, dval);
            JS_FreeValue(ctx, dval);
            if (!display_cstr)
                goto done;
            display_enum = df_display_string_to_enum(display_cstr);
            if (display_enum < 0) {
                JS_ThrowRangeError(ctx, "invalid %s", def->displaySlot);
                goto done;
            }
        }
    }
    if (display_enum < 0)
        display_enum = display_default;
    /* Step 6: ValidateDurationUnitStyle */
    if (display_enum == DF_DISPLAY_ALWAYS && style_enum == DF_STYLE_FRACTIONAL) {
        JS_ThrowRangeError(ctx, "invalid %s", def->displaySlot);
        goto done;
    }
    if (prevStyle >= 0 && prevStyle == DF_STYLE_FRACTIONAL && style_enum != DF_STYLE_FRACTIONAL) {
        JS_ThrowRangeError(ctx, "style conflict for %s", def->unit);
        goto done;
    }
    if (prevStyle >= 0 && (prevStyle == DF_STYLE_NUMERIC || prevStyle == DF_STYLE_2DIGIT)) {
        if (style_enum != DF_STYLE_FRACTIONAL && style_enum != DF_STYLE_NUMERIC && style_enum != DF_STYLE_2DIGIT) {
            JS_ThrowRangeError(ctx, "style conflict for %s", def->unit);
            goto done;
        }
    }
    /* Step 7: If unit is "hours" and twoDigitHours, set style to "2-digit" */
    if (def->unit_idx == DF_UNIT_HOURS && two_digit_hours)
        style_enum = DF_STYLE_2DIGIT;
    /* Step 8: If unit is "minutes" or "seconds" and prevStyle is "numeric" or "2-digit", set style to "2-digit" */
    if ((def->unit_idx == DF_UNIT_MINUTES || def->unit_idx == DF_UNIT_SECONDS) &&
        prevStyle >= 0 && (prevStyle == DF_STYLE_NUMERIC || prevStyle == DF_STYLE_2DIGIT)) {
        style_enum = DF_STYLE_2DIGIT;
    }
    s->unit_style[def->unit_idx] = style_enum;
    s->unit_display[def->unit_idx] = display_enum;
    ret = 0;
 done:
    JS_FreeCString(ctx, style_cstr);
    JS_FreeCString(ctx, display_cstr);
    return ret;
}

static JSValue js_durationformat_constructor(JSContext *ctx,
                                              JSValueConst new_target,
                                              int argc, JSValueConst *argv)
{
    JSDurationFormatData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->locale = js_strdup(ctx, "en-US");
    s->numberingSystem = NS_LATN;
    s->style = DF_STYLE_SHORT;

    obj = JS_CreateFromConstructor(ctx, new_target, js_durationformat_class_id);
    if (JS_IsException(obj))
        goto fail;

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    /* Save canonical locale with extensions for resolvedOptions, and extract
       numberingSystem from the Unicode extension. */
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
        }
    }

    options = js_intl_get_options_object(ctx, argc > 1 ? argv[1] : JS_UNDEFINED);
    if (JS_IsException(options))
        goto fail;

    /* localeMatcher */
    {
        int idx = js_intl_get_string_option(ctx, options, "localeMatcher",
                                             intl_localeMatcher_names,
                                             countof(intl_localeMatcher_names),
                                             INTL_LOCALEMATCHER_BESTFIT);
        if (idx < 0)
            goto fail_options;
    }
    /* numberingSystem */
    {
        JSValue v = JS_GetPropertyStr(ctx, options, "numberingSystem");
        const char *str;
        if (JS_IsException(v))
            goto fail_options;
        if (!JS_IsUndefined(v)) {
            str = JS_ToCStringNZ(ctx, v);
            JS_FreeValue(ctx, v);
            if (!str)
                goto fail_options;
            if (!is_valid_numbering_system(str)) {
                JS_FreeCString(ctx, str);
                JS_ThrowRangeError(ctx, "invalid numberingSystem");
                goto fail_options;
            }
            {
                int ns = js_intl_string_to_numbering_system(str);
                if (ns != NS_UNKNOWN) {
                    s->has_numberingSystem_option = 1;
                    s->numberingSystem = ns;
                }
            }
            JS_FreeCString(ctx, str);
        }
    }
    /* style */
    {
        int idx = js_intl_get_string_option(ctx, options, "style",
                                             df_top_style_names,
                                             countof(df_top_style_names),
                                             1 /* "short" */);
        if (idx < 0)
            goto fail_options;
        s->style = idx;
    }
    /* unit options */
    {
        int prevStyle = -1;
        int i;
        for (i = 0; i < (int)countof(df_units); i++) {
            const UnitDef *def = &df_units[i];
            if (read_duration_unit(ctx, options, def, s->style, prevStyle, FALSE, s) < 0)
                goto fail_options;
            prevStyle = s->unit_style[def->unit_idx];
        }
    }
    /* fractionalDigits */
    {
        int val;
        int ret = js_intl_get_number_option(ctx, options, "fractionalDigits",
                                             &val, 0, 9);
        if (ret < 0)
            goto fail_options;
        if (ret > 0) {
            s->has_fractionalDigits = 1;
            s->fractionalDigits_val = val;
        }
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
            dbuf_put(&db, (uint8_t *)"-u", 2);
            {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-nu-", 4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            dbuf_put(&db, (uint8_t *)"", 1);
            if (dbuf_error(&db)) {
                dbuf_free(&db);
                return JS_EXCEPTION;
            }
            js_free(ctx, s->locale);
            s->locale = js_strdup(ctx, (char *)db.buf);
            dbuf_free(&db);
        }
    }

    JS_FreeValue(ctx, options);
    JS_SetOpaque(obj, s);
    return obj;
 fail_options:
    JS_FreeValue(ctx, options);
 fail:
    js_free(ctx, s->locale);
    js_free(ctx, s->locale_canonical);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static void df_timespec_from_ms(int64_t ms, JSTimeSpec *ts)
{
    ts->secs = ms / 1000;
    ts->nsecs = (uint32_t)(ms % 1000) * 1000000;
}

static void df_timespec_from_us(int64_t us, JSTimeSpec *ts)
{
    ts->secs = us / 1000000;
    ts->nsecs = (uint32_t)(us % 1000000) * 1000;
}

static void df_timespec_from_ns(int64_t ns, JSTimeSpec *ts)
{
    ts->secs = ns / 1000000000;
    ts->nsecs = (uint32_t)(ns % 1000000000);
}

static void df_timespec_add(JSTimeSpec *ts, const JSTimeSpec *other)
{
    ts->secs += other->secs;
    ts->nsecs += other->nsecs;
    if (ts->nsecs >= 1000000000) {
        ts->secs += ts->nsecs / 1000000000;
        ts->nsecs %= 1000000000;
    }
}

static void df_timespec_mul(JSTimeSpec *ts, uint32_t n)
{
    uint64_t nsecs_prod = (uint64_t)ts->nsecs * n;
    ts->secs = ts->secs * (int64_t)n + (int64_t)(nsecs_prod / 1000000000);
    ts->nsecs = (uint32_t)(nsecs_prod % 1000000000);
}

/* Decompose a finite non-negative double into a * 2^e where a is integer.
   d == 0.0 gives a = 0, e = 0.  For integers, e <= 30 for our inputs. */
static void df_double_to_a_exp(double d, uint64_t *pa, int *pe)
{
    uint64_t u, mant;
    int exp;
    if (d == 0.0) {
        *pa = 0;
        *pe = 0;
        return;
    }
    memcpy(&u, &d, sizeof(u));
    mant = u & ((1ULL << 52) - 1);
    exp = (int)((u >> 52) & 0x7FF);
    if (exp == 0) {
        *pa = mant;
        *pe = -1022 - 52;
    } else {
        *pa = mant | (1ULL << 52);
        *pe = exp - 1023 - 52;
    }
}

static void df_timespec_from_double_ms(double d, JSTimeSpec *ts)
{
    df_timespec_from_ms((int64_t)d, ts);
}

static void df_timespec_from_double_us(double d, JSTimeSpec *ts)
{
    if (d <= (double)INT64_MAX) {
        df_timespec_from_us((int64_t)d, ts);
    } else {
        uint64_t a;
        int e;
        df_double_to_a_exp(d, &a, &e);
        df_timespec_from_us((int64_t)a, ts);
        df_timespec_mul(ts, (uint32_t)1 << e);
    }
}

static void df_timespec_from_double_ns(double d, JSTimeSpec *ts)
{
    if (d <= (double)INT64_MAX) {
        df_timespec_from_ns((int64_t)d, ts);
    } else {
        uint64_t a;
        int e;
        df_double_to_a_exp(d, &a, &e);
        df_timespec_from_ns((int64_t)a, ts);
        df_timespec_mul(ts, (uint32_t)1 << e);
    }
}

static int is_valid_duration_record(JSContext *ctx, const JSDurationData *d)
{
    double values[DF_UNIT_COUNT] = {0};
    int i;
    int has_negative = 0;
    int has_positive = 0;

    values[0] = (double)d->years;
    values[1] = (double)d->months;
    values[2] = (double)d->weeks;
    values[3] = (double)d->days;
    values[4] = (double)d->hours;
    values[5] = (double)d->minutes;
    values[6] = (double)d->seconds;
    values[7] = (double)d->milliseconds;
    values[8] = (double)d->microseconds.int128_value;
    values[9] = (double)d->nanoseconds.int128_value;

    for (i = 0; i < DF_UNIT_COUNT; i++) {
        if (values[i] < 0)
            has_negative = 1;
        else if (values[i] > 0)
            has_positive = 1;
        values[i] = fabs(values[i]);
    }
    if (has_negative && has_positive) {
        JS_ThrowRangeError(ctx, "duration properties must not have mixed signs");
        return -1;
    }

    if (values[0] >= 4294967296.0 ||
        values[1] >= 4294967296.0 ||
        values[2] >= 4294967296.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }

    if (values[3] != 0.0 && values[3] >= 104249991375.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[4] != 0.0 && values[4] >= 2501999792984.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[5] != 0.0 && values[5] >= 150119987579017.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[6] != 0.0 && values[6] >= 9007199254740992.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[7] != 0.0 && values[7] >= 9007199254740992000.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[8] != 0.0 && values[8] >= 9007199254740992000000.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }
    if (values[9] != 0.0 && values[9] >= 9007199254740992000000000.0) {
        JS_ThrowRangeError(ctx, "duration out of range");
        return -1;
    }

    {
        JSTimeSpec total = {0, 0};
        JSTimeSpec ts;

        ts.secs = (int64_t)values[3] * 86400; ts.nsecs = 0;
        df_timespec_add(&total, &ts);
        ts.secs = (int64_t)values[4] * 3600; ts.nsecs = 0;
        df_timespec_add(&total, &ts);
        ts.secs = (int64_t)values[5] * 60; ts.nsecs = 0;
        df_timespec_add(&total, &ts);
        ts.secs = (int64_t)values[6]; ts.nsecs = 0;
        df_timespec_add(&total, &ts);
        df_timespec_from_double_ms(values[7], &ts);
        df_timespec_add(&total, &ts);
        df_timespec_from_double_us(values[8], &ts);
        df_timespec_add(&total, &ts);
        df_timespec_from_double_ns(values[9], &ts);
        df_timespec_add(&total, &ts);

        if (total.secs >= 9007199254740992LL) {
            JS_ThrowRangeError(ctx, "duration out of range");
            return -1;
        }
    }

    return 1;
}

/* --------------------------------------------------------------------- */
/* DurationFormat formatting helpers                                     */
/* --------------------------------------------------------------------- */

/* Compute a fractional decimal string for combined sub-second units.
   start_idx: 6=seconds, 7=milliseconds, 8=microseconds.
   exponent: 9 for seconds, 6 for milliseconds, 3 for microseconds.
   Returns a newly allocated string via DynBuf. */
static char *df_duration_to_fractional(JSContext *ctx, double *values,
                                        int start_idx, int exponent)
{
    JSTimeSpec total = {0, 0};
    JSTimeSpec ts;
    int64_t q;
    uint32_t r;
    DynBuf db;
    int is_neg = 0;
    int i;

    for (i = 3; i < 10; i++) {
        if (values[i] != 0.0) {
            if (values[i] < 0)
                is_neg = 1;
            break;
        }
    }

    if (exponent >= 9) {
        ts.secs = (int64_t)fabs(values[6]); ts.nsecs = 0;
        df_timespec_add(&total, &ts);
    }
    if (exponent >= 6) {
        df_timespec_from_double_ms(fabs(values[7]), &ts);
        df_timespec_add(&total, &ts);
    }
    if (exponent >= 3) {
        df_timespec_from_double_us(fabs(values[8]), &ts);
        df_timespec_add(&total, &ts);
    }
    df_timespec_from_double_ns(fabs(values[9]), &ts);
    df_timespec_add(&total, &ts);

    if (exponent == 9) {
        q = total.secs;
        r = total.nsecs;
    } else if (exponent == 6) {
        q = total.secs * 1000 + total.nsecs / 1000000;
        r = total.nsecs % 1000000;
    } else { /* exponent == 3 */
        q = total.secs * 1000000 + total.nsecs / 1000;
        r = total.nsecs % 1000;
    }

    js_intl_dbuf_init(ctx, &db);

    {
        char q_buf[32];
        size_t len = i64toa(q_buf, is_neg ? -q : q);
        q_buf[len] = '\0';
        dbuf_putstr(&db, q_buf);
    }

    dbuf_putc(&db, '.');

    {
        char rev[32];
        int rlen = 0;
        uint32_t tr = r;
        while (tr > 0) {
            rev[rlen++] = (char)('0' + (int)(tr % 10));
            tr /= 10;
        }
        for (i = 0; i < exponent - rlen; i++)
            dbuf_putc(&db, '0');
        for (i = rlen - 1; i >= 0; i--)
            dbuf_putc(&db, (uint8_t)rev[i]);
    }

    dbuf_putc(&db, '\0');
    if (dbuf_error(&db)) {
        dbuf_free(&db);
        return NULL;
    }
    return (char *)db.buf;
}

/* Create a NumberFormat JS object with locale/options and call format(value). */
static JSValue df_nf_format(JSContext *ctx, const char *locale,
                             JSValueConst options, JSValueConst value)
{
    JSValue ctor, argv[2], nf, method, ret;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "NumberFormat not available");

    argv[0] = JS_NewString(ctx, locale);
    if (JS_IsException(argv[0])) {
        JS_FreeValue(ctx, ctor);
        return argv[0];
    }
    argv[1] = JS_DupValue(ctx, options);

    nf = JS_CallConstructor(ctx, ctor, 2, argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    if (JS_IsException(nf))
        return nf;

    method = JS_GetPropertyStr(ctx, nf, "format");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, nf);
        return method;
    }

    ret = JS_Call(ctx, method, nf, 1, &value);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, nf);
    return ret;
}

/* Create a NumberFormat JS object and call formatToParts(value), then add
   'unit' property to each part. */
static JSValue df_nf_formatToParts(JSContext *ctx, const char *locale,
                                    JSValueConst options, JSValueConst value,
                                    const char *unit_name)
{
    JSValue ctor, argv[2], nf, method, ret, arr;
    int32_t len;
    int i;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_NUMBERFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "NumberFormat not available");

    argv[0] = JS_NewString(ctx, locale);
    if (JS_IsException(argv[0])) {
        JS_FreeValue(ctx, ctor);
        return argv[0];
    }
    argv[1] = JS_DupValue(ctx, options);

    nf = JS_CallConstructor(ctx, ctor, 2, argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    if (JS_IsException(nf))
        return nf;

    method = JS_GetPropertyStr(ctx, nf, "formatToParts");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, nf);
        return method;
    }
    if (!JS_IsFunction(ctx, method)) {
        JS_FreeValue(ctx, method);
        JS_FreeValue(ctx, nf);
        return JS_ThrowTypeError(ctx, "not a function");
    }

    ret = JS_Call(ctx, method, nf, 1, &value);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, nf);
    if (JS_IsException(ret))
        return ret;

    arr = ret;
    {
        JSValue lv = JS_GetPropertyStr(ctx, arr, "length");
        if (!JS_IsException(lv)) {
            if (!JS_ToInt32(ctx, &len, lv)) {
                for (i = 0; i < len; i++) {
                    JSValue part = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
                    if (!JS_IsException(part)) {
                        JS_SetPropertyStr(ctx, part, "unit",
                                          JS_NewString(ctx, unit_name));
                        JS_FreeValue(ctx, part);
                    }
                }
            }
            JS_FreeValue(ctx, lv);
        }
    }
    return arr;
}

/* Create a ListFormat JS object and call format(list). */
static JSValue df_lf_format(JSContext *ctx, const char *locale,
                             const char *type, const char *style,
                             JSValueConst list)
{
    JSValue ctor, argv[2], lf, method, ret, options;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_LISTFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "ListFormat not available");

    argv[0] = JS_NewString(ctx, locale);
    if (JS_IsException(argv[0])) {
        JS_FreeValue(ctx, ctor);
        return argv[0];
    }

    options = JS_NewObject(ctx);
    if (JS_IsException(options)) {
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, argv[0]);
        return options;
    }
    JS_SetPropertyStr(ctx, options, "type", JS_NewString(ctx, type));
    JS_SetPropertyStr(ctx, options, "style", JS_NewString(ctx, style));
    argv[1] = options;

    lf = JS_CallConstructor(ctx, ctor, 2, argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    if (JS_IsException(lf))
        return lf;

    method = JS_GetPropertyStr(ctx, lf, "format");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, lf);
        return method;
    }
    if (!JS_IsFunction(ctx, method)) {
        JS_FreeValue(ctx, method);
        JS_FreeValue(ctx, lf);
        return JS_ThrowTypeError(ctx, "not a function");
    }

    ret = JS_Call(ctx, method, lf, 1, &list);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, lf);
    return ret;
}

/* Create a ListFormat JS object and call formatToParts(list). */
static JSValue df_lf_formatToParts(JSContext *ctx, const char *locale,
                                    const char *type, const char *style,
                                    JSValueConst list)
{
    JSValue ctor, argv[2], lf, method, ret, options;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_LISTFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "ListFormat not available");

    argv[0] = JS_NewString(ctx, locale);
    if (JS_IsException(argv[0])) {
        JS_FreeValue(ctx, ctor);
        return argv[0];
    }

    options = JS_NewObject(ctx);
    if (JS_IsException(options)) {
        JS_FreeValue(ctx, ctor);
        JS_FreeValue(ctx, argv[0]);
        return options;
    }
    JS_SetPropertyStr(ctx, options, "type", JS_NewString(ctx, type));
    JS_SetPropertyStr(ctx, options, "style", JS_NewString(ctx, style));
    argv[1] = options;

    lf = JS_CallConstructor(ctx, ctor, 2, argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, argv[0]);
    JS_FreeValue(ctx, argv[1]);
    if (JS_IsException(lf))
        return lf;

    method = JS_GetPropertyStr(ctx, lf, "formatToParts");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, lf);
        return method;
    }

    ret = JS_Call(ctx, method, lf, 1, &list);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, lf);
    return ret;
}

/* Shared logic to build formatted groups. Returns the number of groups (>0) or
   -1 on exception. *out_strings and *out_parts are populated with JS arrays. */
static int df_build_groups(JSContext *ctx, JSDurationFormatData *s,
                            const JSDurationData *duration,
                            JSValue *out_strings, JSValue *out_parts)
{
    const char *unit_names[] = {"year", "month", "week", "day", "hour",
                                "minute", "second", "millisecond",
                                "microsecond", "nanosecond"};
    double values[DF_UNIT_COUNT] = {0};
    int is_negative = 0;
    int displayNegativeSign = 1;
    int needSeparator = 0;
    JSValue strings = JS_UNDEFINED;
    JSValue parts = JS_UNDEFINED;
    DynBuf cur_str;
    JSValue cur_parts = JS_UNDEFINED;
    int group_idx = 0;
    int i;

    strings = JS_NewArray(ctx);
    if (JS_IsException(strings))
        goto fail;
    parts = JS_NewArray(ctx);
    if (JS_IsException(parts))
        goto fail;

    values[0] = (double)duration->years;
    values[1] = (double)duration->months;
    values[2] = (double)duration->weeks;
    values[3] = (double)duration->days;
    values[4] = (double)duration->hours;
    values[5] = (double)duration->minutes;
    values[6] = (double)duration->seconds;
    values[7] = (double)duration->milliseconds;
    values[8] = (double)duration->microseconds.int128_value;
    values[9] = (double)duration->nanoseconds.int128_value;

    for (i = 0; i < DF_UNIT_COUNT; i++) {
        if (values[i] < 0)
            is_negative = 1;
    }

    js_intl_dbuf_init(ctx, &cur_str);
    cur_parts = JS_NewArray(ctx);
    if (JS_IsException(cur_parts))
        goto fail;

    for (i = 0; i < DF_UNIT_COUNT; i++) {
        int style = s->unit_style[i];
        int display = s->unit_display[i];
        int should_display = 0;
        int displayRequired = 0;
        int combine = 0;
        int exponent = 0;
        JSValue nfOpts = JS_UNDEFINED;
        JSValue value = JS_UNDEFINED;
        JSValue formatted = JS_UNDEFINED;
        JSValue formatted_parts = JS_UNDEFINED;
        char *frac_str = NULL;
        int is_num_style = 0;

        if (style < 0 || display < 0)
            continue;

        if (i == DF_UNIT_SECONDS && s->unit_style[DF_UNIT_MILLISECONDS] == DF_STYLE_FRACTIONAL) {
            combine = 1; exponent = 9;
        } else if (i == DF_UNIT_MILLISECONDS && s->unit_style[DF_UNIT_MICROSECONDS] == DF_STYLE_FRACTIONAL) {
            combine = 1; exponent = 6;
        } else if (i == DF_UNIT_MICROSECONDS && s->unit_style[DF_UNIT_NANOSECONDS] == DF_STYLE_FRACTIONAL) {
            combine = 1; exponent = 3;
        }

        if (i == DF_UNIT_MINUTES && needSeparator) {
            if ((s->unit_display[DF_UNIT_SECONDS] == DF_DISPLAY_ALWAYS) ||
                values[DF_UNIT_SECONDS] != 0.0 || values[DF_UNIT_MILLISECONDS] != 0.0 ||
                values[DF_UNIT_MICROSECONDS] != 0.0 || values[DF_UNIT_NANOSECONDS] != 0.0) {
                displayRequired = 1;
            }
        }

        if (values[i] != 0.0 || display == DF_DISPLAY_ALWAYS || displayRequired)
            should_display = 1;
        else if (combine) {
            if (i == DF_UNIT_SECONDS && (values[DF_UNIT_MILLISECONDS] != 0.0 || values[DF_UNIT_MICROSECONDS] != 0.0 || values[DF_UNIT_NANOSECONDS] != 0.0))
                should_display = 1;
            else if (i == DF_UNIT_MILLISECONDS && (values[DF_UNIT_MICROSECONDS] != 0.0 || values[DF_UNIT_NANOSECONDS] != 0.0))
                should_display = 1;
            else if (i == DF_UNIT_MICROSECONDS && values[DF_UNIT_NANOSECONDS] != 0.0)
                should_display = 1;
        }

        if (!should_display)
            continue;

        /* Finalize previous group if starting a new one */
        if (!needSeparator) {
            if (cur_str.size > 0) {
                dbuf_putc(&cur_str, '\0');
                if (dbuf_error(&cur_str))
                    goto loop_fail;
                {
                    JSValue str = JS_NewString(ctx, (char *)cur_str.buf);
                    if (JS_IsException(str))
                        goto loop_fail;
                    JS_DefinePropertyValueUint32(ctx, strings, (uint32_t)group_idx,
                                                  str, JS_PROP_C_W_E);
                }
                JS_DefinePropertyValueUint32(ctx, parts, (uint32_t)group_idx,
                                              cur_parts, JS_PROP_C_W_E);
                cur_parts = JS_NewArray(ctx);
                if (JS_IsException(cur_parts))
                    goto loop_fail;
                group_idx++;
                cur_str.size = 0;
            }
        } else {
            dbuf_putstr(&cur_str, ":");
            {
                JSValue sep = JS_NewObject(ctx);
                if (JS_IsException(sep))
                    goto loop_fail;
                JS_SetPropertyStr(ctx, sep, "type",
                                  JS_NewString(ctx, "literal"));
                JS_SetPropertyStr(ctx, sep, "value",
                                  JS_NewString(ctx, ":"));
                {
                    int32_t plen = 0;
                    JSValue lv = JS_GetPropertyStr(ctx, cur_parts, "length");
                    if (!JS_IsException(lv)) {
                        JS_ToInt32(ctx, &plen, lv);
                        JS_FreeValue(ctx, lv);
                    }
                    JS_DefinePropertyValueUint32(ctx, cur_parts, (uint32_t)plen, sep,
                                                  JS_PROP_C_W_E);
                }
            }
        }

        nfOpts = JS_NewObject(ctx);
        if (JS_IsException(nfOpts))
            goto loop_fail;

        JS_SetPropertyStr(ctx, nfOpts, "numberingSystem",
                          JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)));

        if (displayNegativeSign) {
            displayNegativeSign = 0;
            if (values[i] == 0.0) {
                if (is_negative)
                    value = __JS_NewFloat64(ctx, -0.0);
                else
                    value = __JS_NewFloat64(ctx, 0.0);
            } else {
                value = __JS_NewFloat64(ctx, values[i]);
            }
        } else {
            JS_SetPropertyStr(ctx, nfOpts, "signDisplay",
                              JS_NewString(ctx, "never"));
            value = __JS_NewFloat64(ctx, values[i]);
        }
        if (JS_IsException(value))
            goto loop_fail;

        if (combine) {
            JS_FreeValue(ctx, value);
            frac_str = df_duration_to_fractional(ctx, values, i, exponent);
            if (!frac_str)
                goto loop_fail;
            value = JS_NewString(ctx, frac_str);
            js_free(ctx, frac_str);
            if (JS_IsException(value))
                goto loop_fail;
            if (!s->has_fractionalDigits) {
                JS_SetPropertyStr(ctx, nfOpts, "maximumFractionDigits",
                                  JS_NewInt32(ctx, 9));
                JS_SetPropertyStr(ctx, nfOpts, "minimumFractionDigits",
                                  JS_NewInt32(ctx, 0));
            } else {
                JS_SetPropertyStr(ctx, nfOpts, "maximumFractionDigits",
                                  JS_NewInt32(ctx, s->fractionalDigits_val));
                JS_SetPropertyStr(ctx, nfOpts, "minimumFractionDigits",
                                  JS_NewInt32(ctx, s->fractionalDigits_val));
            }
            JS_SetPropertyStr(ctx, nfOpts, "roundingMode",
                              JS_NewString(ctx, "trunc"));
        }

        if (style == DF_STYLE_2DIGIT) {
            JS_SetPropertyStr(ctx, nfOpts, "minimumIntegerDigits",
                              JS_NewInt32(ctx, 2));
        }

        if (style != DF_STYLE_NUMERIC && style != DF_STYLE_2DIGIT &&
            style != DF_STYLE_FRACTIONAL) {
            JS_SetPropertyStr(ctx, nfOpts, "style",
                              JS_NewString(ctx, "unit"));
            JS_SetPropertyStr(ctx, nfOpts, "unit",
                              JS_NewString(ctx, unit_names[i]));
            JS_SetPropertyStr(ctx, nfOpts, "unitDisplay",
                              JS_NewString(ctx, df_style_names[style]));
        } else {
            JS_SetPropertyStr(ctx, nfOpts, "useGrouping",
                              JS_NewBool(ctx, 0));
        }

        formatted = df_nf_format(ctx, s->locale, nfOpts, value);
        if (JS_IsException(formatted))
            goto loop_fail;

        formatted_parts = df_nf_formatToParts(ctx, s->locale, nfOpts, value,
                                               unit_names[i]);
        if (JS_IsException(formatted_parts))
            goto loop_fail;

        {
            const char *fstr = JS_ToCString(ctx, formatted);
            if (!fstr)
                goto loop_fail;
            dbuf_putstr(&cur_str, fstr);
            JS_FreeCString(ctx, fstr);
        }

        {
            int32_t fp_len = 0;
            int j;
            JSValue lv = JS_GetPropertyStr(ctx, formatted_parts, "length");
            if (!JS_IsException(lv)) {
                JS_ToInt32(ctx, &fp_len, lv);
                JS_FreeValue(ctx, lv);
            }
            for (j = 0; j < fp_len; j++) {
                JSValue part = JS_GetPropertyUint32(ctx, formatted_parts, (uint32_t)j);
                int32_t cp_len = 0;
                JSValue lv2 = JS_GetPropertyStr(ctx, cur_parts, "length");
                if (!JS_IsException(lv2)) {
                    JS_ToInt32(ctx, &cp_len, lv2);
                    JS_FreeValue(ctx, lv2);
                }
                JS_DefinePropertyValueUint32(ctx, cur_parts, (uint32_t)cp_len, part,
                                              JS_PROP_C_W_E);
            }
        }

        is_num_style = (style == DF_STYLE_NUMERIC || style == DF_STYLE_2DIGIT);
        if (!needSeparator) {
            if (is_num_style)
                needSeparator = 1;
        }

        JS_FreeValue(ctx, nfOpts);
        JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, formatted);
        JS_FreeValue(ctx, formatted_parts);

        if (combine)
            break;
        continue;

    loop_fail:
        JS_FreeValue(ctx, nfOpts);
        JS_FreeValue(ctx, value);
        JS_FreeValue(ctx, formatted);
        JS_FreeValue(ctx, formatted_parts);
        goto fail;
    }

    if (cur_str.size > 0) {
        dbuf_putc(&cur_str, '\0');
        if (dbuf_error(&cur_str))
            goto fail;
        {
            JSValue str = JS_NewString(ctx, (char *)cur_str.buf);
            if (JS_IsException(str))
                goto fail;
            JS_DefinePropertyValueUint32(ctx, strings, (uint32_t)group_idx,
                                          str, JS_PROP_C_W_E);
        }
        JS_DefinePropertyValueUint32(ctx, parts, (uint32_t)group_idx,
                                      cur_parts, JS_PROP_C_W_E);
        group_idx++;
    } else {
        JS_FreeValue(ctx, cur_parts);
    }

    dbuf_free(&cur_str);
    *out_strings = strings;
    *out_parts = parts;
    return group_idx;

fail:
    dbuf_free(&cur_str);
    JS_FreeValue(ctx, strings);
    JS_FreeValue(ctx, parts);
    JS_FreeValue(ctx, cur_parts);
    *out_strings = JS_UNDEFINED;
    *out_parts = JS_UNDEFINED;
    return -1;
}

static JSValue js_durationformat_format(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    JSDurationFormatData *s = JS_GetOpaque2(ctx, this_val, js_durationformat_class_id);
    JSDurationData d = {0};
    JSValue strings = JS_UNDEFINED;
    JSValue parts = JS_UNDEFINED;
    JSValue list = JS_UNDEFINED;
    JSValue ret = JS_UNDEFINED;
    int group_count;
    int i;
    const char *list_style_str;

    if (!s)
        return JS_EXCEPTION;

    if (JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]) ||
        JS_IsBool(argv[0]) || JS_IsSymbol(argv[0]) || JS_IsNumber(argv[0]) ||
        JS_IsBigInt(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "format requires a duration argument");

    if (js_to_temporal_duration(ctx, argv[0], &d) < 0)
        return JS_EXCEPTION;

    if (is_valid_duration_record(ctx, &d) < 0)
        return JS_EXCEPTION;

    group_count = df_build_groups(ctx, s, &d, &strings, &parts);
    if (group_count < 0)
        return JS_EXCEPTION;

    list = JS_NewArray(ctx);
    if (JS_IsException(list))
        goto fail;

    for (i = 0; i < group_count; i++) {
        JSValue str = JS_GetPropertyUint32(ctx, strings, (uint32_t)i);
        if (JS_IsException(str))
            goto fail;
        JS_DefinePropertyValueUint32(ctx, list, (uint32_t)i, str, JS_PROP_C_W_E);
    }

    list_style_str = df_style_names[s->style == DF_STYLE_DIGITAL ? DF_STYLE_SHORT : s->style];

    ret = df_lf_format(ctx, s->locale, "unit", list_style_str, list);
    JS_FreeValue(ctx, list);
    JS_FreeValue(ctx, strings);
    JS_FreeValue(ctx, parts);
    return ret;

fail:
    JS_FreeValue(ctx, list);
    JS_FreeValue(ctx, strings);
    JS_FreeValue(ctx, parts);
    return JS_EXCEPTION;
}

static JSValue js_durationformat_formatToParts(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSDurationFormatData *s = JS_GetOpaque2(ctx, this_val, js_durationformat_class_id);
    JSDurationData d = {0};
    JSValue strings = JS_UNDEFINED;
    JSValue parts = JS_UNDEFINED;
    JSValue list = JS_UNDEFINED;
    JSValue lf_parts = JS_UNDEFINED;
    JSValue result = JS_UNDEFINED;
    int group_count;
    int i;
    int32_t lf_len;
    const char *list_style_str;

    if (!s)
        return JS_EXCEPTION;

    if (JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]) ||
        JS_IsBool(argv[0]) || JS_IsSymbol(argv[0]) || JS_IsNumber(argv[0]) ||
        JS_IsBigInt(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "formatToParts requires a duration argument");

    if (js_to_temporal_duration(ctx, argv[0], &d) < 0)
        return JS_EXCEPTION;

    if (is_valid_duration_record(ctx, &d) < 0)
        return JS_EXCEPTION;

    group_count = df_build_groups(ctx, s, &d, &strings, &parts);
    if (group_count < 0)
        return JS_EXCEPTION;

    list = JS_NewArray(ctx);
    if (JS_IsException(list))
        goto fail;

    for (i = 0; i < group_count; i++) {
        JSValue str = JS_GetPropertyUint32(ctx, strings, (uint32_t)i);
        if (JS_IsException(str))
            goto fail;
        JS_DefinePropertyValueUint32(ctx, list, (uint32_t)i, str, JS_PROP_C_W_E);
    }

    list_style_str = df_style_names[s->style == DF_STYLE_DIGITAL ? DF_STYLE_SHORT : s->style];

    lf_parts = df_lf_formatToParts(ctx, s->locale, "unit", list_style_str, list);
    if (JS_IsException(lf_parts))
        goto fail;

    result = JS_NewArray(ctx);
    if (JS_IsException(result))
        goto fail;

    {
        JSValue lv = JS_GetPropertyStr(ctx, lf_parts, "length");
        lf_len = 0;
        if (!JS_IsException(lv)) {
            JS_ToInt32(ctx, &lf_len, lv);
            JS_FreeValue(ctx, lv);
        }
    }

    {
        int group_offset = 0;
        for (i = 0; i < lf_len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, lf_parts, (uint32_t)i);
            if (JS_IsException(item))
                goto fail;
            {
                JSValue type_val = JS_GetPropertyStr(ctx, item, "type");
                if (!JS_IsException(type_val)) {
                    const char *type_str = JS_ToCString(ctx, type_val);
                    if (type_str) {
                        if (!strcmp(type_str, "element")) {
                            JSValue group = JS_GetPropertyUint32(ctx, parts, (uint32_t)group_offset);
                            group_offset++;
                            if (!JS_IsException(group)) {
                                int32_t g_len = 0;
                                int j;
                                JSValue glv = JS_GetPropertyStr(ctx, group, "length");
                                if (!JS_IsException(glv)) {
                                    JS_ToInt32(ctx, &g_len, glv);
                                    JS_FreeValue(ctx, glv);
                                }
                                for (j = 0; j < g_len; j++) {
                                    JSValue p = JS_GetPropertyUint32(ctx, group, (uint32_t)j);
                                    int32_t rlen = 0;
                                    JSValue rlv = JS_GetPropertyStr(ctx, result, "length");
                                    if (!JS_IsException(rlv)) {
                                        JS_ToInt32(ctx, &rlen, rlv);
                                        JS_FreeValue(ctx, rlv);
                                    }
                                    JS_DefinePropertyValueUint32(ctx, result, (uint32_t)rlen,
                                                                  p, JS_PROP_C_W_E);
                                }
                                JS_FreeValue(ctx, group);
                            }
                        } else {
                            JSValue rlv = JS_GetPropertyStr(ctx, result, "length");
                            int32_t rlen = 0;
                            if (!JS_IsException(rlv)) {
                                JS_ToInt32(ctx, &rlen, rlv);
                                JS_FreeValue(ctx, rlv);
                            }
                            JS_DefinePropertyValueUint32(ctx, result, (uint32_t)rlen,
                                                          JS_DupValue(ctx, item),
                                                          JS_PROP_C_W_E);
                        }
                        JS_FreeCString(ctx, type_str);
                    }
                    JS_FreeValue(ctx, type_val);
                }
            }
            JS_FreeValue(ctx, item);
        }
    }

    JS_FreeValue(ctx, list);
    JS_FreeValue(ctx, strings);
    JS_FreeValue(ctx, parts);
    JS_FreeValue(ctx, lf_parts);
    return result;

fail:
    JS_FreeValue(ctx, list);
    JS_FreeValue(ctx, strings);
    JS_FreeValue(ctx, parts);
    JS_FreeValue(ctx, lf_parts);
    JS_FreeValue(ctx, result);
    return JS_EXCEPTION;
}

static JSValue js_durationformat_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv)
{
    JSValue obj;
    JSDurationFormatData *s = JS_GetOpaque2(ctx, this_val, js_durationformat_class_id);
    if (!s)
        return JS_EXCEPTION;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "numberingSystem",
                               JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "style",
                               JS_NewString(ctx, df_style_names[s->style]), JS_PROP_C_W_E);
    {
        int i;
        for (i = 0; i < (int)countof(df_units); i++) {
            const UnitDef *def = &df_units[i];
            int style_val = s->unit_style[def->unit_idx];
            int display_val = s->unit_display[def->unit_idx];
            if (style_val >= 0) {
                const char *out_style = df_style_names[style_val];
                if (style_val == DF_STYLE_FRACTIONAL)
                    out_style = "numeric";
                JS_DefinePropertyValueStr(ctx, obj, def->unit,
                                           JS_NewString(ctx, out_style), JS_PROP_C_W_E);
            }
            if (display_val >= 0)
                JS_DefinePropertyValueStr(ctx, obj, def->displaySlot,
                                           JS_NewString(ctx, df_display_names[display_val]), JS_PROP_C_W_E);
        }
    }
    if (s->has_fractionalDigits)
        JS_DefinePropertyValueStr(ctx, obj, "fractionalDigits",
                                   JS_NewInt32(ctx, s->fractionalDigits_val), JS_PROP_C_W_E);
    return obj;
}

static JSValue js_durationformat_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                   int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_durationformat_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.DurationFormat", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("format", 1, js_durationformat_format),
    JS_CFUNC_DEF("formatToParts", 1, js_durationformat_formatToParts),
    JS_CFUNC_DEF("resolvedOptions", 0, js_durationformat_resolvedOptions),
};

static const JSCFunctionListEntry js_durationformat_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_durationformat_supportedLocalesOf),
};

int js_add_durationformat(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_durationformat_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_durationformat_class_id, &js_durationformat_class);

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_DURATIONFORMAT,
                                                js_durationformat_class_id, "DurationFormat",
                                                (JSCFunction *)js_durationformat_constructor, 0,
                                                JS_CFUNC_constructor, 0,
                                                js_durationformat_static_funcs, countof(js_durationformat_static_funcs),
                                                js_durationformat_proto_funcs, countof(js_durationformat_proto_funcs))))
        return -1;

    return 0;
}
