/*
 * QuickJS Temporal.Instant implementation
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"
#include "timezone.h"

JSClassID js_instant_class_id;

static void js_instant_finalizer(JSRuntime *rt, JSValue val)
{
    JSInstantData *s = JS_GetOpaque(val, js_instant_class_id);
    if (s) {
        js_free_rt(rt, s);
    }
}

static const JSClassDef js_instant_class = {
    "Temporal.Instant",
    .finalizer = js_instant_finalizer,
};

JSValue js_create_instant(JSContext *ctx, const JSTimeSpec *ts)
{
    JSInstantData *s;
    JSValue obj;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    s->ts = *ts;
    obj = JS_NewObjectClass(ctx, (int)js_instant_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

/* --- Date math helpers --- */

static int ti_is_leap_year(int64_t y)
{
    return !(y % 4) - !(y % 100) + !(y % 400);
}

static int ti_days_in_month(int m, int is_leap)
{
    if (m == 2 && is_leap)
        return 29;
    return ti_iso_month_days[m];
}

static int64_t ti_days_from_year(int64_t y)
{
    return 365 * (y - 1970) + floor_div64(y - 1969, 4) -
        floor_div64(y - 1901, 100) + floor_div64(y - 1601, 400);
}

static int ti_is_valid_date(int64_t y, int m, int d)
{
    if (m < 1 || m > 12 || d < 1)
        return 0;
    return d <= ti_days_in_month(m, ti_is_leap_year(y));
}

/* Convert parsed ISO date/time to epoch nanoseconds.
   tz_offset_ns is the offset from the string (positive = east of UTC).
   UTC = local - offset, so we subtract the offset. */
static int ti_iso_to_epoch_ns(JSContext *ctx, int64_t year, int month, int day,
                               int hour, int minute, int second,
                               int ms, int us, int ns,
                               int64_t tz_offset_ns, JSTimeSpec *ts)
{
    int64_t doy = day - 1;
    int i, is_leap = ti_is_leap_year(year);
    int64_t epoch_sec, frac_ns;
    
    for (i = 1; i < month; i++)
        doy += ti_days_in_month(i, is_leap);
    
    epoch_sec = (ti_days_from_year(year) + doy) * 86400LL;
    epoch_sec += hour * 3600LL + minute * 60LL + second;
    
    /* Apply timezone offset: UTC = local - offset */
    epoch_sec -= tz_offset_ns / 1000000000LL;
    frac_ns = (int64_t)ms * 1000000 + (int64_t)us * 1000 + ns
              - (tz_offset_ns % 1000000000LL);
    
    /* Normalize */
    ts->secs = epoch_sec + frac_ns / 1000000000LL;
    ts->nsecs = (uint32_t)(frac_ns % 1000000000LL);
    if (frac_ns < 0 && ts->nsecs != 0) {
        ts->nsecs = (uint32_t)(ts->nsecs + 1000000000LL);
        ts->secs -= 1;
    }
    
    if (!is_valid_epoch_timespec(ts)) {
        JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
        return -1;
    }
    return 0;
}

/* --- Main ISO instant string parser --- */

static int ti_parse_instant_string(JSContext *ctx, const char *str, JSTimeSpec *ts)
{
    TISODateTimeParseResult r;
    int64_t tz_offset_ns = 0;

    if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0)
        goto fail;

    /* Instant strings must have a timezone indicator (Z or offset) */
    if (!r.has_utc && !r.has_tz_offset)
        goto fail;

    /* Instant strings must have a date and time */
    if (!r.has_date || !r.has_time)
        goto fail;

    /* Reject year-month only or month-day only strings */
    if (r.has_yearmonth || r.has_monthday)
        goto fail;

    if (r.has_utc) {
        tz_offset_ns = 0;
    } else if (r.has_tz_offset) {
        tz_offset_ns = (int64_t)r.tz_offset_seconds * 1000000000LL + r.tz_offset_nanoseconds;
    }

    if (!ti_is_valid_date(r.year, r.month, r.day))
        goto fail;

    return ti_iso_to_epoch_ns(ctx, r.year, r.month, r.day,
                               r.hour, r.minute, r.second,
                               r.millisecond, r.microsecond, r.nanosecond,
                               tz_offset_ns, ts);

fail:
    JS_ThrowRangeError(ctx, "invalid ISO string for Temporal.Instant");
    return -1;
}

/* JSValue wrapper for ti_parse_temporal_timezone_string.
   Returns JS_UNDEFINED on success, or throws JS_EXCEPTION on error. */
static JSValue ti_parse_temporal_timezone_string_value(JSContext *ctx, JSValue val, int64_t *offset_ns)
{
    const char *str;
    int ret;

    if (!JS_IsString(val))
        return JS_ThrowTypeError(ctx, "timeZone is not a valid string");
    str = JS_ToCString(ctx, val);
    if (!str)
        return JS_EXCEPTION;
    ret = ti_parse_temporal_timezone_string(str, offset_ns);
    JS_FreeCString(ctx, str);
    if (ret == -1)
        return JS_ThrowRangeError(ctx, "invalid time zone");
    return JS_UNDEFINED;
}

/* Return a canonical time zone identifier string for a time zone value.
   Returns a JSValue string on success, JS_EXCEPTION on error.
   This handles strings like "UTC", "utc", "+01:00", "2021-08-19T17:30-07:00",
   "2021-08-19T17:30Z", "2021-08-19T17:30[UTC]", etc.
   Bracket annotation takes precedence over prefix offset. */
