/*
 * QuickJS Temporal.PlainTime implementation
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

JSClassID js_plaintime_class_id;

static void js_plaintime_finalizer(JSRuntime *rt, JSValue val)
{
    JSPlainTimeData *s = JS_GetOpaque(val, js_plaintime_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_plaintime_class = {
    "Temporal.PlainTime",
    .finalizer = js_plaintime_finalizer,
};

JSValue js_create_plaintime(JSContext *ctx, const JSPlainTimeData *t)
{
    JSPlainTimeData *s;
    JSValue obj;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    *s = *t;
    obj = JS_NewObjectClass(ctx, (int)js_plaintime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

/* Forward declaration */
JSValue js_plaintime_from(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv);

static int pt_to_integer_with_truncation(JSContext *ctx, JSValueConst val, int *pres)
{
    double d;
    if (JS_IsUndefined(val)) {
        *pres = 0;
        return 0;
    }
    if (JS_ToFloat64(ctx, &d, val))
        return -1;
    if (!isfinite(d)) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return -1;
    }
    if (d < INT32_MIN || d > INT32_MAX) {
        JS_ThrowRangeError(ctx, "value out of range");
        return -1;
    }
    *pres = (int)(d < 0 ? ceil(d) : floor(d));
    return 0;
}

/* Validate time fields */
static int pt_is_valid_time(int hour, int minute, int second,
                             int millisecond, int microsecond, int nanosecond)
{
    if (hour < 0 || hour > 23)
        return 0;
    if (minute < 0 || minute > 59)
        return 0;
    if (second < 0 || second > 59)
        return 0;
    if (millisecond < 0 || millisecond > 999)
        return 0;
    if (microsecond < 0 || microsecond > 999)
        return 0;
    if (nanosecond < 0 || nanosecond > 999)
        return 0;
    return 1;
}

/* Temporal.PlainTime constructor */
static JSValue js_plaintime_constructor(JSContext *ctx, JSValueConst new_target,
                                         int argc, JSValueConst *argv)
{
    JSPlainTimeData t = {0};
    JSValue obj;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.PlainTime constructor cannot be called as a function");
    }
    if (argc > 0 && pt_to_integer_with_truncation(ctx, argv[0], &t.hour) < 0)
        return JS_EXCEPTION;
    if (argc > 1 && pt_to_integer_with_truncation(ctx, argv[1], &t.minute) < 0)
        return JS_EXCEPTION;
    if (argc > 2 && pt_to_integer_with_truncation(ctx, argv[2], &t.second) < 0)
        return JS_EXCEPTION;
    if (argc > 3 && pt_to_integer_with_truncation(ctx, argv[3], &t.millisecond) < 0)
        return JS_EXCEPTION;
    if (argc > 4 && pt_to_integer_with_truncation(ctx, argv[4], &t.microsecond) < 0)
        return JS_EXCEPTION;
    if (argc > 5 && pt_to_integer_with_truncation(ctx, argv[5], &t.nanosecond) < 0)
        return JS_EXCEPTION;
    if (!pt_is_valid_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond, t.nanosecond))
        return JS_ThrowRangeError(ctx, "invalid PlainTime");
    {
        JSPlainTimeData *s = js_mallocz(ctx, sizeof(*s));
        if (!s)
            return JS_EXCEPTION;
        *s = t;
        obj = js_create_temporal_object(ctx, new_target, js_plaintime_class_id, s);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        return obj;
    }
}

/* Getters */
enum {
    PLAINTIME_HOUR = 0,
    PLAINTIME_MINUTE,
    PLAINTIME_SECOND,
    PLAINTIME_MILLISECOND,
    PLAINTIME_MICROSECOND,
    PLAINTIME_NANOSECOND,
};

static JSValue js_plaintime_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSPlainTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    switch (magic) {
    case PLAINTIME_HOUR: return JS_NewInt32(ctx, s->hour);
    case PLAINTIME_MINUTE: return JS_NewInt32(ctx, s->minute);
    case PLAINTIME_SECOND: return JS_NewInt32(ctx, s->second);
    case PLAINTIME_MILLISECOND: return JS_NewInt32(ctx, s->millisecond);
    case PLAINTIME_MICROSECOND: return JS_NewInt32(ctx, s->microsecond);
    case PLAINTIME_NANOSECOND: return JS_NewInt32(ctx, s->nanosecond);
    default: return JS_UNDEFINED;
    }
}