static JSValue ti_canonicalize_timezone_string(JSContext *ctx, JSValue val)
{
    const char *str;
    size_t len;
    JSValue result = JS_UNDEFINED;

    if (!JS_IsString(val))
        return JS_UNDEFINED;

    str = JS_ToCStringLen(ctx, &len, val);
    if (!str)
        return JS_EXCEPTION;

    /* Bracket annotation takes precedence */
    const char *bracket = strchr(str, '[');
    if (bracket) {
        const char *close = strchr(bracket, ']');
        if (close) {
            int inner_len = (int)(close - bracket - 1);
            if (inner_len == 3 && (bracket[1] == 'U' || bracket[1] == 'u') &&
                (bracket[2] == 'T' || bracket[2] == 't') &&
                (bracket[3] == 'C' || bracket[3] == 'c')) {
                result = JS_NewString(ctx, "UTC");
            } else if (inner_len == 6 && (bracket[1] == '+' || bracket[1] == '-')) {
                char buf[8];
                memcpy(buf, bracket + 1, 6);
                buf[6] = '\0';
                result = JS_NewString(ctx, buf);
            } else if (inner_len > 0) {
                /* IANA name */
                result = JS_NewStringLen(ctx, bracket + 1, (size_t)inner_len);
            }
        }
    } else if (len == 3 && (str[0] == 'U' || str[0] == 'u') &&
               (str[1] == 'T' || str[1] == 't') &&
               (str[2] == 'C' || str[2] == 'c')) {
        result = JS_NewString(ctx, "UTC");
    } else if (len == 6 && (str[0] == '+' || str[0] == '-')) {
        result = JS_NewString(ctx, str);
    } else if (len == 5 && (str[0] == '+' || str[0] == '-')) {
        /* ±HHMM format - convert to ±HH:MM */
        char buf[7];
        buf[0] = str[0];
        buf[1] = str[1];
        buf[2] = str[2];
        buf[3] = ':';
        buf[4] = str[3];
        buf[5] = str[4];
        buf[6] = '\0';
        result = JS_NewString(ctx, buf);
    } else if (len > 1 && str[len - 1] == 'Z') {
        result = JS_NewString(ctx, "UTC");
    } else {
        /* Plain IANA name */
        int valid_iana = 1;
        size_t i;
        for (i = 0; i < len; i++) {
            char c = str[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-')) {
                valid_iana = 0;
                break;
            }
        }
        if (valid_iana && len > 0) {
            result = JS_NewString(ctx, str);
        } else {
            /* Find offset at end: ±HH:MM */
            int j;
            for (j = (int)len - 6; j >= 0; j--) {
                if ((str[j] == '+' || str[j] == '-') && str[j + 3] == ':') {
                    int h1 = str[j + 1] - '0';
                    int h2 = str[j + 2] - '0';
                    int m1 = str[j + 4] - '0';
                    int m2 = str[j + 5] - '0';
                    if (h1 >= 0 && h1 <= 9 && h2 >= 0 && h2 <= 9 &&
                        m1 >= 0 && m1 <= 5 && m2 >= 0 && m2 <= 9) {
                        char buf[7];
                        memcpy(buf, str + j, 6);
                        buf[6] = '\0';
                        result = JS_NewString(ctx, buf);
                        break;
                    }
                }
            }
            if (JS_IsUndefined(result)) {
                /* Find offset at end: ±HHMM */
                int k;
                for (k = (int)len - 5; k >= 0; k--) {
                    if (str[k] == '+' || str[k] == '-') {
                        int h1 = str[k + 1] - '0';
                        int h2 = str[k + 2] - '0';
                        int m1 = str[k + 3] - '0';
                        int m2 = str[k + 4] - '0';
                        if (h1 >= 0 && h1 <= 9 && h2 >= 0 && h2 <= 9 &&
                            m1 >= 0 && m1 <= 5 && m2 >= 0 && m2 <= 9) {
                            char buf[7];
                            buf[0] = str[k];
                            buf[1] = str[k + 1];
                            buf[2] = str[k + 2];
                            buf[3] = ':';
                            buf[4] = str[k + 3];
                            buf[5] = str[k + 4];
                            buf[6] = '\0';
                            result = JS_NewString(ctx, buf);
                            break;
                        }
                    }
                }
            }
        }
    }

    JS_FreeCString(ctx, str);
    return result;
}

/* ToTemporalInstant(item): converts item to Temporal.Instant */
static JSValue ti_to_temporal_instant(JSContext *ctx, JSValueConst item)
{
    JSInstantData *s;
    const char *str;
    JSTimeSpec ts;
    JSValue prim, r;
    
    if (JS_IsObject(item)) {
        s = JS_GetOpaque(item, js_instant_class_id);
        if (s)
            return js_create_instant(ctx, &s->ts);
        /* Fast path for ZonedDateTime */
        if (js_zoneddatetime_get_timespec(ctx, item, &ts) == 0)
            return js_create_instant(ctx, &ts);
        /* Convert to primitive string */
        prim = JS_ToPrimitive(ctx, item, JS_HINT_STRING);
        if (JS_IsException(prim))
            return JS_EXCEPTION;
        if (!JS_IsString(prim)) {
            JS_FreeValue(ctx, prim);
            return JS_ThrowTypeError(ctx, "cannot convert to Temporal.Instant");
        }
        str = JS_ToCString(ctx, prim);
        if (!str) {
            JS_FreeValue(ctx, prim);
            return JS_EXCEPTION;
        }
        r = (ti_parse_instant_string(ctx, str, &ts) == 0)
            ? js_create_instant(ctx, &ts)
            : JS_EXCEPTION;
        JS_FreeCString(ctx, str);
        JS_FreeValue(ctx, prim);
        return r;
    }
    if (!JS_IsString(item))
        return JS_ThrowTypeError(ctx, "cannot convert to Temporal.Instant");
    str = JS_ToCString(ctx, item);
    if (!str)
        return JS_EXCEPTION;
    r = (ti_parse_instant_string(ctx, str, &ts) == 0)
        ? js_create_instant(ctx, &ts)
        : JS_EXCEPTION;
    JS_FreeCString(ctx, str);
    return r;
}

/* --- Instant methods --- */

/* Temporal.Instant constructor */
static JSValue js_instant_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    (void)argc;
    JSValue obj = JS_UNDEFINED;
    JSTimeSpec ts;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.Instant constructor cannot be called as a function");
    }
    if (JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "Temporal.Instant requires an epochNanoseconds argument");
    }
    if (ti_bigint_to_timespec(ctx, argv[0], &ts) < 0)
        return JS_EXCEPTION;
    if (!is_valid_epoch_timespec(&ts)) {
        return JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
    }
    {
        JSInstantData *s = js_mallocz(ctx, sizeof(*s));
        if (!s)
            return JS_EXCEPTION;
        s->ts = ts;
        obj = js_create_temporal_object(ctx, new_target, js_instant_class_id, s);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        return obj;
    }
}

/* Temporal.Instant.fromEpochMilliseconds */
/* NumberToBigInt semantics: throws RangeError for non-integral, NaN, Infinity, undefined */
static int js_number_to_int64(JSContext *ctx, JSValueConst val, int64_t *pres)
{
    double d;
    if (JS_ToFloat64(ctx, &d, val))
        return -1;
    if (!isfinite(d) || floor(d) != d) {
        JS_ThrowRangeError(ctx, "number cannot be converted to BigInt");
        return -1;
    }
    *pres = (int64_t)d;
    return 0;
}

static void ms_to_timespec(int64_t ms, JSTimeSpec *ts)
{
    ts->secs = ms / 1000;
    int64_t rem = (ms % 1000) * 1000000;
    if (rem < 0) {
        rem += 1000000000;
        ts->secs -= 1;
    }
    ts->nsecs = (uint32_t)rem;
}

static JSValue js_instant_fromEpochMilliseconds(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    (void)this_val;
    int64_t ms;
    JSTimeSpec ts;
    JSValueConst val = argc > 0 ? argv[0] : JS_UNDEFINED;
    if (js_number_to_int64(ctx, val, &ms) < 0)
        return JS_EXCEPTION;
    ms_to_timespec(ms, &ts);
    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "epochMilliseconds out of range");
    return js_create_instant(ctx, &ts);
}

/* Temporal.Instant.fromEpochNanoseconds */
static JSValue js_instant_fromEpochNanoseconds(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSTimeSpec ts;
    if (ti_bigint_to_timespec(ctx, argv[0], &ts) < 0)
        return JS_EXCEPTION;
    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
    return js_create_instant(ctx, &ts);
}

/* Temporal.Instant.compare */
static JSValue js_instant_compare(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSValue a, b;
    JSInstantData *s1, *s2;
    int cmp;
    a = ti_to_temporal_instant(ctx, argv[0]);
    if (JS_IsException(a))
        return JS_EXCEPTION;
    b = ti_to_temporal_instant(ctx, argv[1]);
    if (JS_IsException(b)) {
        JS_FreeValue(ctx, a);
        return JS_EXCEPTION;
    }
    s1 = JS_GetOpaque(a, js_instant_class_id);
    s2 = JS_GetOpaque(b, js_instant_class_id);
    if (s1->ts.secs < s2->ts.secs)
        cmp = -1;
    else if (s1->ts.secs > s2->ts.secs)
        cmp = 1;
    else if (s1->ts.nsecs < s2->ts.nsecs)
        cmp = -1;
    else if (s1->ts.nsecs > s2->ts.nsecs)
        cmp = 1;
    else
        cmp = 0;
    JS_FreeValue(ctx, a);
    JS_FreeValue(ctx, b);
    return JS_NewInt32(ctx, cmp);
}

/* Temporal.Instant.from */
static JSValue js_instant_from(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    return ti_to_temporal_instant(ctx, argv[0]);
}

/* get epochMilliseconds */
static JSValue js_instant_get_epochMilliseconds(JSContext *ctx, JSValueConst this_val)
{
    JSInstantData *s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    int64_t ms;
    if (!s)
        return JS_EXCEPTION;
    ms = s->ts.secs * 1000 + (int64_t)(s->ts.nsecs / 1000000);
    return JS_NewFloat64(ctx, (double)ms);
}

/* get epochNanoseconds */
static JSValue js_instant_get_epochNanoseconds(JSContext *ctx, JSValueConst this_val)
{
    JSInstantData *s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_timespec_to_bigint(ctx, &s->ts);
}

/* Temporal.Instant.prototype.equals */
static JSValue js_instant_equals(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argc;
    JSInstantData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = ti_to_temporal_instant(ctx, argv[0]);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_instant_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowTypeError(ctx, "not an Instant");
    }
    {
        JSTimeSpec s_ts = s->ts;
        int64_t other_secs = other->ts.secs;
        uint32_t other_nsecs = other->ts.nsecs;
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, s_ts.secs == other_secs && s_ts.nsecs == other_nsecs);
    }
}

/* Temporal.Instant.prototype.valueOf */
static JSValue js_instant_valueOf(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.Instant with < or >");
}

/* Forward declarations */
static JSValue js_instant_toString(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv);

/* --- Rounding helpers --- */

int ti_parse_rounding_mode(JSContext *ctx, JSValueConst val, TIRoundingMode *out)
{
    const char *str;
    if (JS_IsUndefined(val)) {
        *out = ROUND_HALF_EXPAND;
        return 0;
    }
    str = JS_ToCStringNZ(ctx, val);
    if (!str)
        return -1;
    if (!strcmp(str, "ceil"))
        *out = ROUND_CEIL;
    else if (!strcmp(str, "floor"))
        *out = ROUND_FLOOR;
    else if (!strcmp(str, "expand"))
        *out = ROUND_EXPAND;
    else if (!strcmp(str, "trunc"))
        *out = ROUND_TRUNC;
    else if (!strcmp(str, "halfCeil"))
        *out = ROUND_HALF_CEIL;
    else if (!strcmp(str, "halfFloor"))
        *out = ROUND_HALF_FLOOR;
    else if (!strcmp(str, "halfExpand"))
        *out = ROUND_HALF_EXPAND;
    else if (!strcmp(str, "halfTrunc"))
        *out = ROUND_HALF_TRUNC;
    else if (!strcmp(str, "halfEven"))
        *out = ROUND_HALF_EVEN;
    else {
        JS_ThrowRangeError(ctx, "invalid rounding mode");
        JS_FreeCString(ctx, str);
        return -1;
    }
    JS_FreeCString(ctx, str);
    return 0;
}

/* Standard rounding using Euclidean division.
   ceil/floor are mathematical (toward ±∞).
   expand rounds away from zero, trunc rounds toward zero. */
int64_t ti_round_to_increment(int64_t x, int64_t increment, TIRoundingMode mode)
{
    int64_t q, r, q_euclid, r_euclid, floor_q, ceil_q, trunc_q, result;

    if (increment == 1)
        return x;

    q = x / increment;
    r = x % increment;

    /* Convert to Euclidean division: x = q_euclid * inc + r_euclid, 0 <= r_euclid < inc */
    if (r < 0) {
        q_euclid = q - 1;
        r_euclid = r + increment;
    } else {
        q_euclid = q;
        r_euclid = r;
    }

    floor_q = q_euclid;
    ceil_q = (r_euclid == 0) ? q_euclid : q_euclid + 1;
    trunc_q = (x >= 0) ? q_euclid : ((r_euclid == 0) ? q_euclid : q_euclid + 1);

    if (r_euclid == 0)
        return x;

    switch (mode) {
    case ROUND_CEIL:
        result = ceil_q * increment;
        break;
    case ROUND_FLOOR:
        result = floor_q * increment;
        break;
    case ROUND_EXPAND:
        result = (x > 0 ? ceil_q : floor_q) * increment;
        break;
    case ROUND_TRUNC:
        result = trunc_q * increment;
        break;
    case ROUND_HALF_CEIL:
    case ROUND_HALF_FLOOR:
    case ROUND_HALF_EXPAND:
    case ROUND_HALF_TRUNC:
    case ROUND_HALF_EVEN: {
        int64_t dist_floor = r_euclid;
        int64_t dist_ceil = increment - r_euclid;
        int up; /* 1 = ceil, 0 = floor */
        if (dist_floor < dist_ceil) {
            up = 0;
        } else if (dist_floor > dist_ceil) {
            up = 1;
        } else {
            switch (mode) {
            case ROUND_HALF_CEIL:
                up = 1;
                break;
            case ROUND_HALF_FLOOR:
                up = 0;
                break;
            case ROUND_HALF_EXPAND:
                up = (x > 0) ? 1 : 0; /* away from zero: positive→ceil, negative→floor */
                break;
            case ROUND_HALF_TRUNC:
                up = (x > 0) ? 0 : 1; /* toward zero: positive→floor, negative→ceil */
                break;
            case ROUND_HALF_EVEN:
                up = (floor_q % 2 == 0) ? 0 : 1;
                break;
            default:
                up = 0;
                break;
            }
        }
        result = up ? (ceil_q * increment) : (floor_q * increment);
        break;
    }
    default:
        result = x;
        break;
    }
    return result;
}