/* Temporal.PlainTime.prototype.equals */
static JSValue js_plaintime_equals(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainTimeData *s, *other;
    JSValue other_val;
    int ret;
    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plaintime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaintime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    ret = s->hour == other->hour && s->minute == other->minute &&
          s->second == other->second && s->millisecond == other->millisecond &&
          s->microsecond == other->microsecond && s->nanosecond == other->nanosecond;
    JS_FreeValue(ctx, other_val);
    return JS_NewBool(ctx, ret);
}

/* Format PlainTime as ISO string */
static JSValue js_plaintime_toString(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSPlainTimeData *s;
    JSPlainTimeData rounded = {0};
    char buf[64];
    int n;
    int precision_type = -1; /* -1 = auto, 0 = second, 1-9 = digits, 10 = minute, 11 = second, 12 = ms, 13 = us, 14 = ns */
    int fsd_is_auto = 1;
    int fsd_value = 0;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    TIRoundingMode rounding_mode = ROUND_TRUNC;
    int has_smallestUnit = 0;
    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (!JS_IsObject(argv[0]))
            return JS_ThrowTypeError(ctx, "options must be an object");

        /* Step 1: Read fractionalSecondDigits */
        {
            int fsd = ti_get_fractional_second_digits_option(ctx, argv[0], &fsd_value);
            if (fsd < 0)
                return JS_EXCEPTION;
            if (fsd_value != -1)
                fsd_is_auto = 0;
        }

        /* Step 2: Read roundingMode */
        {
            TIRoundingMode mode;
            if (ti_get_rounding_mode_option_default(ctx, argv[0], &mode, ROUND_TRUNC) < 0)
                return JS_EXCEPTION;
            rounding_mode = mode;
        }

        /* Step 3: Read smallestUnit */
        {
            TIUnit unit;
            if (ti_parse_unit_option(ctx, argv[0], "smallestUnit", &unit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
                return JS_EXCEPTION;
            if (unit != TI_UNIT_INVALID) {
                smallestUnit = unit;
                has_smallestUnit = 1;
            }
        }
    }

    /* Validation */
    if (has_smallestUnit && (smallestUnit == TI_UNIT_INVALID || smallestUnit < TI_UNIT_MINUTE || smallestUnit > TI_UNIT_NANOSECOND))
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");

    /* Determine precision: smallestUnit overrides fractionalSecondDigits */
    if (has_smallestUnit && smallestUnit >= TI_UNIT_MINUTE && smallestUnit <= TI_UNIT_NANOSECOND) {
        precision_type = ti_precision_from_smallest_unit(smallestUnit);
    } else if (!fsd_is_auto) {
        precision_type = fsd_value;
    } else {
        precision_type = -1; /* auto */
    }

    /* Apply rounding if precision is not auto and not nanosecond (increment > 1) */
    rounded = *s;
    if (precision_type >= 0 && precision_type != 14) {
        int64_t total_ns = ((int64_t)s->hour * 3600 + s->minute * 60 + s->second) * 1000000000LL
                         + s->millisecond * 1000000LL + s->microsecond * 1000LL + s->nanosecond;
        int64_t increment_ns;

        switch (precision_type) {
        case 0:
        case 11:
            increment_ns = 1000000000LL;
            break;
        case 10:
            increment_ns = 60000000000LL;
            break;
        case 12:
            increment_ns = 1000000LL;
            break;
        case 13:
            increment_ns = 1000LL;
            break;
        default: /* 1-9 digits */
            increment_ns = 1;
            {
                int i;
                for (i = 0; i < 9 - precision_type; i++) increment_ns *= 10;
            }
            break;
        }

        int64_t rounded_ns = ti_round_to_increment_as_if_positive(total_ns, increment_ns, rounding_mode);
        rounded_ns %= 86400000000000LL;

        rounded.hour = (int)(rounded_ns / 3600000000000LL);
        rounded_ns %= 3600000000000LL;
        rounded.minute = (int)(rounded_ns / 60000000000LL);
        rounded_ns %= 60000000000LL;
        rounded.second = (int)(rounded_ns / 1000000000LL);
        rounded_ns %= 1000000000LL;
        rounded.millisecond = (int)(rounded_ns / 1000000LL);
        rounded_ns %= 1000000LL;
        rounded.microsecond = (int)(rounded_ns / 1000LL);
        rounded.nanosecond = (int)(rounded_ns % 1000LL);
    }

    /* Format */
    if (precision_type == 10) { /* minute */
        n = snprintf(buf, sizeof(buf), "%02d:%02d", rounded.hour, rounded.minute);
    } else {
        n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                     rounded.hour, rounded.minute, rounded.second);

        if (precision_type != 0 && precision_type != 11) {
            /* Add fractional part */
            int digits = 9;
            if (precision_type >= 1 && precision_type <= 9)
                digits = precision_type;
            else if (precision_type == 12)
                digits = 3;
            else if (precision_type == 13)
                digits = 6;
            else if (precision_type == 14)
                digits = 9;

            char frac[16];
            snprintf(frac, sizeof(frac), "%03d%03d%03d",
                     rounded.millisecond, rounded.microsecond, rounded.nanosecond);

            if (precision_type == -1) {
                /* auto: drop trailing zeros */
                while (digits > 0 && frac[digits - 1] == '0')
                    digits--;
            }

            if (digits > 0) {
                frac[digits] = '\0';
                snprintf(buf + n, sizeof(buf) - (size_t)n, ".%s", frac);
            }
        }
    }

    return JS_NewString(ctx, buf);
}