js_int128_t ti_round_to_increment128(js_int128_t x, js_int128_t increment, TIRoundingMode mode)
{
    js_int128_t q, r, q_euclid, r_euclid, floor_q, ceil_q, trunc_q, result;

    if (increment == 1)
        return x;

    q = x / increment;
    r = x % increment;

    /* Convert to Euclidean division */
    if (r < 0) {
        q_euclid = q - 1;
        r_euclid = r + increment;
    } else {
        q_euclid = q;
        r_euclid = r;
    }

    floor_q = q_euclid;
    ceil_q = (r_euclid == 0) ? q_euclid : q_euclid + 1;
    trunc_q = (x >= 0) ? q_euclid : ((r_euclid == 0) ? q_euclid : q_euclid + 1);

    if (r_euclid == 0)
        return x;

    switch (mode) {
    case ROUND_CEIL:
        result = ceil_q * increment;
        break;
    case ROUND_FLOOR:
        result = floor_q * increment;
        break;
    case ROUND_EXPAND:
        result = (x > 0 ? ceil_q : floor_q) * increment;
        break;
    case ROUND_TRUNC:
        result = trunc_q * increment;
        break;
    case ROUND_HALF_CEIL:
    case ROUND_HALF_FLOOR:
    case ROUND_HALF_EXPAND:
    case ROUND_HALF_TRUNC:
    case ROUND_HALF_EVEN: {
        js_int128_t dist_floor = r_euclid;
        js_int128_t dist_ceil = increment - r_euclid;
        int up;
        if (dist_floor < dist_ceil) {
            up = 0;
        } else if (dist_floor > dist_ceil) {
            up = 1;
        } else {
            switch (mode) {
            case ROUND_HALF_CEIL:
                up = 1;
                break;
            case ROUND_HALF_FLOOR:
                up = 0;
                break;
            case ROUND_HALF_EXPAND:
                up = (x > 0) ? 1 : 0;
                break;
            case ROUND_HALF_TRUNC:
                up = (x > 0) ? 0 : 1;
                break;
            case ROUND_HALF_EVEN:
                up = (floor_q % 2 == 0) ? 0 : 1;
                break;
            default:
                up = 0;
                break;
            }
        }
        result = up ? (ceil_q * increment) : (floor_q * increment);
        break;
    }
    default:
        result = x;
        break;
    }
    return result;
}

/* As-if-positive rounding used by toString: treat negative as positive then negate */
int64_t ti_round_to_increment_as_if_positive(int64_t x, int64_t increment, TIRoundingMode mode)
{
    int64_t q, r, floor_q, ceil_q, result;
    int has_remainder;
    
    if (increment == 1)
        return x;
    
    q = x / increment;
    r = x % increment;
    has_remainder = (r != 0);
    
    if (x >= 0) {
        floor_q = q;
        ceil_q = has_remainder ? q + 1 : q;
    } else {
        floor_q = has_remainder ? q - 1 : q;
        ceil_q = q;
    }
    
    if (!has_remainder)
        return x;
    
    switch (mode) {
    case ROUND_CEIL:
    case ROUND_EXPAND:
        result = ceil_q * increment;
        break;
    case ROUND_FLOOR:
    case ROUND_TRUNC:
        result = floor_q * increment;
        break;
    case ROUND_HALF_CEIL:
    case ROUND_HALF_FLOOR:
    case ROUND_HALF_EXPAND:
    case ROUND_HALF_TRUNC:
    case ROUND_HALF_EVEN: {
        int64_t abs_r = r >= 0 ? r : -r;
        int64_t r_euclid = (x >= 0) ? abs_r : (increment - abs_r);
        int64_t dist_floor = r_euclid;
        int64_t dist_ceil = increment - r_euclid;
        int up;
        if (dist_floor < dist_ceil) {
            up = 0;
        } else if (dist_floor > dist_ceil) {
            up = 1;
        } else {
            /* Tie: round as if positive, then negate */
            switch (mode) {
            case ROUND_HALF_CEIL:
                up = 1; /* ceil for positive → negate → ceil for negative */
                break;
            case ROUND_HALF_FLOOR:
                up = 0; /* floor for positive → negate → floor for negative */
                break;
            case ROUND_HALF_EXPAND:
                up = 1; /* away from zero for positive → negate → away from zero for negative */
                break;
            case ROUND_HALF_TRUNC:
                up = 0; /* toward zero for positive → negate → toward zero for negative */
                break;
            case ROUND_HALF_EVEN:
                /* For positive: if floor is even, floor; else ceil.
                   For negative x, floor(|x|/inc) = -ceil_q.
                   If -ceil_q is even (i.e., ceil_q is even), choose ceil_q.
                   Otherwise choose floor_q. */
                up = (ceil_q % 2 == 0) ? 1 : 0;
                break;
            default:
                up = 0;
                break;
            }
        }
        result = up ? (ceil_q * increment) : (floor_q * increment);
        break;
    }
    default:
        result = x;
        break;
    }
    return result;
}

/* js_int128_t variant of AsIfPositive rounding, used to avoid overflow for large epoch nanoseconds */
js_int128_t ti_round_to_increment_as_if_positive128(js_int128_t x, js_int128_t increment, TIRoundingMode mode)
{
    js_int128_t q, r, floor_q, ceil_q, result;
    int has_remainder;

    if (increment == 1)
        return x;

    q = x / increment;
    r = x % increment;
    has_remainder = (r != 0);

    if (x >= 0) {
        floor_q = q;
        ceil_q = has_remainder ? q + 1 : q;
    } else {
        floor_q = has_remainder ? q - 1 : q;
        ceil_q = q;
    }

    if (!has_remainder)
        return x;

    switch (mode) {
    case ROUND_CEIL:
    case ROUND_EXPAND:
        result = ceil_q * increment;
        break;
    case ROUND_FLOOR:
    case ROUND_TRUNC:
        result = floor_q * increment;
        break;
    case ROUND_HALF_CEIL:
    case ROUND_HALF_FLOOR:
    case ROUND_HALF_EXPAND:
    case ROUND_HALF_TRUNC:
    case ROUND_HALF_EVEN: {
        js_int128_t abs_r = r >= 0 ? r : -r;
        js_int128_t r_euclid = (x >= 0) ? abs_r : (increment - abs_r);
        js_int128_t dist_floor = r_euclid;
        js_int128_t dist_ceil = increment - r_euclid;
        int up;
        if (dist_floor < dist_ceil) {
            up = 0;
        } else if (dist_floor > dist_ceil) {
            up = 1;
        } else {
            switch (mode) {
            case ROUND_HALF_CEIL:
                up = 1;
                break;
            case ROUND_HALF_FLOOR:
                up = 0;
                break;
            case ROUND_HALF_EXPAND:
                up = 1;
                break;
            case ROUND_HALF_TRUNC:
                up = 0;
                break;
            case ROUND_HALF_EVEN:
                up = (ceil_q % 2 == 0) ? 1 : 0;
                break;
            default:
                up = 0;
                break;
            }
        }
        result = up ? (ceil_q * increment) : (floor_q * increment);
        break;
    }
    default:
        result = x;
        break;
    }
    return result;
}

static int ti_unit_is_time_unit(TIUnit unit)
{
    return unit >= TI_UNIT_HOUR;
}

/* Convert nanosecond count to Duration with given largestUnit */
static void ti_ns_to_duration(js_int128_t total_ns, TIUnit largestUnit, JSDurationData *d)
{
    js_int128_t abs_ns = total_ns >= 0 ? total_ns : -total_ns;
    int negative = total_ns < 0;

    memset(d, 0, sizeof(*d));

    if (largestUnit <= TI_UNIT_SECOND) {
        int64_t total_seconds = (int64_t)(abs_ns / 1000000000LL);
        int64_t rem_ns = (int64_t)(abs_ns % 1000000000LL);

        if (largestUnit <= TI_UNIT_HOUR) {
            d->hours = total_seconds / 3600;
            total_seconds %= 3600;
        }
        if (largestUnit <= TI_UNIT_MINUTE) {
            d->minutes = total_seconds / 60;
            total_seconds %= 60;
        }
        d->seconds = total_seconds;

        d->milliseconds = rem_ns / 1000000LL;
        rem_ns %= 1000000LL;
        d->microseconds.int128_value = rem_ns / 1000LL;
        d->nanoseconds.int128_value = rem_ns % 1000LL;
    } else {
        if (largestUnit <= TI_UNIT_MILLISECOND) {
            d->milliseconds = (int64_t)(abs_ns / 1000000LL);
            abs_ns %= 1000000LL;
        }
        if (largestUnit <= TI_UNIT_MICROSECOND) {
            d->microseconds.int128_value = (int64_t)(abs_ns / 1000LL);
            abs_ns %= 1000LL;
        }
        if (largestUnit <= TI_UNIT_NANOSECOND) {
            d->nanoseconds.int128_value = (int64_t)abs_ns;
        }
    }

    if (negative) {
        d->hours = -d->hours;
        d->minutes = -d->minutes;
        d->seconds = -d->seconds;
        d->milliseconds = -d->milliseconds;
        d->microseconds.int128_value = -d->microseconds.int128_value;
        d->nanoseconds.int128_value = -d->nanoseconds.int128_value;
    }
}

/* Temporal.Instant.prototype.round */
static JSValue js_instant_round(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    (void)argc;
    JSInstantData *s;
    JSValue options;
    const char *unit_str;
    int64_t unit_ns, increment = 1;
    TIRoundingMode mode = ROUND_TRUNC;
    js_int128_t total_ns, rounded_ns;
    JSTimeSpec result;
    
    s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "round requires an options object");

    /* Accept string as smallestUnit shorthand */
    if (JS_IsString(argv[0])) {
        unit_str = JS_ToCStringNZ(ctx, argv[0]);
        if (!unit_str)
            return JS_EXCEPTION;
        {
            TIUnit u = ti_string_to_unit(unit_str, strlen(unit_str));
            unit_ns = ti_unit_to_ns(u);
        }
        JS_FreeCString(ctx, unit_str);
        if (unit_ns == 0)
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        options = JS_NULL;
        mode = ROUND_HALF_EXPAND;
    } else if (JS_IsObject(argv[0])) {
        options = JS_DupValue(ctx, argv[0]);
        
        if (ti_get_rounding_increment_option(ctx, options, &increment) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        /* roundingMode */
        if (ti_get_rounding_mode_option(ctx, options, &mode) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        
        /* smallestUnit */
        {
            TIUnit smallestUnit;
            if (ti_parse_unit_option(ctx, options, "smallestUnit", &smallestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
            unit_ns = ti_unit_to_ns(smallestUnit);
        }
        
        JS_FreeValue(ctx, options);
    } else {
        return JS_ThrowTypeError(ctx, "round requires an options object");
    }
    
    /* Validate smallestUnit */
    if (unit_ns == 0)
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");
    
    /* Validate roundingIncrement against maximum for smallestUnit */
    {
        int64_t maximum;
        switch (unit_ns) {
        case 3600000000000LL: maximum = 24; break;
        case 60000000000LL:   maximum = 1440; break;
        case 1000000000LL:    maximum = 86400; break;
        case 1000000LL:       maximum = 86400000; break;
        case 1000LL:          maximum = 86400000000LL; break;
        case 1LL:             maximum = 86400000000000LL; break;
        default:              maximum = 0; break;
        }
        if (increment > maximum || maximum % increment != 0) {
            return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
        }
    }
    
    /* Convert instant to total nanoseconds using js_int128_t to avoid overflow */
    total_ns = ti_timespec_to_ns(&s->ts);
    
    /* Round */
    {
        js_int128_t inc_ns = unit_ns * increment;
        rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, inc_ns, mode);
    }
    
    /* Convert back to JSTimeSpec */
    ti_ns_to_timespec(rounded_ns, &result);
    if (!is_valid_epoch_timespec(&result))
        return JS_ThrowRangeError(ctx, "rounded value out of range");
    return js_create_instant(ctx, &result);
}

/* Temporal.Instant.prototype.toJSON */
static JSValue js_instant_toJSON(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_instant_toString(ctx, this_val, 0, NULL);
}

/* Temporal.Instant.prototype.toLocaleString */
static JSValue js_instant_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSInstantData *s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 0, js_instant_toString);
}

/* Convert a JSValue (number or bigint) to js_int128_t. Returns 0 on success, -1 on exception. */
static int js_value_to_int128(JSContext *ctx, JSValueConst val, js_int128_t *res)
{
    if (JS_IsBigInt(ctx, val)) {
        JSValue str = JS_ToString(ctx, val);
        if (JS_IsException(str))
            return -1;
        const char *s = JS_ToCString(ctx, str);
        if (!s) {
            JS_FreeValue(ctx, str);
            return -1;
        }
        js_int128_t v = 0;
        int sign = 1;
        const char *p = s;
        if (*p == '-') {
            sign = -1;
            p++;
        }
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            p++;
        }
        *res = sign * v;
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, str);
        return 0;
    } else {
        double d;
        if (JS_ToFloat64(ctx, &d, val))
            return -1;
        if (!isfinite(d) || floor(d) != d) {
            JS_ThrowRangeError(ctx, "value must be an integer");
            return -1;
        }
        *res = (js_int128_t)d;
        return 0;
    }
}

/* Read duration-like properties from an object and return total nanoseconds as js_int128_t.
   Also validates no date units and no mixed signs. */