static JSValue js_plaintime_toJSON(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_plaintime_toString(ctx, this_val, 0, NULL);
}

static JSValue js_plaintime_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSPlainTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 2, js_plaintime_toString);
}

static JSValue js_plaintime_valueOf(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.PlainTime with < or >");
}

/* Temporal.PlainTime.compare */
static JSValue js_plaintime_compare(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    JSPlainTimeData *s1, *s2;
    JSValue a, b;
    int cmp;
    a = js_plaintime_from(ctx, JS_UNDEFINED, 1, &argv[0]);
    if (JS_IsException(a))
        return JS_EXCEPTION;
    b = js_plaintime_from(ctx, JS_UNDEFINED, 1, &argv[1]);
    if (JS_IsException(b)) {
        JS_FreeValue(ctx, a);
        return JS_EXCEPTION;
    }
    s1 = JS_GetOpaque(a, js_plaintime_class_id);
    s2 = JS_GetOpaque(b, js_plaintime_class_id);
    if (!s1 || !s2) {
        JS_FreeValue(ctx, a);
        JS_FreeValue(ctx, b);
        return JS_ThrowTypeError(ctx, "compare arguments must be PlainTime objects");
    }
    if (s1->hour != s2->hour) {
        cmp = s1->hour < s2->hour ? -1 : 1;
    } else if (s1->minute != s2->minute) {
        cmp = s1->minute < s2->minute ? -1 : 1;
    } else if (s1->second != s2->second) {
        cmp = s1->second < s2->second ? -1 : 1;
    } else if (s1->millisecond != s2->millisecond) {
        cmp = s1->millisecond < s2->millisecond ? -1 : 1;
    } else if (s1->microsecond != s2->microsecond) {
        cmp = s1->microsecond < s2->microsecond ? -1 : 1;
    } else if (s1->nanosecond != s2->nanosecond) {
        cmp = s1->nanosecond < s2->nanosecond ? -1 : 1;
    } else {
        cmp = 0;
    }
    JS_FreeValue(ctx, a);
    JS_FreeValue(ctx, b);
    return JS_NewInt32(ctx, cmp);
}

/* Helper: convert PlainTime to total nanoseconds from midnight */
static int64_t pt_to_nanoseconds(const JSPlainTimeData *t)
{
    return ((int64_t)t->hour * 3600000000000LL +
            (int64_t)t->minute * 60000000000LL +
            (int64_t)t->second * 1000000000LL +
            (int64_t)t->millisecond * 1000000LL +
            (int64_t)t->microsecond * 1000LL +
            (int64_t)t->nanosecond);
}

/* Helper: convert nanoseconds from midnight to PlainTime */
static void pt_from_nanoseconds(int64_t ns, JSPlainTimeData *t)
{
    int64_t rem;
    t->hour = (int)(ns / 3600000000000LL);
    rem = ns % 3600000000000LL;
    if (rem < 0) {
        t->hour -= 1;
        rem += 3600000000000LL;
    }
    t->minute = (int)(rem / 60000000000LL);
    rem = rem % 60000000000LL;
    t->second = (int)(rem / 1000000000LL);
    rem = rem % 1000000000LL;
    t->millisecond = (int)(rem / 1000000LL);
    rem = rem % 1000000LL;
    t->microsecond = (int)(rem / 1000LL);
    t->nanosecond = (int)(rem % 1000LL);
}