static int ti_read_instant_duration_ns(JSContext *ctx, JSValueConst item, js_int128_t *total_ns)
{
    JSValue val;
    js_int128_t ns = 0;
    int has_positive = 0, has_negative = 0;
    int has_any = 0;

    *total_ns = 0;
    if (JS_IsString(item)) {
        const char *str = JS_ToCString(ctx, item);
        JSDurationData d;
        int ret;
        if (!str)
            return -1;
        ret = js_parse_duration_string(ctx, str, &d);
        JS_FreeCString(ctx, str);
        if (ret < 0)
            return -1;
        if (duration_has_mixed_signs(&d)) {
            JS_ThrowRangeError(ctx, "mixed signs in duration fields");
            return -1;
        }
        if (d.years != 0 || d.months != 0 || d.weeks != 0 || d.days != 0) {
            JS_ThrowRangeError(ctx, "date units not allowed for Instant arithmetic");
            return -1;
        }
        ns += (js_int128_t)d.hours * 3600 * 1000000000;
        ns += (js_int128_t)d.minutes * 60 * 1000000000;
        ns += (js_int128_t)d.seconds * 1000000000;
        ns += (js_int128_t)d.milliseconds * 1000000;
        ns += (js_int128_t)d.microseconds.int128_value * 1000;
        ns += (js_int128_t)d.nanoseconds.int128_value;
        *total_ns = ns;
        return 0;
    }
    if (!JS_IsObject(item)) {
        JS_ThrowTypeError(ctx, "expected object");
        return -1;
    }

    /* days */
    val = JS_GetPropertyStr(ctx, item, "days");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v != 0) {
            JS_ThrowRangeError(ctx, "date units not allowed for Instant arithmetic");
            JS_FreeValue(ctx, val);
            return -1;
        }
        JS_FreeValue(ctx, val);
    }
    /* hours */
    val = JS_GetPropertyStr(ctx, item, "hours");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v * 3600 * 1000000000;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* microseconds */
    val = JS_GetPropertyStr(ctx, item, "microseconds");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v * 1000;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* milliseconds */
    val = JS_GetPropertyStr(ctx, item, "milliseconds");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v * 1000000;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* minutes */
    val = JS_GetPropertyStr(ctx, item, "minutes");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v * 60 * 1000000000;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* months */
    val = JS_GetPropertyStr(ctx, item, "months");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v != 0) {
            JS_ThrowRangeError(ctx, "date units not allowed for Instant arithmetic");
            JS_FreeValue(ctx, val);
            return -1;
        }
        JS_FreeValue(ctx, val);
    }
    /* nanoseconds */
    val = JS_GetPropertyStr(ctx, item, "nanoseconds");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* seconds */
    val = JS_GetPropertyStr(ctx, item, "seconds");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v > 0) has_positive = 1;
        if (v < 0) has_negative = 1;
        ns += v * 1000000000;
        has_any = 1;
        JS_FreeValue(ctx, val);
    }
    /* weeks */
    val = JS_GetPropertyStr(ctx, item, "weeks");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v != 0) {
            JS_ThrowRangeError(ctx, "date units not allowed for Instant arithmetic");
            JS_FreeValue(ctx, val);
            return -1;
        }
        JS_FreeValue(ctx, val);
    }
    /* years */
    val = JS_GetPropertyStr(ctx, item, "years");
    if (!JS_IsUndefined(val)) {
        js_int128_t v;
        if (js_value_to_int128(ctx, val, &v) < 0) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        if (v != 0) {
            JS_ThrowRangeError(ctx, "date units not allowed for Instant arithmetic");
            JS_FreeValue(ctx, val);
            return -1;
        }
        JS_FreeValue(ctx, val);
    }

    if (has_positive && has_negative) {
        JS_ThrowRangeError(ctx, "mixed signs in duration fields");
        return -1;
    }
    if (!has_any) {
        JS_ThrowTypeError(ctx, "no duration properties found");
        return -1;
    }

    *total_ns = ns;
    return 0;
}

/* Temporal.Instant.prototype.toZonedDateTimeISO */
static JSValue js_instant_toZonedDateTimeISO(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    (void)argc;
    JSInstantData *s;
    JSValue result;

    s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    /* Validate timeZone */
    {
        int64_t offset_ns;
        JSValue r = ti_parse_temporal_timezone_string_value(ctx, argv[0], &offset_ns);
        if (JS_IsException(r))
            return JS_EXCEPTION;
        JS_FreeValue(ctx, r);
    }

    /* Get canonical time zone identifier */
    {
        JSValue canon = ti_canonicalize_timezone_string(ctx, argv[0]);
        const char *timeZone;
        if (JS_IsException(canon))
            return JS_EXCEPTION;
        if (JS_IsUndefined(canon))
            return JS_ThrowRangeError(ctx, "invalid time zone");
        timeZone = JS_ToCString(ctx, canon);
        if (!timeZone) {
            JS_FreeValue(ctx, canon);
            return JS_EXCEPTION;
        }
        result = js_create_zoneddatetime(ctx, &s->ts, timeZone, CAL_ISO8601);
        JS_FreeCString(ctx, timeZone);
        JS_FreeValue(ctx, canon);
        return result;
    }
}

/* Temporal.Instant.prototype.add */
/* Shared implementation for Instant.prototype.add and subtract.
   is_add: 1 for add, 0 for subtract. */
static JSValue js_instant_add_subtract(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int is_add)
{
    (void)argc;
    (void)ctx;
    JSInstantData *s;
    js_int128_t total_ns;
    JSTimeSpec result;

    s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (ti_read_instant_duration_ns(ctx, argv[0], &total_ns) < 0)
        return JS_EXCEPTION;

    if (!is_add)
        total_ns = -total_ns;

    total_ns += ti_timespec_to_ns(&s->ts);

    if (total_ns < (js_int128_t)(-INSTANT_MAX_SECS) * 1000000000 ||
        total_ns > (js_int128_t)INSTANT_MAX_SECS * 1000000000)
        return JS_ThrowRangeError(ctx, "instant arithmetic overflow");

    ti_ns_to_timespec(total_ns, &result);
    return js_create_instant(ctx, &result);
}

/* Shared implementation for Instant.prototype.until and since.
   is_since: 0 for until (diff = other - this), 1 for since (diff = this - other) */
static JSValue js_instant_difference(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int is_since)
{
    JSInstantData *s, *other;
    JSValue other_val = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    JSValue v = JS_UNDEFINED;
    JSDurationData d;
    TIUnit smallestUnit = TI_UNIT_INVALID, largestUnit = TI_UNIT_INVALID;
    TIRoundingMode mode = ROUND_TRUNC;
    int64_t increment = 1;
    js_int128_t total_ns, rounded_ns;

    s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    if (!s)
        return JS_EXCEPTION;
    /* Convert other first */
    other_val = ti_to_temporal_instant(ctx, argv[0]);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_instant_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_EXCEPTION;
    }

    /* Parse options: GetOptionsObject semantics */
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (!JS_IsObject(argv[1])) {
            JS_FreeValue(ctx, other_val);
            return JS_ThrowTypeError(ctx, "options must be an object");
        }
        options = JS_DupValue(ctx, argv[1]);

        if (ti_parse_unit_option(ctx, options, "largestUnit", &largestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
            goto fail;
        if (ti_get_rounding_increment_option(ctx, options, &increment) < 0)
            goto fail;
        if (ti_get_rounding_mode_option_default(ctx, options, &mode, ROUND_TRUNC) < 0)
            goto fail;
        if (ti_parse_unit_option(ctx, options, "smallestUnit", &smallestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
            goto fail;

        JS_FreeValue(ctx, options);
    }

    /* Apply defaults */
    if (smallestUnit == TI_UNIT_INVALID) {
        smallestUnit = TI_UNIT_NANOSECOND;
        if (largestUnit == TI_UNIT_INVALID)
            largestUnit = TI_UNIT_SECOND;
    } else {
        if (largestUnit == TI_UNIT_INVALID) {
            if (smallestUnit < TI_UNIT_SECOND)
                largestUnit = smallestUnit;
            else
                largestUnit = TI_UNIT_SECOND;
        }
    }

    /* Validate units are time units (not date units) */
    if (largestUnit != TI_UNIT_INVALID && !ti_unit_is_time_unit(largestUnit)) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "invalid largestUnit");
    }
    if (smallestUnit != TI_UNIT_INVALID && !ti_unit_is_time_unit(smallestUnit)) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");
    }

    /* Validate smallestUnit <= largestUnit (fineness) */
    if (smallestUnit < largestUnit) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "smallestUnit must not be larger than largestUnit");
    }

    /* Validate roundingIncrement against maximum for smallestUnit */
    if (smallestUnit != TI_UNIT_INVALID && increment > 1) {
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
        if (maximum > 0) {
            int64_t max_allowed = maximum - 1; /* inclusive = false */
            if (increment > max_allowed || maximum % increment != 0) {
                JS_FreeValue(ctx, other_val);
                return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
            }
        }
    }

    /* Compute diff in nanoseconds using js_int128_t to avoid overflow */
    if (is_since) {
        total_ns = ti_timespec_to_ns(&s->ts);
        total_ns -= ti_timespec_to_ns(&other->ts);
    } else {
        total_ns = ti_timespec_to_ns(&other->ts);
        total_ns -= ti_timespec_to_ns(&s->ts);
    }

    /* Round to smallestUnit */
    {
        int64_t unit_ns = ti_unit_to_ns(smallestUnit);
        int64_t inc_ns = unit_ns * increment;
        rounded_ns = ti_round_to_increment128(total_ns, inc_ns, mode);
    }

    /* Convert to Duration */
    ti_ns_to_duration(rounded_ns, largestUnit, &d);

    JS_FreeValue(ctx, other_val);
    return js_create_duration(ctx, &d);

fail:
    JS_FreeValue(ctx, v);
    JS_FreeValue(ctx, options);
    JS_FreeValue(ctx, other_val);
    return JS_EXCEPTION;
}

/* Temporal.Instant.prototype.until */
/* Temporal.Instant.prototype.subtract */
/* Convert epoch seconds to ISO date components */
void ti_epoch_sec_to_date(int64_t epoch_sec, int64_t *year, int *month, int *day,
                           int *hour, int *minute, int *second)
{
    int64_t days, doy, y, m, d = 1, rem;
    int is_leap;
    static const int mdays_cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    
    days = floor_div64(epoch_sec, 86400ULL);
    rem = epoch_sec - days * 86400LL;
    
    if (hour) {
        *hour = (int)(rem / 3600);
        rem %= 3600;
    }
    if (minute) {
        *minute = (int)(rem / 60);
        rem %= 60;
    }
    if (second)
        *second = (int)(rem % 60);
    
    /* Find year from days since 1970-01-01 */
    y = 1970;
    if (days >= 0) {
        while (days >= 365 + ti_is_leap_year(y)) {
            days -= 365 + ti_is_leap_year(y);
            y++;
        }
    } else {
        while (days < 0) {
            y--;
            days += 365 + ti_is_leap_year(y);
        }
    }
    is_leap = ti_is_leap_year(y);
    doy = days;
    
    /* Find month from day-of-year */
    for (m = 11; m >= 0; m--) {
        int start = mdays_cum[(int)m];
        if (m >= 2)
            start += is_leap;
        if (doy >= start) {
            d = doy - start + 1;
            break;
        }
    }
    
    if (year)
        *year = y;
    if (month)
        *month = (int)m + 1;
    if (day)
        *day = (int)d;
}