/* Temporal.PlainTime.prototype.add */
static JSValue js_plaintime_add_subtract(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv,
                                          int is_add)
{
    (void)argc;
    JSPlainTimeData *s;
    JSDurationData d;
    int64_t result_ns;
    JSPlainTimeData result;
    int sign = is_add ? 1 : -1;
    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (js_to_temporal_duration(ctx, argv[0], &d) < 0)
        return JS_EXCEPTION;
    if (duration_has_mixed_signs(&d))
        return JS_ThrowRangeError(ctx, "mixed signs in duration fields");
    if (duration_is_out_of_range(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    {
        js_int128_t total = (js_int128_t)pt_to_nanoseconds(s) +
            sign * ti_duration_time_fields_to_ns(&d);
        /* Wrap around 24 hours */
        total = ((total % 86400000000000LL) + 86400000000000LL) % 86400000000000LL;
        result_ns = (int64_t)total;
    }
    pt_from_nanoseconds(result_ns, &result);
    return js_create_plaintime(ctx, &result);
}

/* Temporal.PlainTime.prototype.round */
static JSValue js_plaintime_round(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainTimeData *s;
    JSValue options = JS_UNDEFINED;
    JSValue v = JS_UNDEFINED;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    TIRoundingMode mode = ROUND_HALF_EXPAND;
    int64_t increment = 1;
    int64_t total_ns, rounded_ns, unit_ns;
    JSPlainTimeData result;

    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsString(argv[0])) {
        size_t len;
        const char *str = JS_ToCStringLen(ctx, &len, argv[0]);
        if (!str)
            return JS_EXCEPTION;
        options = JS_NewObjectProto(ctx, JS_NULL);
        JS_SetPropertyStr(ctx, options, "smallestUnit", JS_NewStringLen(ctx, str, len));
        JS_FreeCString(ctx, str);
    } else if (!JS_IsObject(argv[0])) {
        return JS_ThrowTypeError(ctx, "round options must be an object or string");
    } else {
        options = JS_DupValue(ctx, argv[0]);
    }

    if (ti_get_rounding_increment_option(ctx, options, &increment) < 0) {
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    if (ti_get_rounding_mode_option(ctx, options, &mode) < 0) {
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    if (ti_parse_unit_option(ctx, options, "smallestUnit", &smallestUnit, 0, 0) < 0) {
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    if (smallestUnit < TI_UNIT_HOUR || smallestUnit > TI_UNIT_NANOSECOND) {
        JS_ThrowRangeError(ctx, "invalid smallestUnit");
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    /* Validate roundingIncrement against per-unit maximum (inclusive = false) */
    {
        int64_t maximum;
        switch (smallestUnit) {
        case TI_UNIT_HOUR:        maximum = 24; break;
        case TI_UNIT_MINUTE:      maximum = 60; break;
        case TI_UNIT_SECOND:      maximum = 60; break;
        case TI_UNIT_MILLISECOND: maximum = 1000; break;
        case TI_UNIT_MICROSECOND: maximum = 1000; break;
        case TI_UNIT_NANOSECOND:  maximum = 1000; break;
        default:                  maximum = 0; break;
        }
        if (increment >= maximum || maximum % increment != 0) {
            JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
            goto round_fail;
        }
    }



    total_ns = pt_to_nanoseconds(s);

    switch (smallestUnit) {
    case TI_UNIT_HOUR: unit_ns = 3600000000000LL; break;
    case TI_UNIT_MINUTE: unit_ns = 60000000000LL; break;
    case TI_UNIT_SECOND: unit_ns = 1000000000LL; break;
    case TI_UNIT_MILLISECOND: unit_ns = 1000000LL; break;
    case TI_UNIT_MICROSECOND: unit_ns = 1000LL; break;
    case TI_UNIT_NANOSECOND: unit_ns = 1LL; break;
    default: unit_ns = 1LL; break;
    }

    rounded_ns = ti_round_to_increment(total_ns, unit_ns * increment, mode);
    /* Wrap around midnight for PlainTime */
    rounded_ns = rounded_ns % 86400000000000LL;
    if (rounded_ns < 0)
        rounded_ns += 86400000000000LL;
    pt_from_nanoseconds(rounded_ns, &result);
    JS_FreeValue(ctx, options);
    return js_create_plaintime(ctx, &result);

round_fail:
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, options);
    return JS_EXCEPTION;
}

/* Helper: convert nanoseconds to JSDurationData for PlainTime difference */
static void pt_ns_to_duration(int64_t ns, TIUnit largestUnit, JSDurationData *d)
{
    int64_t rem;
    int sign = (ns < 0) ? -1 : 1;
    memset(d, 0, sizeof(*d));
    if (ns < 0)
        ns = -ns;

    if (largestUnit == TI_UNIT_HOUR) {
        d->hours = sign * (ns / 3600000000000LL);
        rem = ns % 3600000000000LL;
    } else {
        d->hours = 0;
        rem = ns;
    }

    if (largestUnit == TI_UNIT_MINUTE) {
        d->minutes = sign * (rem / 60000000000LL);
        rem = rem % 60000000000LL;
    } else if (largestUnit == TI_UNIT_HOUR) {
        d->minutes = sign * (rem / 60000000000LL);
        rem = rem % 60000000000LL;
    } else {
        d->minutes = 0;
    }

    if (largestUnit == TI_UNIT_SECOND) {
        d->seconds = sign * (rem / 1000000000LL);
        rem = rem % 1000000000LL;
    } else if (largestUnit == TI_UNIT_HOUR || largestUnit == TI_UNIT_MINUTE) {
        d->seconds = sign * (rem / 1000000000LL);
        rem = rem % 1000000000LL;
    } else {
        d->seconds = 0;
    }

    if (largestUnit == TI_UNIT_MILLISECOND) {
        d->milliseconds = sign * (rem / 1000000LL);
        rem = rem % 1000000LL;
    } else if (largestUnit < TI_UNIT_MILLISECOND) {
        d->milliseconds = sign * (rem / 1000000LL);
        rem = rem % 1000000LL;
    } else {
        d->milliseconds = 0;
    }

    if (largestUnit == TI_UNIT_MICROSECOND) {
        d->microseconds.int128_value = sign * (rem / 1000LL);
        rem = rem % 1000LL;
    } else if (largestUnit < TI_UNIT_MICROSECOND) {
        d->microseconds.int128_value = sign * (rem / 1000LL);
        rem = rem % 1000LL;
    } else {
        d->microseconds.int128_value = 0;
    }

    if (largestUnit == TI_UNIT_NANOSECOND) {
        d->nanoseconds.int128_value = sign * rem;
    } else if (largestUnit < TI_UNIT_NANOSECOND) {
        d->nanoseconds.int128_value = sign * rem;
    } else {
        d->nanoseconds.int128_value = 0;
    }
}

/* Temporal.PlainTime.prototype.until */
static JSValue js_plaintime_difference(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv, int is_since)
{
    JSPlainTimeData *s, *other;
    JSValue other_val = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    JSDurationData d;
    TIUnit smallestUnit = TI_UNIT_INVALID, largestUnit = TI_UNIT_INVALID;
    TIRoundingMode mode = ROUND_TRUNC;
    int64_t increment = 1;
    int64_t this_ns, other_ns, diff_ns;

    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plaintime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaintime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_EXCEPTION;
    }

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (!JS_IsObject(argv[1])) {
            JS_FreeValue(ctx, other_val);
            return JS_ThrowTypeError(ctx, "options must be an object");
        }
        options = JS_DupValue(ctx, argv[1]);

        if (ti_parse_unit_option(ctx, options, "largestUnit", &largestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED | TI_UNIT_OPT_ALLOW_AUTO, TI_UNIT_INVALID) < 0)
            goto fail;
        if (ti_get_rounding_increment_option(ctx, options, &increment) < 0)
            goto fail;
        if (ti_get_rounding_mode_option_default(ctx, options, &mode, ROUND_TRUNC) < 0)
            goto fail;
        if (ti_parse_unit_option(ctx, options, "smallestUnit", &smallestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
            goto fail;

        /* Validate unit restrictions */
        if (largestUnit != TI_UNIT_INVALID && largestUnit != TI_UNIT_AUTO && largestUnit < TI_UNIT_HOUR) {
            JS_ThrowRangeError(ctx, "invalid largestUnit");
            goto fail;
        }
        if (smallestUnit != TI_UNIT_INVALID && smallestUnit < TI_UNIT_HOUR) {
            JS_ThrowRangeError(ctx, "invalid smallestUnit");
            goto fail;
        }
        /* Validate roundingIncrement against per-unit maximum (inclusive = false) */
        {
            int64_t maximum;
            TIUnit eff_su = (smallestUnit == TI_UNIT_INVALID) ? TI_UNIT_NANOSECOND : smallestUnit;
            switch (eff_su) {
            case TI_UNIT_HOUR:        maximum = 24; break;
            case TI_UNIT_MINUTE:      maximum = 60; break;
            case TI_UNIT_SECOND:      maximum = 60; break;
            case TI_UNIT_MILLISECOND: maximum = 1000; break;
            case TI_UNIT_MICROSECOND: maximum = 1000; break;
            case TI_UNIT_NANOSECOND:  maximum = 1000; break;
            default:                  maximum = 0; break;
            }
            if (increment >= maximum || maximum % increment != 0) {
                JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
                goto fail;
            }
        }
        /* Validate largestUnit >= smallestUnit in magnitude */
        {
            TIUnit eff_lu = (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO) ? TI_UNIT_HOUR : largestUnit;
            TIUnit eff_su = (smallestUnit == TI_UNIT_INVALID) ? TI_UNIT_NANOSECOND : smallestUnit;
            if (eff_lu > eff_su) {
                JS_ThrowRangeError(ctx, "largestUnit must be larger than smallestUnit");
                goto fail;
            }
        }
        JS_FreeValue(ctx, options);
        options = JS_UNDEFINED;
    }

    this_ns = pt_to_nanoseconds(s);
    other_ns = pt_to_nanoseconds(other);
    diff_ns = is_since ? (this_ns - other_ns) : (other_ns - this_ns);

    {
        int64_t unit_ns;
        TIUnit su = smallestUnit;
        if (su == -1)
            su = TI_UNIT_NANOSECOND;
        switch (su) {
        case TI_UNIT_HOUR: unit_ns = 3600000000000LL; break;
        case TI_UNIT_MINUTE: unit_ns = 60000000000LL; break;
        case TI_UNIT_SECOND: unit_ns = 1000000000LL; break;
        case TI_UNIT_MILLISECOND: unit_ns = 1000000LL; break;
        case TI_UNIT_MICROSECOND: unit_ns = 1000LL; break;
        case TI_UNIT_NANOSECOND: unit_ns = 1LL; break;
        default: unit_ns = 1LL; break;
        }
        diff_ns = ti_round_to_increment(diff_ns, unit_ns * increment, mode);
    }

    if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO)
        largestUnit = TI_UNIT_HOUR;
    pt_ns_to_duration(diff_ns, largestUnit, &d);
    JS_FreeValue(ctx, other_val);
    return js_create_duration(ctx, &d);

fail:
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, other_val);
    return JS_EXCEPTION;
}

/* Temporal.PlainTime.from */
JSValue js_plaintime_from(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    (void)this_val;
    JSPlainTimeData t = {0};
    JSValue val, result = JS_UNDEFINED;
    val = argv[0];
    if (JS_IsObject(val)) {
        JSPlainTimeData *s = JS_GetOpaque(val, js_plaintime_class_id);
        if (s) {
            result = js_create_plaintime(ctx, s);
        }
        if (JS_IsUndefined(result)) {
            JSPlainDateTimeData *pdt = JS_GetOpaque(val, js_plaindatetime_class_id);
            if (pdt) {
                JSPlainTimeData pt;
                pt.hour = pdt->hour;
                pt.minute = pdt->minute;
                pt.second = pdt->second;
                pt.millisecond = pdt->millisecond;
                pt.microsecond = pdt->microsecond;
                pt.nanosecond = pdt->nanosecond;
                result = js_create_plaintime(ctx, &pt);
            }
        }
        if (JS_IsUndefined(result)) {
            JSZonedDateTimeData *zdt = JS_GetOpaque(val, js_zoneddatetime_class_id);
            if (zdt) {
                JSPlainTimeData pt;
                /* Convert epoch nanoseconds to time of day, applying timezone offset */
                js_int128_t ns = ti_timespec_to_ns(&zdt->ts);
                {
                    int64_t offset_ns = 0;
                    int ret = ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                    if (ret == 0) {
                        ns += offset_ns;
                    }
                }
                {
                    int64_t day_ns = 86400000000000LL;
                    int64_t rem = (int64_t)(ns % day_ns);
                    if (rem < 0) rem += day_ns;
                    pt_from_nanoseconds(rem, &pt);
                }
                result = js_create_plaintime(ctx, &pt);
            }
        }
        if (!JS_IsUndefined(result)) {
            /* Read options for PlainTime/PlainDateTime/ZonedDateTime instances */
            if (argc >= 2) {
                if (JS_IsNull(argv[1])) {
                    JS_FreeValue(ctx, result);
                    return JS_ThrowTypeError(ctx, "options must be an object");
                }
                if (!JS_IsUndefined(argv[1])) {
                    if (!JS_IsObject(argv[1])) {
                        JS_FreeValue(ctx, result);
                        return JS_ThrowTypeError(ctx, "options must be an object");
                    }
                    {
                        int oc = 1;
                        if (ti_get_overflow_option(ctx, argv[1], &oc) < 0) {
                            JS_FreeValue(ctx, result);
                            return JS_EXCEPTION;
                        }
                    }
                }
            }
            return result;
        }
        /* Property bag */
        {
            TIPreparedFields fields;
            if (ti_prepare_calendar_fields(ctx, val,
                                            TI_FIELD_HOUR | TI_FIELD_MINUTE | TI_FIELD_SECOND |
                                            TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND | TI_FIELD_NANOSECOND,
                                            0, 0, &fields, CAL_ISO8601) < 0)
                return JS_EXCEPTION;
            if (!fields.present_mask)
                return JS_ThrowTypeError(ctx, "invalid PlainTime");
            if (fields.present_mask & TI_FIELD_HOUR) t.hour = fields.hour;
            if (fields.present_mask & TI_FIELD_MINUTE) t.minute = fields.minute;
            if (fields.present_mask & TI_FIELD_SECOND) t.second = fields.second;
            if (fields.present_mask & TI_FIELD_MILLISECOND) t.millisecond = fields.millisecond;
            if (fields.present_mask & TI_FIELD_MICROSECOND) t.microsecond = fields.microsecond;
            if (fields.present_mask & TI_FIELD_NANOSECOND) t.nanosecond = fields.nanosecond;
        }
        /* Now read options */
        {
            int overflow_reject = 0;
            if (argc >= 2) {
                if (JS_IsNull(argv[1]))
                    return JS_ThrowTypeError(ctx, "options must be an object");
                if (!JS_IsUndefined(argv[1])) {
                    if (!JS_IsObject(argv[1]))
                        return JS_ThrowTypeError(ctx, "options must be an object");
                    {
                        int oc = 1;
                        if (ti_get_overflow_option(ctx, argv[1], &oc) < 0)
                            return JS_EXCEPTION;
                        if (!oc)
                            overflow_reject = 1;
                    }
                }
            }
            /* Handle leap second: second 60 is normalized to 59 with constrain, rejected with reject */
            if (t.second == 60) {
                if (overflow_reject)
                    return JS_ThrowRangeError(ctx, "invalid PlainTime");
                t.second = 59;
            }
            if (!overflow_reject) {
                if (t.hour < 0) t.hour = 0;
                if (t.hour > 23) t.hour = 23;
                if (t.minute < 0) t.minute = 0;
                if (t.minute > 59) t.minute = 59;
                if (t.second < 0) t.second = 0;
                if (t.second > 59) t.second = 59;
                if (t.millisecond < 0) t.millisecond = 0;
                if (t.millisecond > 999) t.millisecond = 999;
                if (t.microsecond < 0) t.microsecond = 0;
                if (t.microsecond > 999) t.microsecond = 999;
                if (t.nanosecond < 0) t.nanosecond = 0;
                if (t.nanosecond > 999) t.nanosecond = 999;
            }
        }
        if (!pt_is_valid_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond, t.nanosecond))
            return JS_ThrowRangeError(ctx, "invalid PlainTime");
        return js_create_plaintime(ctx, &t);
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        TISODateTimeParseResult r = {0};
        if (!str)
            return JS_EXCEPTION;
        if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_PLAIN_TIME) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainTime string");
        }
        JS_FreeCString(ctx, str);
        if (r.second == 60) r.second = 59;
        if (!pt_is_valid_time(r.hour, r.minute, r.second, r.millisecond, r.microsecond, r.nanosecond))
            return JS_ThrowRangeError(ctx, "invalid PlainTime");
        if (argc >= 2) {
            if (JS_IsNull(argv[1]))
                return JS_ThrowTypeError(ctx, "options must be an object");
            if (!JS_IsUndefined(argv[1])) {
                if (!JS_IsObject(argv[1]))
                    return JS_ThrowTypeError(ctx, "options must be an object");
                {
                    int oc = 1;
                    if (ti_get_overflow_option(ctx, argv[1], &oc) < 0)
                        return JS_EXCEPTION;
                }
            }
        }
        t.hour = r.hour;
        t.minute = r.minute;
        t.second = r.second;
        t.millisecond = r.millisecond;
        t.microsecond = r.microsecond;
        t.nanosecond = r.nanosecond;
        return js_create_plaintime(ctx, &t);
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.PlainTime.from");
}

/* Temporal.PlainTime.prototype.with */
static JSValue js_plaintime_with(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSPlainTimeData *s;
    JSPlainTimeData t;
    TIPreparedFields fields;
    int overflow_reject = 0;
    s = JS_GetOpaque2(ctx, this_val, js_plaintime_class_id);
    if (!s)
        return JS_EXCEPTION;
    t = *s;
    if (ti_is_partial_temporal_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, argv[0],
                                    TI_FIELD_HOUR | TI_FIELD_MINUTE | TI_FIELD_SECOND |
                                    TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND | TI_FIELD_NANOSECOND,
                                    0, 1, &fields, CAL_ISO8601) < 0)
        return JS_EXCEPTION;
    /* Merge fields */
    if (fields.present_mask & TI_FIELD_HOUR) t.hour = fields.hour;
    if (fields.present_mask & TI_FIELD_MINUTE) t.minute = fields.minute;
    if (fields.present_mask & TI_FIELD_SECOND) t.second = fields.second;
    if (fields.present_mask & TI_FIELD_MILLISECOND) t.millisecond = fields.millisecond;
    if (fields.present_mask & TI_FIELD_MICROSECOND) t.microsecond = fields.microsecond;
    if (fields.present_mask & TI_FIELD_NANOSECOND) t.nanosecond = fields.nanosecond;
    /* Read and validate overflow option */
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (!JS_IsObject(argv[1]))
            return JS_ThrowTypeError(ctx, "options must be an object");
        {
            int oc = 1;
            if (ti_get_overflow_option(ctx, argv[1], &oc) < 0)
                return JS_EXCEPTION;
            if (!oc)
                overflow_reject = 1;
        }
    }
    /* Handle leap second */
    if (t.second == 60) {
        if (overflow_reject)
            return JS_ThrowRangeError(ctx, "invalid PlainTime");
        t.second = 59;
    }
    if (overflow_reject) {
        if (!pt_is_valid_time(t.hour, t.minute, t.second, t.millisecond, t.microsecond, t.nanosecond))
            return JS_ThrowRangeError(ctx, "invalid PlainTime");
    } else {
        /* constrain */
        if (t.hour < 0) t.hour = 0;
        if (t.hour > 23) t.hour = 23;
        if (t.minute < 0) t.minute = 0;
        if (t.minute > 59) t.minute = 59;
        if (t.second < 0) t.second = 0;
        if (t.second > 59) t.second = 59;
        if (t.millisecond < 0) t.millisecond = 0;
        if (t.millisecond > 999) t.millisecond = 999;
        if (t.microsecond < 0) t.microsecond = 0;
        if (t.microsecond > 999) t.microsecond = 999;
        if (t.nanosecond < 0) t.nanosecond = 0;
        if (t.nanosecond > 999) t.nanosecond = 999;
    }
    return js_create_plaintime(ctx, &t);
}