/* Format Instant as ISO string */
static JSValue js_instant_toString(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    JSInstantData *s = JS_GetOpaque2(ctx, this_val, js_instant_class_id);
    char buf[64];
    int64_t year;
    int month, day, hour, minute, second;
    int64_t epoch_sec;
    uint32_t nsec;
    int n;
    int64_t unit_ns = 1; /* default nanosecond */
    int show_seconds = 1; /* 1 = show seconds field */
    int frac_digits = -1; /* -1 = auto, 0-9 = explicit */
    int has_smallestUnit = 0;
    int has_timezone = 0;
    int64_t offset_sec = 0;
    char tz_name[64] = "";
    TIRoundingMode mode = ROUND_TRUNC;
    JSValue options = JS_UNDEFINED;
    
    if (!s)
        return JS_EXCEPTION;
    
    /* Parse options if provided */
    if (argc > 0 && !JS_IsUndefined(argv[0])) {
        int fsd_is_auto = 0, fsd_num = 0;
        JSValue v_tz;
        int su_valid = 0;
        
        if (!JS_IsObject(argv[0]))
            return JS_ThrowTypeError(ctx, "options must be an object");
        options = JS_DupValue(ctx, argv[0]);
        
        /* Read all options in spec order before any algorithmic validation */
        /* fractionalSecondDigits */
        {
            int fsd = ti_get_fractional_second_digits_option(ctx, options, &fsd_num);
            if (fsd < 0) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
            if (fsd_num == -1)
                fsd_is_auto = 1;
            else
                fsd_is_auto = 0;
        }

        /* roundingMode */
        if (ti_get_rounding_mode_option_default(ctx, options, &mode, ROUND_TRUNC) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }

        /* smallestUnit */
        {
            TIUnit su = TI_UNIT_INVALID;
            if (ti_parse_unit_option(ctx, options, "smallestUnit", &su, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
            if (su != TI_UNIT_INVALID) {
                has_smallestUnit = 1;
                switch (su) {
                case TI_UNIT_MINUTE:
                    unit_ns = 60000000000LL; show_seconds = 0; frac_digits = 0; su_valid = 1; break;
                case TI_UNIT_SECOND:
                    unit_ns = 1000000000LL; show_seconds = 1; frac_digits = 0; su_valid = 1; break;
                case TI_UNIT_MILLISECOND:
                    unit_ns = 1000000LL; show_seconds = 1; frac_digits = 3; su_valid = 1; break;
                case TI_UNIT_MICROSECOND:
                    unit_ns = 1000LL; show_seconds = 1; frac_digits = 6; su_valid = 1; break;
                case TI_UNIT_NANOSECOND:
                    unit_ns = 1LL; show_seconds = 1; frac_digits = 9; su_valid = 1; break;
                default:
                    break;
                }
            }
        }
        
        /* timeZone */
        v_tz = JS_GetPropertyStr(ctx, options, "timeZone");
        if (JS_IsException(v_tz)) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(v_tz)) {
            int64_t offset_ns;
            const char *tz_str;
            int ret;
            if (!JS_IsString(v_tz)) {
                JS_FreeValue(ctx, v_tz);
                JS_FreeValue(ctx, options);
                return JS_ThrowTypeError(ctx, "timeZone must be a string");
            }
            tz_str = JS_ToCString(ctx, v_tz);
            ret = ti_parse_temporal_timezone_string(tz_str, &offset_ns);
            if (ret < 0) {
                JS_FreeCString(ctx, tz_str);
                JS_FreeValue(ctx, v_tz);
                JS_FreeValue(ctx, options);
                return JS_ThrowRangeError(ctx, "invalid time zone");
            }
            if (offset_ns == 0) {
                /* Named timezone - try full string first, then extract from brackets */
                if (ti_tz_find_id(tz_str) >= 0) {
                    pstrcpy(tz_name, sizeof(tz_name), tz_str);
                } else {
                    const char *bo = strchr(tz_str, '[');
                    const char *bc = bo ? strchr(bo, ']') : NULL;
                    if (bo && bc && bc[1] == '\0' && bc > bo + 1) {
                        int inner_len = (int)(bc - bo - 1);
                        if (inner_len < (int)sizeof(tz_name)) {
                            memcpy(tz_name, bo + 1, (size_t)inner_len);
                            tz_name[inner_len] = '\0';
                        }
                    }
                }
            } else {
                offset_sec = offset_ns / 1000000000;
            }
            has_timezone = 1;
            JS_FreeCString(ctx, tz_str);
            JS_FreeValue(ctx, v_tz);
        } else {
            JS_FreeValue(ctx, v_tz);
        }
        
        JS_FreeValue(ctx, options);
        
        /* Validate smallestUnit after all reads */
        if (has_smallestUnit && !su_valid)
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");

        /* Apply fractionalSecondDigits only if smallestUnit is not present */
        if (!has_smallestUnit && !fsd_is_auto) {
            frac_digits = fsd_num;
        }
    }
    
    epoch_sec = s->ts.secs;
    nsec = s->ts.nsecs;
    
    /* Apply rounding/truncation based on smallestUnit */
    if (unit_ns > 1 || frac_digits >= 0) {
        js_int128_t total_ns;
        js_int128_t rounded_ns;
        
        /* Use js_int128_t to avoid overflow for large negative epoch seconds */
        total_ns = ti_sec_nsec_to_ns(epoch_sec, nsec);
        
        if (unit_ns > 1) {
            rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, unit_ns, mode);
        } else if (frac_digits >= 0) {
            js_int128_t inc = 1;
            int i;
            for (i = 0; i < (9 - frac_digits); i++)
                inc *= 10;
            rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, inc, mode);
        } else {
            rounded_ns = total_ns;
        }
        
        epoch_sec = (int64_t)(rounded_ns / 1000000000);
        nsec = (uint32_t)(rounded_ns % 1000000000);
        if (rounded_ns < 0 && nsec != 0) {
            nsec += 1000000000;
            epoch_sec -= 1;
        }
    }
    
    /* Apply time zone offset for display */
    if (has_timezone && tz_name[0] != '\0') {
        int tz_id = ti_tz_find_id(tz_name);
        if (tz_id >= 0)
            offset_sec = ti_tz_offset_at_sec(tz_id, epoch_sec);
    }
    if (has_timezone)
        epoch_sec += offset_sec;
    
    /* Round offset to nearest minute for display (half-expand away from zero) */
    if (has_timezone) {
        if (offset_sec >= 0)
            offset_sec = (offset_sec + 30) / 60 * 60;
        else
            offset_sec = (offset_sec - 30) / 60 * 60;
    }
    
    ti_epoch_sec_to_date(epoch_sec, &year, &month, &day, &hour, &minute, &second);
    
    if (year < 0) {
        n = snprintf(buf, sizeof(buf), "-%06" PRId64 "-%02d-%02dT%02d:%02d",
                     -year, month, day, hour, minute);
    } else if (year >= 10000) {
        n = snprintf(buf, sizeof(buf), "+%06" PRId64 "-%02d-%02dT%02d:%02d",
                     year, month, day, hour, minute);
    } else {
        n = snprintf(buf, sizeof(buf), "%04" PRId64 "-%02d-%02dT%02d:%02d",
                     year, month, day, hour, minute);
    }
    
    if (show_seconds) {
        int m = snprintf(buf + n, sizeof(buf) - (size_t)n, ":%02d", second);
        n += m;
        
        if (frac_digits == 0) {
            if (has_timezone) {
                int off_sec = (int)(offset_sec >= 0 ? offset_sec : -offset_sec);
                snprintf(buf + n, sizeof(buf) - (size_t)n, "%c%02d:%02d",
                         offset_sec >= 0 ? '+' : '-', off_sec / 3600, (off_sec % 3600) / 60);
            } else {
                snprintf(buf + n, sizeof(buf) - (size_t)n, "Z");
            }
        } else if (frac_digits > 0) {
            char frac[16];
            snprintf(frac, sizeof(frac), "%09u", nsec);
            frac[frac_digits] = '\0';
            if (has_timezone) {
                int off_sec = (int)(offset_sec >= 0 ? offset_sec : -offset_sec);
                snprintf(buf + n, sizeof(buf) - (size_t)n, ".%s%c%02d:%02d", frac,
                         offset_sec >= 0 ? '+' : '-', off_sec / 3600, (off_sec % 3600) / 60);
            } else {
                snprintf(buf + n, sizeof(buf) - (size_t)n, ".%sZ", frac);
            }
        } else {
            /* Auto: show only non-zero fraction digits */
            if (nsec > 0) {
                char frac[16];
                int frac_n;
                frac_n = snprintf(frac, sizeof(frac), "%09u", nsec);
                while (frac_n > 0 && frac[frac_n - 1] == '0')
                    frac_n--;
                frac[frac_n] = '\0';
                if (has_timezone) {
                    int off_sec = (int)(offset_sec >= 0 ? offset_sec : -offset_sec);
                    snprintf(buf + n, sizeof(buf) - (size_t)n, ".%s%c%02d:%02d", frac,
                             offset_sec >= 0 ? '+' : '-', off_sec / 3600, (off_sec % 3600) / 60);
                } else {
                    snprintf(buf + n, sizeof(buf) - (size_t)n, ".%sZ", frac);
                }
            } else {
                if (has_timezone) {
                    int off_sec = (int)(offset_sec >= 0 ? offset_sec : -offset_sec);
                    snprintf(buf + n, sizeof(buf) - (size_t)n, "%c%02d:%02d",
                             offset_sec >= 0 ? '+' : '-', off_sec / 3600, (off_sec % 3600) / 60);
                } else {
                    snprintf(buf + n, sizeof(buf) - (size_t)n, "Z");
                }
            }
        }
    } else {
        if (has_timezone) {
            int off_sec = (int)(offset_sec >= 0 ? offset_sec : -offset_sec);
            snprintf(buf + n, sizeof(buf) - (size_t)n, "%c%02d:%02d",
                     offset_sec >= 0 ? '+' : '-', off_sec / 3600, (off_sec % 3600) / 60);
        } else {
            snprintf(buf + n, sizeof(buf) - (size_t)n, "Z");
        }
    }
    
    return JS_NewString(ctx, buf);
}

static const JSCFunctionListEntry js_instant_proto_funcs[] = {
    JS_CGETSET_DEF("epochMilliseconds", js_instant_get_epochMilliseconds, NULL),
    JS_CGETSET_DEF("epochNanoseconds", js_instant_get_epochNanoseconds, NULL),
    JS_CFUNC_DEF("equals", 1, js_instant_equals),
    JS_CFUNC_DEF("toString", 0, js_instant_toString),
    JS_CFUNC_DEF("toJSON", 0, js_instant_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_instant_toLocaleString),
    JS_CFUNC_DEF("toZonedDateTimeISO", 1, js_instant_toZonedDateTimeISO),
    JS_CFUNC_MAGIC_DEF("add", 1, js_instant_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_instant_add_subtract, 0),
    JS_CFUNC_MAGIC_DEF("until", 1, js_instant_difference, 0),
    JS_CFUNC_MAGIC_DEF("since", 1, js_instant_difference, 1),
    JS_CFUNC_DEF("round", 1, js_instant_round),
    JS_CFUNC_DEF("valueOf", 0, js_instant_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Instant", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_instant_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_instant_from),
    JS_CFUNC_DEF("fromEpochMilliseconds", 1, js_instant_fromEpochMilliseconds),
    JS_CFUNC_DEF("fromEpochNanoseconds", 1, js_instant_fromEpochNanoseconds),
    JS_CFUNC_DEF("compare", 2, js_instant_compare),
};

int js_add_instant(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_instant_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_instant_class_id, &js_instant_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_instant_class_id, "Instant",
                               (JSCFunction *)js_instant_constructor, 1,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_instant_static_funcs, countof(js_instant_static_funcs),
                               js_instant_proto_funcs, countof(js_instant_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "Instant", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