static const JSCFunctionListEntry js_plaintime_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("hour", js_plaintime_get_field, NULL, PLAINTIME_HOUR),
    JS_CGETSET_MAGIC_DEF("minute", js_plaintime_get_field, NULL, PLAINTIME_MINUTE),
    JS_CGETSET_MAGIC_DEF("second", js_plaintime_get_field, NULL, PLAINTIME_SECOND),
    JS_CGETSET_MAGIC_DEF("millisecond", js_plaintime_get_field, NULL, PLAINTIME_MILLISECOND),
    JS_CGETSET_MAGIC_DEF("microsecond", js_plaintime_get_field, NULL, PLAINTIME_MICROSECOND),
    JS_CGETSET_MAGIC_DEF("nanosecond", js_plaintime_get_field, NULL, PLAINTIME_NANOSECOND),
    JS_CFUNC_DEF("equals", 1, js_plaintime_equals),
    JS_CFUNC_DEF("toString", 0, js_plaintime_toString),
    JS_CFUNC_DEF("toJSON", 0, js_plaintime_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_plaintime_toLocaleString),
    JS_CFUNC_DEF("with", 1, js_plaintime_with),
    JS_CFUNC_MAGIC_DEF("add", 1, js_plaintime_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_plaintime_add_subtract, 0),
    JS_CFUNC_DEF("round", 1, js_plaintime_round),
    JS_CFUNC_MAGIC_DEF("until", 1, js_plaintime_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, js_plaintime_difference, 1),
    JS_CFUNC_DEF("valueOf", 0, js_plaintime_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_plaintime_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_plaintime_from),
    JS_CFUNC_DEF("compare", 2, js_plaintime_compare),
};

int js_add_plaintime(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_plaintime_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_plaintime_class_id, &js_plaintime_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_plaintime_class_id, "PlainTime",
                               (JSCFunction *)js_plaintime_constructor, 0,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_plaintime_static_funcs, countof(js_plaintime_static_funcs),
                               js_plaintime_proto_funcs, countof(js_plaintime_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "PlainTime", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
