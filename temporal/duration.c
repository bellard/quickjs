/*
 * QuickJS Temporal.Duration implementation (minimal)
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

static JSClassID js_duration_class_id;

static void js_duration_finalizer(JSRuntime *rt, JSValue val)
{
    JSDurationData *s = JS_GetOpaque(val, js_duration_class_id);
    if (s) {
        js_free_rt(rt, s);
    }
}

static const JSClassDef js_duration_class = {
    "Temporal.Duration",
    .finalizer = js_duration_finalizer,
};

static void duration_round_fields_to_float64(JSDurationData *d)
{
    d->milliseconds = (int64_t)(double)d->milliseconds;
    d->microseconds.int128_value = (js_int128_t)(double)d->microseconds.int128_value;
    d->nanoseconds.int128_value = (js_int128_t)(double)d->nanoseconds.int128_value;
}

static TIUnit duration_get_largest_unit(const JSDurationData *d)
{
    if (d->years != 0) return TI_UNIT_YEAR;
    if (d->months != 0) return TI_UNIT_MONTH;
    if (d->weeks != 0) return TI_UNIT_WEEK;
    if (d->days != 0) return TI_UNIT_DAY;
    if (d->hours != 0) return TI_UNIT_HOUR;
    if (d->minutes != 0) return TI_UNIT_MINUTE;
    if (d->seconds != 0) return TI_UNIT_SECOND;
    if (d->milliseconds != 0) return TI_UNIT_MILLISECOND;
    if (d->microseconds.int128_value != 0) return TI_UNIT_MICROSECOND;
    if (d->nanoseconds.int128_value != 0) return TI_UNIT_NANOSECOND;
    return TI_UNIT_NANOSECOND;
}

static TIUnit ti_larger_of_two_units(TIUnit a, TIUnit b)
{
    return a < b ? a : b;
}

JSValue js_create_duration(JSContext *ctx, const JSDurationData *d)
{
    JSDurationData *s;
    JSValue obj;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    *s = *d;
    /* Round fields to float64-representable integers to match spec behavior */
    duration_round_fields_to_float64(s);
    obj = JS_NewObjectClass(ctx, (int)js_duration_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

static int to_integer_if_integral(JSContext *ctx, JSValueConst val, int64_t *pres)
{
    double d;
    if (JS_IsUndefined(val)) {
        *pres = 0;
        return 0;
    }
    if (JS_ToFloat64(ctx, &d, val))
        return -1;
    if (!isfinite(d) || floor(d) != d) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return -1;
    }
    if (d > (double)INT64_MAX || d < (double)INT64_MIN) {
        JS_ThrowRangeError(ctx, "value out of range");
        return -1;
    }
    *pres = (int64_t)d;
    return 0;
}

static int to_integer_if_integral128(JSContext *ctx, JSValueConst val, js_int128_t *pres)
{
    double d;
    if (JS_IsUndefined(val)) {
        *pres = 0;
        return 0;
    }
    if (JS_ToFloat64(ctx, &d, val))
        return -1;
    if (!isfinite(d) || floor(d) != d) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return -1;
    }
    *pres = (js_int128_t)d;
    if (d != 0 && *pres == 0) {
        JS_ThrowRangeError(ctx, "value out of range");
        return -1;
    }
    return 0;
}

#define DURATION_MAX_SECONDS 9007199254740991LL

int duration_has_mixed_signs(const JSDurationData *d)
{
    int sign = 0;
    int64_t vals[] = { d->years, d->months, d->weeks, d->days, d->hours,
                       d->minutes, d->seconds, d->milliseconds };
    int i;
    for (i = 0; i < 8; i++) {
        if (vals[i] != 0) {
            if (sign == 0)
                sign = vals[i] > 0 ? 1 : -1;
            else if ((vals[i] > 0 ? 1 : -1) != sign)
                return 1;
        }
    }
    if (d->microseconds.int128_value != 0) {
        if (sign == 0)
            sign = d->microseconds.int128_value > 0 ? 1 : -1;
        else if ((d->microseconds.int128_value > 0 ? 1 : -1) != sign)
            return 1;
    }
    if (d->nanoseconds.int128_value != 0) {
        if (sign == 0)
            sign = d->nanoseconds.int128_value > 0 ? 1 : -1;
        else if ((d->nanoseconds.int128_value > 0 ? 1 : -1) != sign)
            return 1;
    }
    return 0;
}

int duration_is_out_of_range(const JSDurationData *d)
{
    const int64_t MAX_YEARS = 4294967295LL;
    const int64_t MAX_MONTHS = 4294967295LL;
    const int64_t MAX_WEEKS = 4294967295LL;
    /* These limits prevent overflow in multiplication and are derived from
       the total nanoseconds limit 10^9 * 2^53. */
    const int64_t MAX_DAYS_ALONE = 104249991374LL;
    const int64_t MAX_HOURS_ALONE = 2501999792983LL;
    const int64_t MAX_MINUTES_ALONE = 150119987579016LL;
    const int64_t MAX_SECONDS_ALONE = 9007199254740991LL;
    int64_t abs_days, abs_hours, abs_minutes, abs_seconds;
    int64_t abs_ms;
    js_int128_t abs_us, abs_ns;
    JSTimeSpec total = {0, 0};
    JSTimeSpec ts;

    if (d->years < -MAX_YEARS || d->years > MAX_YEARS)
        return 1;
    if (d->months < -MAX_MONTHS || d->months > MAX_MONTHS)
        return 1;
    if (d->weeks < -MAX_WEEKS || d->weeks > MAX_WEEKS)
        return 1;

    /* Quick reject for individual fields that are too large, preventing
       overflow in the subsequent total computation. */
    if (d->days < -MAX_DAYS_ALONE || d->days > MAX_DAYS_ALONE)
        return 1;
    if (d->hours < -MAX_HOURS_ALONE || d->hours > MAX_HOURS_ALONE)
        return 1;
    if (d->minutes < -MAX_MINUTES_ALONE || d->minutes > MAX_MINUTES_ALONE)
        return 1;
    if (d->seconds < -MAX_SECONDS_ALONE || d->seconds > MAX_SECONDS_ALONE)
        return 1;

    /* Since duration_has_mixed_signs() is already checked, all non-zero
       fields have the same sign. Use absolute values for safe addition. */
    abs_days = d->days < 0 ? -d->days : d->days;
    abs_hours = d->hours < 0 ? -d->hours : d->hours;
    abs_minutes = d->minutes < 0 ? -d->minutes : d->minutes;
    abs_seconds = d->seconds < 0 ? -d->seconds : d->seconds;
    abs_ms = d->milliseconds < 0 ? -d->milliseconds : d->milliseconds;
    abs_us = d->microseconds.int128_value < 0 ? -d->microseconds.int128_value : d->microseconds.int128_value;
    abs_ns = d->nanoseconds.int128_value < 0 ? -d->nanoseconds.int128_value : d->nanoseconds.int128_value;

    /* Accumulate total nanoseconds as (seconds, nanoseconds) using
       non-negative arithmetic to avoid signed overflow. */
    ts.secs = abs_days * 86400LL;
    ts.nsecs = 0;
    total.secs += ts.secs;

    ts.secs = abs_hours * 3600LL;
    ts.nsecs = 0;
    total.secs += ts.secs;

    ts.secs = abs_minutes * 60LL;
    ts.nsecs = 0;
    total.secs += ts.secs;

    ts.secs = abs_seconds;
    ts.nsecs = 0;
    total.secs += ts.secs;

    {
        uint64_t n = (uint64_t)total.nsecs + (uint64_t)(abs_ms % 1000) * 1000000;
        total.secs += abs_ms / 1000 + (int64_t)(n / 1000000000);
        total.nsecs = (uint32_t)(n % 1000000000);
    }

    {
        uint64_t n = (uint64_t)total.nsecs + (uint64_t)(abs_us % 1000000) * 1000;
        total.secs += (int64_t)(abs_us / 1000000) + (int64_t)(n / 1000000000);
        total.nsecs = (uint32_t)(n % 1000000000);
    }

    {
        uint64_t n = (uint64_t)total.nsecs + (uint64_t)(abs_ns % 1000000000);
        total.secs += (int64_t)(abs_ns / 1000000000) + (int64_t)(n / 1000000000);
        total.nsecs = (uint32_t)(n % 1000000000);
    }

    if (total.secs >= 9007199254740992LL)
        return 1;
    return 0;
}

static int64_t round_half_away_from_zero(double x)
{
    if (x >= 0)
        return (int64_t)floor(x + 0.5);
    else
        return (int64_t)ceil(x - 0.5);
}

static void distribute_fractional_time(double hours, double minutes, double seconds,
                                        double milliseconds, double microseconds, double nanoseconds,
                                        JSDurationData *out)
{
    /* Distribute fractional parts downward only. Integer components in each
       unit are preserved as-is per ParseTemporalDurationString. */
    double frac;
    out->hours = (int64_t)floor(hours);
    frac = (hours - (double)out->hours) * 60;
    minutes += frac;
    
    out->minutes = (int64_t)floor(minutes);
    frac = (minutes - (double)out->minutes) * 60;
    seconds += frac;
    
    out->seconds = (int64_t)floor(seconds);
    frac = (seconds - (double)out->seconds) * 1000;
    milliseconds += frac;
    
    out->milliseconds = (int64_t)floor(milliseconds);
    frac = (milliseconds - (double)out->milliseconds) * 1000;
    microseconds += frac;
    
    out->microseconds.int128_value = (int64_t)floor(microseconds);
    frac = (microseconds - (double)out->microseconds.int128_value) * 1000;
    nanoseconds += frac;
    
    /* Final nanoseconds rounded with halfExpand */
    out->nanoseconds.int128_value = round_half_away_from_zero(nanoseconds);
    /* Normalize subseconds only (they all came from fractional seconds) */
    if (out->nanoseconds.int128_value >= 1000) {
        out->microseconds.int128_value += out->nanoseconds.int128_value / 1000;
        out->nanoseconds.int128_value %= 1000;
    } else if (out->nanoseconds.int128_value < 0) {
        out->microseconds.int128_value += (out->nanoseconds.int128_value - 999) / 1000;
        out->nanoseconds.int128_value = 1000 + (out->nanoseconds.int128_value % 1000);
        if (out->nanoseconds.int128_value == 1000) {
            out->nanoseconds.int128_value = 0;
            out->microseconds.int128_value += 1;
        }
    }
    if (out->microseconds.int128_value >= 1000) {
        out->milliseconds += (int64_t)(out->microseconds.int128_value / 1000);
        out->microseconds.int128_value %= 1000;
    } else if (out->microseconds.int128_value < 0) {
        out->milliseconds += (int64_t)((out->microseconds.int128_value - 999) / 1000);
        out->microseconds.int128_value = 1000 + (out->microseconds.int128_value % 1000);
        if (out->microseconds.int128_value == 1000) {
            out->microseconds.int128_value = 0;
            out->milliseconds += 1;
        }
    }
    if (out->milliseconds >= 1000) {
        out->seconds += out->milliseconds / 1000;
        out->milliseconds %= 1000;
    } else if (out->milliseconds < 0) {
        out->seconds += (out->milliseconds - 999) / 1000;
        out->milliseconds = 1000 + (out->milliseconds % 1000);
        if (out->milliseconds == 1000) {
            out->milliseconds = 0;
            out->seconds += 1;
        }
    }
}

int js_parse_duration_string(JSContext *ctx, const char *str, JSDurationData *out)
{
    const char *p;
    int negative = 0;
    int has_t = 0;
    double num = 0;
    int in_time = 0;
    int has_value = 0;
    double hours = 0, minutes = 0, seconds = 0;
    double milliseconds = 0, microseconds = 0, nanoseconds = 0;
    int has_minutes = 0, has_seconds = 0;
    int frac_hours = 0, frac_minutes = 0;
    memset(out, 0, sizeof(*out));
    p = str;
    if (*p == '-') {
        negative = 1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (*p != 'P' && *p != 'p') {
        JS_ThrowRangeError(ctx, "invalid duration string");
        return -1;
    }
    p++;
    while (*p) {
        char c;
        char *end;
        char num_buf[64];
        int num_len;
        if (*p == 'T' || *p == 't') {
            if (has_t) {
                JS_ThrowRangeError(ctx, "invalid duration string");
                return -1;
            }
            has_t = 1;
            in_time = 1;
            p++;
            continue;
        }
        if (*p < '0' || *p > '9') {
            JS_ThrowRangeError(ctx, "invalid duration string");
            return -1;
        }
        /* Copy number to buffer, replacing comma with dot for strtod */
        num_len = 0;
        while ((*p >= '0' && *p <= '9') || *p == '.' || *p == ',') {
            if (num_len < (int)(sizeof(num_buf) - 1)) {
                num_buf[num_len] = (*p == ',') ? '.' : *p;
                num_len++;
            }
            p++;
        }
        if (num_len >= (int)(sizeof(num_buf) - 1)) {
            JS_ThrowRangeError(ctx, "invalid duration string");
            return -1;
        }
        /* Reject trailing decimal point (e.g. "2.") or leading decimal (".1") */
        if (num_buf[0] == '.' || (num_len > 0 && num_buf[num_len - 1] == '.')) {
            JS_ThrowRangeError(ctx, "invalid duration string");
            return -1;
        }
        /* Reject multiple decimal points */
        {
            int dot_count = 0, i;
            for (i = 0; i < num_len; i++)
                if (num_buf[i] == '.')
                    dot_count++;
            if (dot_count > 1) {
                JS_ThrowRangeError(ctx, "invalid duration string");
                return -1;
            }
        }
        num_buf[num_len] = '\0';
        num = strtod(num_buf, &end);
        if (end == num_buf || !isfinite(num)) {
            JS_ThrowRangeError(ctx, "invalid duration string");
            return -1;
        }
        c = *p;
        if (!c) {
            JS_ThrowRangeError(ctx, "invalid duration string");
            return -1;
        }
        has_value = 1;
        if (in_time) {
            switch (c) {
            case 'H': case 'h':
                if (floor(num) != num) frac_hours = 1;
                hours += num;
                break;
            case 'M': case 'm':
                has_minutes = 1;
                if (floor(num) != num) frac_minutes = 1;
                minutes += num;
                break;
            case 'S': case 's':
                has_seconds = 1;
                {
                    int dot_pos = -1, i;
                    for (i = 0; i < num_len; i++) {
                        if (num_buf[i] == '.') {
                            dot_pos = i;
                            break;
                        }
                    }
                    if (dot_pos >= 0) {
                        int frac_digits = num_len - dot_pos - 1;
                        char int_buf[32];
                        int64_t int_sec;
                        if (frac_digits > 9) {
                            JS_ThrowRangeError(ctx, "invalid duration string");
                            return -1;
                        }
                        /* Parse integer part separately to avoid double rounding */
                        if (dot_pos >= (int)sizeof(int_buf))
                            dot_pos = sizeof(int_buf) - 1;
                        memcpy(int_buf, num_buf, (size_t)dot_pos);
                        int_buf[dot_pos] = '\0';
                        int_sec = strtoll(int_buf, NULL, 10);
                        if (int_sec > DURATION_MAX_SECONDS || int_sec < -DURATION_MAX_SECONDS) {
                            JS_ThrowRangeError(ctx, "duration out of range");
                            return -1;
                        }
                        seconds += (double)int_sec;
                        /* Distribute fractional part to ms/us/ns */
                        {
                            char frac_buf[16];
                            int64_t frac_val;
                            int j;
                            for (j = 0; j < frac_digits && j < 9; j++)
                                frac_buf[j] = num_buf[dot_pos + 1 + j];
                            for (; j < 9; j++)
                                frac_buf[j] = '0';
                            frac_buf[9] = '\0';
                            frac_val = strtoll(frac_buf, NULL, 10);
                            milliseconds += (double)(frac_val / 1000000);
                            microseconds += (double)((frac_val / 1000) % 1000);
                            nanoseconds += (double)(frac_val % 1000);
                        }
                    } else {
                        int64_t int_sec = strtoll(num_buf, NULL, 10);
                        if (int_sec > DURATION_MAX_SECONDS || int_sec < -DURATION_MAX_SECONDS) {
                            JS_ThrowRangeError(ctx, "duration out of range");
                            return -1;
                        }
                        seconds += (double)int_sec;
                    }
                }
                break;
            default:
                JS_ThrowRangeError(ctx, "invalid duration string");
                return -1;
            }
        } else {
            switch (c) {
            case 'Y': case 'y':
                if (floor(num) != num) {
                    JS_ThrowRangeError(ctx, "invalid duration string");
                    return -1;
                }
                out->years = (int64_t)num;
                break;
            case 'M': case 'm':
                if (floor(num) != num) {
                    JS_ThrowRangeError(ctx, "invalid duration string");
                    return -1;
                }
                out->months = (int64_t)num;
                break;
            case 'W': case 'w':
                if (floor(num) != num) {
                    JS_ThrowRangeError(ctx, "invalid duration string");
                    return -1;
                }
                out->weeks = (int64_t)num;
                break;
            case 'D': case 'd':
                if (floor(num) != num) {
                    JS_ThrowRangeError(ctx, "invalid duration string");
                    return -1;
                }
                out->days = (int64_t)num;
                break;
            default:
                JS_ThrowRangeError(ctx, "invalid duration string");
                return -1;
            }
        }
        p++;
    }
    if (!has_value) {
        JS_ThrowRangeError(ctx, "invalid duration string");
        return -1;
    }
    if (*p) {
        JS_ThrowRangeError(ctx, "invalid duration string");
        return -1;
    }
    /* If a unit is fractional, no lower time unit may be present */
    if ((frac_hours && (has_minutes || has_seconds)) ||
        (frac_minutes && has_seconds)) {
        JS_ThrowRangeError(ctx, "invalid duration string");
        return -1;
    }
    /* Distribute fractional time units */
    distribute_fractional_time(hours, minutes, seconds, milliseconds, microseconds, nanoseconds, out);
    if (negative) {
        out->years = -out->years;
        out->months = -out->months;
        out->weeks = -out->weeks;
        out->days = -out->days;
        out->hours = -out->hours;
        out->minutes = -out->minutes;
        out->seconds = -out->seconds;
        out->milliseconds = -out->milliseconds;
        out->microseconds.int128_value = -out->microseconds.int128_value;
        out->nanoseconds.int128_value = -out->nanoseconds.int128_value;
    }
    return 0;
}

/* Convert an arbitrary value to a JSDurationData */
/* Read a single duration property. Returns 1 if found, 0 if undefined, -1 on error. */
static int read_duration_property(JSContext *ctx, JSValueConst obj, const char *name,
                                   int use_128, int64_t *out_i64, js_int128_t *out_i128)
{
    JSValue v = JS_GetPropertyStr(ctx, obj, name);
    if (JS_IsUndefined(v))
        return 0;
    if (use_128) {
        if (to_integer_if_integral128(ctx, v, out_i128) < 0) {
            JS_FreeValue(ctx, v);
            return -1;
        }
    } else {
        if (to_integer_if_integral(ctx, v, out_i64) < 0) {
            JS_FreeValue(ctx, v);
            return -1;
        }
    }
    JS_FreeValue(ctx, v);
    return 1;
}

int js_to_temporal_duration(JSContext *ctx, JSValueConst item, JSDurationData *out)
{
    JSDurationData *s;
    int has_any = 0;
    js_int128_t tmp_microseconds = 0, tmp_nanoseconds = 0;
    int ret;
    memset(out, 0, sizeof(*out));
    if (JS_IsObject(item)) {
        /* If it's already a Duration, extract its data */
        s = JS_GetOpaque(item, js_duration_class_id);
        if (s) {
            *out = *s;
            return 0;
        }
        /* Read duration-like properties in alphabetical order per spec */
        ret = read_duration_property(ctx, item, "days", 0, &out->days, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "hours", 0, &out->hours, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "microseconds", 1, NULL, &tmp_microseconds);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "milliseconds", 0, &out->milliseconds, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "minutes", 0, &out->minutes, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "months", 0, &out->months, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "nanoseconds", 1, NULL, &tmp_nanoseconds);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "seconds", 0, &out->seconds, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "weeks", 0, &out->weeks, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        ret = read_duration_property(ctx, item, "years", 0, &out->years, NULL);
        if (ret < 0) return -1;
        has_any |= ret;
        out->microseconds.int128_value = tmp_microseconds;
        out->nanoseconds.int128_value = tmp_nanoseconds;
        if (!has_any) {
            JS_ThrowTypeError(ctx, "no duration properties found");
            return -1;
        }
        if (duration_has_mixed_signs(out)) {
            JS_ThrowRangeError(ctx, "mixed-sign duration not allowed");
            return -1;
        }
        return 0;
    }
    if (JS_IsString(item)) {
        const char *str = JS_ToCString(ctx, item);
        if (!str)
            return -1;
        ret = js_parse_duration_string(ctx, str, out);
        JS_FreeCString(ctx, str);
        return ret;
    }
    JS_ThrowTypeError(ctx, "cannot convert argument to Temporal.Duration");
    return -1;
}

/* Temporal.Duration constructor */
static JSValue js_duration_constructor(JSContext *ctx, JSValueConst new_target,
                                        int argc, JSValueConst *argv)
{
    JSDurationData d = {0};
    js_int128_t tmp_microseconds = 0, tmp_nanoseconds = 0;
    JSValue obj;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.Duration constructor cannot be called as a function");
    }
    if (argc > 0 && to_integer_if_integral(ctx, argv[0], &d.years) < 0)
        return JS_EXCEPTION;
    if (argc > 1 && to_integer_if_integral(ctx, argv[1], &d.months) < 0)
        return JS_EXCEPTION;
    if (argc > 2 && to_integer_if_integral(ctx, argv[2], &d.weeks) < 0)
        return JS_EXCEPTION;
    if (argc > 3 && to_integer_if_integral(ctx, argv[3], &d.days) < 0)
        return JS_EXCEPTION;
    if (argc > 4 && to_integer_if_integral(ctx, argv[4], &d.hours) < 0)
        return JS_EXCEPTION;
    if (argc > 5 && to_integer_if_integral(ctx, argv[5], &d.minutes) < 0)
        return JS_EXCEPTION;
    if (argc > 6 && to_integer_if_integral(ctx, argv[6], &d.seconds) < 0)
        return JS_EXCEPTION;
    if (argc > 7 && to_integer_if_integral(ctx, argv[7], &d.milliseconds) < 0)
        return JS_EXCEPTION;
    if (argc > 8 && to_integer_if_integral128(ctx, argv[8], &tmp_microseconds) < 0)
        return JS_EXCEPTION;
    if (argc > 9 && to_integer_if_integral128(ctx, argv[9], &tmp_nanoseconds) < 0)
        return JS_EXCEPTION;
    d.microseconds.int128_value = tmp_microseconds;
    d.nanoseconds.int128_value = tmp_nanoseconds;
    if (duration_has_mixed_signs(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    if (duration_is_out_of_range(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    {
        JSDurationData *s = js_mallocz(ctx, sizeof(*s));
        if (!s)
            return JS_EXCEPTION;
        *s = d;
        obj = js_create_temporal_object(ctx, new_target, js_duration_class_id, s);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        return obj;
    }
}

/* Temporal.Duration.from */
static JSValue js_duration_from(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSDurationData d = {0};
    JSValue val;
    (void)this_val;
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    val = argv[0];
    if (JS_IsObject(val)) {
        if (js_to_temporal_duration(ctx, val, &d) < 0)
            return JS_EXCEPTION;
        if (duration_has_mixed_signs(&d))
            return JS_ThrowRangeError(ctx, "duration out of range");
        if (duration_is_out_of_range(&d))
            return JS_ThrowRangeError(ctx, "duration out of range");
        return js_create_duration(ctx, &d);
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        int ret;
        if (!str)
            return JS_EXCEPTION;
        ret = js_parse_duration_string(ctx, str, &d);
        JS_FreeCString(ctx, str);
        if (ret < 0)
            return JS_EXCEPTION;
        if (duration_has_mixed_signs(&d))
            return JS_ThrowRangeError(ctx, "duration out of range");
        if (duration_is_out_of_range(&d))
            return JS_ThrowRangeError(ctx, "duration out of range");
        return js_create_duration(ctx, &d);
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.Duration.from");
}

#define DURATION_GETTER(name) \
static JSValue js_duration_get_##name(JSContext *ctx, JSValueConst this_val) \
{ \
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id); \
    if (!s) \
        return JS_EXCEPTION; \
    return JS_NewFloat64(ctx, (double)s->name); \
}

DURATION_GETTER(years)
DURATION_GETTER(months)
DURATION_GETTER(weeks)
DURATION_GETTER(days)
DURATION_GETTER(hours)
DURATION_GETTER(minutes)
DURATION_GETTER(seconds)
DURATION_GETTER(milliseconds)

static JSValue js_duration_get_microseconds(JSContext *ctx, JSValueConst this_val)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, (double)s->microseconds.int128_value);
}

static JSValue js_duration_get_nanoseconds(JSContext *ctx, JSValueConst this_val)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewFloat64(ctx, (double)s->nanoseconds.int128_value);
}

static JSValue js_duration_get_blank(JSContext *ctx, JSValueConst this_val)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, s->years == 0 && s->months == 0 && s->weeks == 0 &&
                           s->days == 0 && s->hours == 0 && s->minutes == 0 &&
                           s->seconds == 0 && s->milliseconds == 0 &&
                           s->microseconds.int128_value == 0 && s->nanoseconds.int128_value == 0);
}

static JSValue js_duration_negated(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    (void)argc;
    (void)argv;
    JSDurationData d;
    if (!s)
        return JS_EXCEPTION;
    d.years = -s->years;
    d.months = -s->months;
    d.weeks = -s->weeks;
    d.days = -s->days;
    d.hours = -s->hours;
    d.minutes = -s->minutes;
    d.seconds = -s->seconds;
    d.milliseconds = -s->milliseconds;
    d.microseconds.int128_value = -s->microseconds.int128_value;
    d.nanoseconds.int128_value = -s->nanoseconds.int128_value;
    return js_create_duration(ctx, &d);
}

static JSValue js_duration_abs(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    (void)argc;
    (void)argv;
    JSDurationData d;
    if (!s)
        return JS_EXCEPTION;
    d.years = s->years < 0 ? -s->years : s->years;
    d.months = s->months < 0 ? -s->months : s->months;
    d.weeks = s->weeks < 0 ? -s->weeks : s->weeks;
    d.days = s->days < 0 ? -s->days : s->days;
    d.hours = s->hours < 0 ? -s->hours : s->hours;
    d.minutes = s->minutes < 0 ? -s->minutes : s->minutes;
    d.seconds = s->seconds < 0 ? -s->seconds : s->seconds;
    d.milliseconds = s->milliseconds < 0 ? -s->milliseconds : s->milliseconds;
    d.microseconds.int128_value = s->microseconds.int128_value < 0 ? -s->microseconds.int128_value : s->microseconds.int128_value;
    d.nanoseconds.int128_value = s->nanoseconds.int128_value < 0 ? -s->nanoseconds.int128_value : s->nanoseconds.int128_value;
    return js_create_duration(ctx, &d);
}

static JSValue js_duration_get_sign(JSContext *ctx, JSValueConst this_val)
{
    JSDurationData *s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    int sign = 0;
    if (!s)
        return JS_EXCEPTION;
    if (s->years > 0 || s->months > 0 || s->weeks > 0 || s->days > 0 || s->hours > 0 ||
        s->minutes > 0 || s->seconds > 0 || s->milliseconds > 0 || s->microseconds.int128_value > 0 ||
        s->nanoseconds.int128_value > 0)
        sign = 1;
    else if (s->years < 0 || s->months < 0 || s->weeks < 0 || s->days < 0 || s->hours < 0 ||
             s->minutes < 0 || s->seconds < 0 || s->milliseconds < 0 || s->microseconds.int128_value < 0 ||
             s->nanoseconds.int128_value < 0)
        sign = -1;
    return JS_NewInt32(ctx, sign);
}

static JSValue js_duration_with(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    (void)argc;
    JSDurationData *s;
    JSDurationData d;
    JSValue val;
    int has_any = 0;
    js_int128_t tmp_microseconds, tmp_nanoseconds;
    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;
    d = *s;
    tmp_microseconds = d.microseconds.int128_value;
    tmp_nanoseconds = d.nanoseconds.int128_value;
    val = argv[0];
    if (!JS_IsObject(val))
        return JS_ThrowTypeError(ctx, "with argument must be an object");
    {
        int ret;
        /* Read in alphabetical order per spec */
        ret = read_duration_property(ctx, val, "days", 0, &d.days, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "hours", 0, &d.hours, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "microseconds", 1, NULL, &tmp_microseconds);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "milliseconds", 0, &d.milliseconds, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "minutes", 0, &d.minutes, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "months", 0, &d.months, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "nanoseconds", 1, NULL, &tmp_nanoseconds);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "seconds", 0, &d.seconds, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "weeks", 0, &d.weeks, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
        ret = read_duration_property(ctx, val, "years", 0, &d.years, NULL);
        if (ret < 0) return JS_EXCEPTION;
        has_any |= ret;
    }
    d.microseconds.int128_value = tmp_microseconds;
    d.nanoseconds.int128_value = tmp_nanoseconds;
    if (!has_any)
        return JS_ThrowTypeError(ctx, "no duration properties found");
    if (duration_has_mixed_signs(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    if (duration_is_out_of_range(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    return js_create_duration(ctx, &d);
}

static js_int128_t duration_to_total_ns(const JSDurationData *d)
{
    int64_t total_seconds = d->years * 31536000LL +
                            d->months * 2592000LL +
                            d->weeks * 604800LL +
                            d->days * 86400LL +
                            d->hours * 3600LL +
                            d->minutes * 60LL +
                            d->seconds;
    js_int128_t total_ns = (js_int128_t)total_seconds * 1000000000LL;
    total_ns += (js_int128_t)d->milliseconds * 1000000LL;
    total_ns += d->microseconds.int128_value * 1000LL;
    total_ns += d->nanoseconds.int128_value;
    return total_ns;
}

static int int128_fits_in_int64(js_int128_t v)
{
    return v >= INT64_MIN && v <= INT64_MAX;
}

static int duration_from_total_ns(js_int128_t total_ns, TIUnit largestUnit, JSDurationData *d)
{
    int negative = total_ns < 0;
    js_int128_t abs_ns = negative ? -total_ns : total_ns;

    memset(d, 0, sizeof(*d));

    if (largestUnit <= TI_UNIT_SECOND) {
        int64_t total_seconds = (int64_t)(abs_ns / 1000000000LL);
        int32_t rem_ns = (int32_t)(abs_ns % 1000000000LL);

        if (largestUnit <= TI_UNIT_YEAR) {
            d->years = total_seconds / 31536000;
            total_seconds %= 31536000;
        }
        if (largestUnit <= TI_UNIT_MONTH) {
            d->months = total_seconds / 2592000;
            total_seconds %= 2592000;
        }
        if (largestUnit <= TI_UNIT_WEEK) {
            d->weeks = total_seconds / 604800;
            total_seconds %= 604800;
        }
        if (largestUnit <= TI_UNIT_DAY) {
            d->days = total_seconds / 86400;
            total_seconds %= 86400;
        }
        if (largestUnit <= TI_UNIT_HOUR) {
            d->hours = total_seconds / 3600;
            total_seconds %= 3600;
        }
        if (largestUnit <= TI_UNIT_MINUTE) {
            d->minutes = total_seconds / 60;
            total_seconds %= 60;
        }
        d->seconds = total_seconds;

        if (largestUnit <= TI_UNIT_MILLISECOND) {
            d->milliseconds = rem_ns / 1000000;
            rem_ns %= 1000000;
        }
        if (largestUnit <= TI_UNIT_MICROSECOND) {
            d->microseconds.int128_value = rem_ns / 1000;
            rem_ns %= 1000;
        }
        d->nanoseconds.int128_value = rem_ns;
    } else {
        if (largestUnit <= TI_UNIT_MILLISECOND) {
            js_int128_t v = abs_ns / 1000000LL;
            if (!int128_fits_in_int64(v)) return -1;
            d->milliseconds = (int64_t)v;
            abs_ns %= 1000000LL;
        }
        if (largestUnit <= TI_UNIT_MICROSECOND) {
            d->microseconds.int128_value = abs_ns / 1000LL;
            abs_ns %= 1000LL;
        }
        d->nanoseconds.int128_value = abs_ns;
    }

    if (negative) {
        d->years = -d->years;
        d->months = -d->months;
        d->weeks = -d->weeks;
        d->days = -d->days;
        d->hours = -d->hours;
        d->minutes = -d->minutes;
        d->seconds = -d->seconds;
        d->milliseconds = -d->milliseconds;
        d->microseconds.int128_value = -d->microseconds.int128_value;
        d->nanoseconds.int128_value = -d->nanoseconds.int128_value;
    }
    return 0;
}

static JSValue js_duration_add_subtract(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv, int sign)
{
    (void)argc;
    JSDurationData *s;
    JSDurationData d;
    JSDurationData result;
    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (js_to_temporal_duration(ctx, argv[0], &d) < 0)
        return JS_EXCEPTION;
    if (s->years != 0 || s->months != 0 || s->weeks != 0 || d.years != 0 || d.months != 0 || d.weeks != 0)
        return JS_ThrowRangeError(ctx, "duration contains calendar units");
    {
        TIUnit largestUnit = ti_larger_of_two_units(duration_get_largest_unit(s), duration_get_largest_unit(&d));
        js_int128_t total1 = duration_to_total_ns(s);
        js_int128_t total2 = duration_to_total_ns(&d);
        js_int128_t sum = total1 + sign * total2;
        if (duration_from_total_ns(sum, largestUnit, &result) < 0)
            return JS_ThrowRangeError(ctx, "duration out of range");
    }
    duration_round_fields_to_float64(&result);
    if (duration_is_out_of_range(&result))
        return JS_ThrowRangeError(ctx, "duration out of range");
    return js_create_duration(ctx, &result);
}

static JSValue js_duration_add(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    return js_duration_add_subtract(ctx, this_val, argc, argv, 1);
}

static JSValue js_duration_subtract(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    return js_duration_add_subtract(ctx, this_val, argc, argv, -1);
}

static void duration_round_fields_to_float64(JSDurationData *d);
static JSValue ti_to_temporal_relative_to_string(JSContext *ctx, const char *str);
static JSValue ti_convert_relative_to_property_bag(JSContext *ctx, JSValueConst obj);
static double ti_iso_total_in_unit(int rel_year, int rel_month, int rel_day,
                             int target_year, int target_month, int target_day,
                             TIUnit unit, int64_t rel_time_ns, int64_t target_time_ns);

/* Temporal.Duration.compare */
static JSValue js_duration_compare(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    JSDurationData d1, d2;
    JSValue relativeTo = JS_UNDEFINED;
    if (js_to_temporal_duration(ctx, argv[0], &d1) < 0)
        return JS_EXCEPTION;
    if (js_to_temporal_duration(ctx, argv[1], &d2) < 0)
        return JS_EXCEPTION;
    /* Round to float64 to match spec observable behavior */
    duration_round_fields_to_float64(&d1);
    duration_round_fields_to_float64(&d2);
    if (duration_is_out_of_range(&d1))
        return JS_ThrowRangeError(ctx, "duration out of range");
    if (duration_is_out_of_range(&d2))
        return JS_ThrowRangeError(ctx, "duration out of range");
    /* Validate options object (spec step 3: GetOptionsObject) */
    if (ti_validate_options_object(ctx, argc >= 3 ? argv[2] : JS_UNDEFINED) < 0)
        return JS_EXCEPTION;
    /* Read relativeTo from options (spec step 4: GetTemporalRelativeToOption) */
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        relativeTo = JS_GetPropertyStr(ctx, argv[2], "relativeTo");
        if (JS_IsException(relativeTo))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(relativeTo)) {
            if (JS_IsObject(relativeTo)) {
                JSPlainDateTimeData *pdt = JS_GetOpaque(relativeTo, js_plaindatetime_class_id);
                if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
                    JSValue plainDate = js_create_plaindate(ctx, pdt->year, pdt->month, pdt->day);
                    JS_FreeValue(ctx, relativeTo);
                    relativeTo = plainDate;
                    if (JS_IsException(relativeTo))
                        return JS_EXCEPTION;
                }
                if (!JS_GetOpaque(relativeTo, js_plaindate_class_id) &&
                    !JS_GetOpaque(relativeTo, js_plaindatetime_class_id) &&
                    !JS_GetOpaque(relativeTo, js_zoneddatetime_class_id)) {
                    JSValue converted = ti_convert_relative_to_property_bag(ctx, relativeTo);
                    JS_FreeValue(ctx, relativeTo);
                    relativeTo = converted;
                    if (JS_IsException(relativeTo))
                        return JS_EXCEPTION;
                }
            } else if (JS_IsString(relativeTo)) {
                const char *rt_str = JS_ToCString(ctx, relativeTo);
                if (!rt_str) {
                    JS_FreeValue(ctx, relativeTo);
                    return JS_EXCEPTION;
                }
                JS_FreeValue(ctx, relativeTo);
                relativeTo = ti_to_temporal_relative_to_string(ctx, rt_str);
                JS_FreeCString(ctx, rt_str);
                if (JS_IsException(relativeTo))
                    return JS_EXCEPTION;
            }
        }
    }
    /* Early exit if all fields are equal (spec step 5) */
    if (d1.years == d2.years && d1.months == d2.months && d1.weeks == d2.weeks &&
        d1.days == d2.days && d1.hours == d2.hours && d1.minutes == d2.minutes &&
        d1.seconds == d2.seconds && d1.milliseconds == d2.milliseconds &&
        d1.microseconds.int128_value == d2.microseconds.int128_value && d1.nanoseconds.int128_value == d2.nanoseconds.int128_value) {
        JS_FreeValue(ctx, relativeTo);
        return JS_NewInt32(ctx, 0);
    }
    /* If calendar units are present, relativeTo is required */
    if (d1.years != 0 || d1.months != 0 || d1.weeks != 0 ||
        d2.years != 0 || d2.months != 0 || d2.weeks != 0) {
        if (JS_IsUndefined(relativeTo))
            return JS_ThrowRangeError(ctx, "relativeTo is required");
    }
    /* If relativeTo is ISO PlainDate/PlainDateTime, use actual calendar
       arithmetic instead of approximate total nanoseconds. */
    if (!JS_IsUndefined(relativeTo)) {
        JSPlainDateData *pd = JS_GetOpaque(relativeTo, js_plaindate_class_id);
        JSPlainDateTimeData *pdt = JS_GetOpaque(relativeTo, js_plaindatetime_class_id);
        if (pd && (pd->calendar == CAL_ISO8601 || pd->calendar == CAL_GREGORY)) {
            /* For time-only durations, skip calendar arithmetic and compare directly */
            if (d1.years == 0 && d1.months == 0 && d1.weeks == 0 && d1.days == 0 &&
                d2.years == 0 && d2.months == 0 && d2.weeks == 0 && d2.days == 0) {
                JS_FreeValue(ctx, relativeTo);
                goto compare_total_ns;
            }
            int y1, m1, d1d, h1, min1, s1, ms1, us1, ns1;
            int y2, m2, d2d, h2, min2, s2, ms2, us2, ns2;
            js_int128_t us_val = d1.microseconds.int128_value;
            js_int128_t ns_val = d1.nanoseconds.int128_value;
            if (ti_iso_datetime_add_duration(
                pd->year, pd->month, pd->day, 0, 0, 0, 0, 0, 0,
                d1.years, d1.months, d1.weeks, d1.days,
                d1.hours, d1.minutes, d1.seconds, d1.milliseconds, us_val, ns_val,
                &y1, &m1, &d1d, &h1, &min1, &s1, &ms1, &us1, &ns1) < 0) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            if (!ti_is_date_within_iso_limits(y1, m1, d1d)) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            us_val = d2.microseconds.int128_value;
            ns_val = d2.nanoseconds.int128_value;
            if (ti_iso_datetime_add_duration(
                pd->year, pd->month, pd->day, 0, 0, 0, 0, 0, 0,
                d2.years, d2.months, d2.weeks, d2.days,
                d2.hours, d2.minutes, d2.seconds, d2.milliseconds, us_val, ns_val,
                &y2, &m2, &d2d, &h2, &min2, &s2, &ms2, &us2, &ns2) < 0) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            if (!ti_is_date_within_iso_limits(y2, m2, d2d)) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            {
                int64_t sec1 = pdt_to_ordinal_days(y1, m1, d1d) * 86400LL + h1 * 3600LL + min1 * 60LL + s1;
                int64_t sec2 = pdt_to_ordinal_days(y2, m2, d2d) * 86400LL + h2 * 3600LL + min2 * 60LL + s2;
                js_int128_t epoch1 = (js_int128_t)sec1 * 1000000000LL + (js_int128_t)ms1 * 1000000LL + (js_int128_t)us1 * 1000LL + (js_int128_t)ns1;
                js_int128_t epoch2 = (js_int128_t)sec2 * 1000000000LL + (js_int128_t)ms2 * 1000000LL + (js_int128_t)us2 * 1000LL + (js_int128_t)ns2;
                JS_FreeValue(ctx, relativeTo);
                if (epoch1 < epoch2) return JS_NewInt32(ctx, -1);
                if (epoch1 > epoch2) return JS_NewInt32(ctx, 1);
                return JS_NewInt32(ctx, 0);
            }
        } else if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
            /* For time-only durations, skip calendar arithmetic and compare directly */
            if (d1.years == 0 && d1.months == 0 && d1.weeks == 0 && d1.days == 0 &&
                d2.years == 0 && d2.months == 0 && d2.weeks == 0 && d2.days == 0) {
                JS_FreeValue(ctx, relativeTo);
                goto compare_total_ns;
            }
            int y1, m1, d1d, h1, min1, s1, ms1, us1, ns1;
            int y2, m2, d2d, h2, min2, s2, ms2, us2, ns2;
            js_int128_t us_val = d1.microseconds.int128_value;
            js_int128_t ns_val = d1.nanoseconds.int128_value;
            if (ti_iso_datetime_add_duration(
                pdt->year, pdt->month, pdt->day, pdt->hour, pdt->minute, pdt->second,
                pdt->millisecond, pdt->microsecond, pdt->nanosecond,
                d1.years, d1.months, d1.weeks, d1.days,
                d1.hours, d1.minutes, d1.seconds, d1.milliseconds, us_val, ns_val,
                &y1, &m1, &d1d, &h1, &min1, &s1, &ms1, &us1, &ns1) < 0) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            if (!ti_is_date_within_iso_limits(y1, m1, d1d)) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            us_val = d2.microseconds.int128_value;
            ns_val = d2.nanoseconds.int128_value;
            if (ti_iso_datetime_add_duration(
                pdt->year, pdt->month, pdt->day, pdt->hour, pdt->minute, pdt->second,
                pdt->millisecond, pdt->microsecond, pdt->nanosecond,
                d2.years, d2.months, d2.weeks, d2.days,
                d2.hours, d2.minutes, d2.seconds, d2.milliseconds, us_val, ns_val,
                &y2, &m2, &d2d, &h2, &min2, &s2, &ms2, &us2, &ns2) < 0) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            if (!ti_is_date_within_iso_limits(y2, m2, d2d)) {
                JS_FreeValue(ctx, relativeTo);
                return JS_ThrowRangeError(ctx, "duration out of range");
            }
            {
                int64_t sec1 = pdt_to_ordinal_days(y1, m1, d1d) * 86400LL + h1 * 3600LL + min1 * 60LL + s1;
                int64_t sec2 = pdt_to_ordinal_days(y2, m2, d2d) * 86400LL + h2 * 3600LL + min2 * 60LL + s2;
                js_int128_t epoch1 = (js_int128_t)sec1 * 1000000000LL + (js_int128_t)ms1 * 1000000LL + (js_int128_t)us1 * 1000LL + (js_int128_t)ns1;
                js_int128_t epoch2 = (js_int128_t)sec2 * 1000000000LL + (js_int128_t)ms2 * 1000000LL + (js_int128_t)us2 * 1000LL + (js_int128_t)ns2;
                JS_FreeValue(ctx, relativeTo);
                if (epoch1 < epoch2) return JS_NewInt32(ctx, -1);
                if (epoch1 > epoch2) return JS_NewInt32(ctx, 1);
                return JS_NewInt32(ctx, 0);
            }
        } else {
            JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo, js_zoneddatetime_class_id);
            if (zdt) {
                /* Only use ZonedDateTime arithmetic if date units are present.
                   For time-only durations, fall through to plain nanosecond comparison
                   to avoid spurious out-of-range errors from adding to boundary instants. */
                if (d1.years == 0 && d1.months == 0 && d1.weeks == 0 && d1.days == 0 &&
                    d2.years == 0 && d2.months == 0 && d2.weeks == 0 && d2.days == 0) {
                    JS_FreeValue(ctx, relativeTo);
                    goto compare_total_ns;
                }
                JSValue d1_obj = js_create_duration(ctx, &d1);
                if (JS_IsException(d1_obj)) {
                    JS_FreeValue(ctx, relativeTo);
                    return JS_EXCEPTION;
                }
                JSValue after1 = js_zoneddatetime_add(ctx, relativeTo, 1, &d1_obj);
                JS_FreeValue(ctx, d1_obj);
                if (JS_IsException(after1)) {
                    JS_FreeValue(ctx, relativeTo);
                    return JS_EXCEPTION;
                }
                JSValue d2_obj = js_create_duration(ctx, &d2);
                if (JS_IsException(d2_obj)) {
                    JS_FreeValue(ctx, after1);
                    JS_FreeValue(ctx, relativeTo);
                    return JS_EXCEPTION;
                }
                JSValue after2 = js_zoneddatetime_add(ctx, relativeTo, 1, &d2_obj);
                JS_FreeValue(ctx, d2_obj);
                JS_FreeValue(ctx, relativeTo);
                if (JS_IsException(after2)) {
                    JS_FreeValue(ctx, after1);
                    return JS_EXCEPTION;
                }
                JSZonedDateTimeData *a1 = JS_GetOpaque(after1, js_zoneddatetime_class_id);
                JSZonedDateTimeData *a2 = JS_GetOpaque(after2, js_zoneddatetime_class_id);
                js_int128_t ns1 = ti_timespec_to_ns(&a1->ts);
                js_int128_t ns2 = ti_timespec_to_ns(&a2->ts);
                JS_FreeValue(ctx, after1);
                JS_FreeValue(ctx, after2);
                if (ns1 < ns2) return JS_NewInt32(ctx, -1);
                if (ns1 > ns2) return JS_NewInt32(ctx, 1);
                return JS_NewInt32(ctx, 0);
            }
        }
    }
    JS_FreeValue(ctx, relativeTo);
compare_total_ns:
    /* Compare using exact js_int128_t arithmetic */
    {
        js_int128_t total1 = duration_to_total_ns(&d1);
        js_int128_t total2 = duration_to_total_ns(&d2);
        if (total1 < total2)
            return JS_NewInt32(ctx, -1);
        if (total1 > total2)
            return JS_NewInt32(ctx, 1);
        return JS_NewInt32(ctx, 0);
    }
}

static int duration_has_calendar_units(const JSDurationData *s)
{
    return s->years != 0 || s->months != 0 || s->weeks != 0;
}

/* Format an instant difference (in nanoseconds) as a JSDurationData time
   duration, capping fields according to largestUnit (a time unit:
   hour..nanosecond).  All date fields in out are zeroed. */
static void duration_format_time_ns(js_int128_t total_ns, TIUnit largestUnit, JSDurationData *out)
{
    memset(out, 0, sizeof(*out));
    int sign = (total_ns < 0) ? -1 : 1;
    js_int128_t abs_ns = total_ns >= 0 ? total_ns : -total_ns;
    int64_t total_seconds = (int64_t)(abs_ns / 1000000000LL);
    int32_t rem_ns = (int32_t)(abs_ns % 1000000000LL);

    if (largestUnit == TI_UNIT_HOUR) {
        out->hours = sign * (total_seconds / 3600);
        total_seconds %= 3600;
        out->minutes = sign * (total_seconds / 60);
        total_seconds %= 60;
        out->seconds = sign * total_seconds;
        out->milliseconds = sign * (rem_ns / 1000000);
        rem_ns %= 1000000;
        out->microseconds.int128_value = sign * (rem_ns / 1000);
        out->nanoseconds.int128_value = sign * (rem_ns % 1000);
    } else if (largestUnit == TI_UNIT_MINUTE) {
        out->minutes = sign * (total_seconds / 60);
        total_seconds %= 60;
        out->seconds = sign * total_seconds;
        out->milliseconds = sign * (rem_ns / 1000000);
        rem_ns %= 1000000;
        out->microseconds.int128_value = sign * (rem_ns / 1000);
        out->nanoseconds.int128_value = sign * (rem_ns % 1000);
    } else if (largestUnit == TI_UNIT_SECOND) {
        out->seconds = sign * total_seconds;
        out->milliseconds = sign * (rem_ns / 1000000);
        rem_ns %= 1000000;
        out->microseconds.int128_value = sign * (rem_ns / 1000);
        out->nanoseconds.int128_value = sign * (rem_ns % 1000);
    } else if (largestUnit == TI_UNIT_MILLISECOND) {
        out->milliseconds = sign * (total_seconds * 1000 + rem_ns / 1000000);
        rem_ns %= 1000000;
        out->microseconds.int128_value = sign * (rem_ns / 1000);
        out->nanoseconds.int128_value = sign * (rem_ns % 1000);
    } else if (largestUnit == TI_UNIT_MICROSECOND) {
        out->microseconds.int128_value = sign * (total_seconds * 1000000LL + rem_ns / 1000);
        out->nanoseconds.int128_value = sign * (rem_ns % 1000);
    } else {
        /* TI_UNIT_NANOSECOND: everything in nanoseconds */
        out->nanoseconds.int128_value = sign * total_ns;
    }
}

/* Divide total_ns by divisor as a correctly-rounded double.
   |total_ns| < ~2^86 (from DURATION_MAX_SECONDS * 1e9 + subsecond parts).
   divisor is a uint64_t unit in nanoseconds, always < 2^53.
   The simplest correct method uses __float128 division. */
static double duration_total_div(js_int128_t total_ns, uint64_t divisor)
{
    /* Fast path: both total_ns and divisor are exactly representable as doubles,
       and IEEE 754 division is correctly rounded. */
    if (total_ns < (1LL << 53) && total_ns > -(1LL << 53)) {
        return (double)total_ns / (double)divisor;
    }
#ifdef __SIZEOF_FLOAT128__
    return (double)((__float128)total_ns / (__float128)divisor);
#else
    {
        js_int128_t q = total_ns / (js_int128_t)divisor;
        js_int128_t r = total_ns % (js_int128_t)divisor;
        return (double)q + (double)r / (double)divisor;
    }
#endif
}

/* NudgeToCalendarUnit-style total for ZonedDateTime relativeTo.
   Computes total = (r1 * denominator + sign * numerator) / denominator
   where numerator = dest - start, denominator = end - start. */
static int duration_total_nudge(const char *timeZone, CalendarType calendar,
                                 JSTimeSpec origin, JSTimeSpec dest, JSDurationData *diff,
                                 TIUnit unit, double *out)
{
    /* Determine sign from the first non-zero field (same as InternalDurationSign) */
    int sign = 1;
    if (diff->years < 0) sign = -1;
    else if (diff->months < 0) sign = -1;
    else if (diff->weeks < 0) sign = -1;
    else if (diff->days < 0) sign = -1;
    else if (diff->hours < 0) sign = -1;
    else if (diff->minutes < 0) sign = -1;
    else if (diff->seconds < 0) sign = -1;
    else if (diff->milliseconds < 0) sign = -1;
    else if (diff->microseconds.int128_value < 0) sign = -1;
    else if (diff->nanoseconds.int128_value < 0) sign = -1;

    int64_t r1 = 0;
    JSDurationData start_d = {0};
    JSDurationData end_d = {0};

    switch (unit) {
    case TI_UNIT_YEAR:
        r1 = diff->years;
        start_d.years = r1;
        start_d.months = 0;
        start_d.weeks = 0;
        start_d.days = 0;
        end_d.years = r1 + sign;
        end_d.months = 0;
        end_d.weeks = 0;
        end_d.days = 0;
        break;
    case TI_UNIT_MONTH:
        r1 = diff->months;
        start_d.years = diff->years;
        start_d.months = r1;
        start_d.weeks = 0;
        start_d.days = 0;
        end_d.years = diff->years;
        end_d.months = r1 + sign;
        end_d.weeks = 0;
        end_d.days = 0;
        break;
    case TI_UNIT_WEEK: {
        /* Balance days into weeks like CalendarDateUntil(..., 'week') */
        int64_t wsy, wsm, wsd, wey, wem, wed;
        if (ti_iso_calendar_date_add(0, 1, 1, diff->years, diff->months, 0, 0, (int*)&wsy, (int*)&wsm, (int*)&wsd) < 0)
            return -1;
        if (ti_iso_calendar_date_add((int)wsy, (int)wsm, (int)wsd, 0, 0, 0, diff->days, (int*)&wey, (int*)&wem, (int*)&wed) < 0)
            return -1;
        int64_t wweeks = 0, wdays = 0;
        ti_iso_calendar_date_until((int)wsy, (int)wsm, (int)wsd, (int)wey, (int)wem, (int)wed, TI_UNIT_WEEK,
                                    &wweeks, &wdays, NULL, NULL);
        r1 = diff->weeks + wweeks;
        start_d.years = diff->years;
        start_d.months = diff->months;
        start_d.weeks = r1;
        start_d.days = 0;
        end_d.years = diff->years;
        end_d.months = diff->months;
        end_d.weeks = r1 + sign;
        end_d.days = 0;
        break;
    }
    case TI_UNIT_DAY:
        r1 = diff->days;
        start_d.years = diff->years;
        start_d.months = diff->months;
        start_d.weeks = diff->weeks;
        start_d.days = r1;
        end_d.years = diff->years;
        end_d.months = diff->months;
        end_d.weeks = diff->weeks;
        end_d.days = r1 + sign;
        break;
    default:
        return -1;
    }

    JSTimeSpec start_ts, end_ts;
    if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &start_d, 1, &start_ts) < 0)
        return -1;
    if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &end_d, 1, &end_ts) < 0)
        return -1;

    js_int128_t start_ns = ti_timespec_to_ns(&start_ts);
    js_int128_t end_ns = ti_timespec_to_ns(&end_ts);
    js_int128_t dest_ns = ti_timespec_to_ns(&dest);

    /* Expand window if dest is outside */
    if (sign == 1) {
        if (!(start_ns <= dest_ns && dest_ns <= end_ns)) {
            r1 += sign;
            start_d = end_d;
            end_d.years = start_d.years;
            end_d.months = start_d.months;
            end_d.weeks = start_d.weeks;
            end_d.days = start_d.days;
            switch (unit) {
            case TI_UNIT_YEAR: end_d.years += sign; end_d.months = 0; end_d.weeks = 0; end_d.days = 0; break;
            case TI_UNIT_MONTH: end_d.months += sign; end_d.weeks = 0; end_d.days = 0; break;
            case TI_UNIT_WEEK: end_d.weeks += sign; end_d.days = 0; break;
            case TI_UNIT_DAY: end_d.days += sign; break;
            default: break;
            }
            if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &start_d, 1, &start_ts) < 0)
                return -1;
            if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &end_d, 1, &end_ts) < 0)
                return -1;
            start_ns = ti_timespec_to_ns(&start_ts);
            end_ns = ti_timespec_to_ns(&end_ts);
        }
    } else {
        if (!(end_ns <= dest_ns && dest_ns <= start_ns)) {
            r1 += sign;
            start_d = end_d;
            end_d.years = start_d.years;
            end_d.months = start_d.months;
            end_d.weeks = start_d.weeks;
            end_d.days = start_d.days;
            switch (unit) {
            case TI_UNIT_YEAR: end_d.years += sign; end_d.months = 0; end_d.weeks = 0; end_d.days = 0; break;
            case TI_UNIT_MONTH: end_d.months += sign; end_d.weeks = 0; end_d.days = 0; break;
            case TI_UNIT_WEEK: end_d.weeks += sign; end_d.days = 0; break;
            case TI_UNIT_DAY: end_d.days += sign; break;
            default: break;
            }
            if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &start_d, 1, &start_ts) < 0)
                return -1;
            if (ti_zoneddatetime_add_duration_iso(timeZone, calendar, origin, &end_d, 1, &end_ts) < 0)
                return -1;
            start_ns = ti_timespec_to_ns(&start_ts);
            end_ns = ti_timespec_to_ns(&end_ts);
        }
    }

    js_int128_t numerator = dest_ns - start_ns;
    js_int128_t denominator = end_ns - start_ns;

    if (denominator == 0) {
        *out = (double)r1;
        return 0;
    }

    *out = (double)((long double)(r1 * denominator + sign * numerator) / (long double)denominator);
    return 0;
}

/* Check if adding sign days to a ZonedDateTime would stay within instant limits.
   For now, only supports UTC. Returns TRUE if in range or cannot determine. */
static BOOL ti_zoneddatetime_day_boundary_in_range(JSZonedDateTimeData *zdt, int sign)
{
    if (strcmp(zdt->timeZone, "UTC") != 0)
        return TRUE;
    {
        js_int128_t epoch_ns = ti_timespec_to_ns(&zdt->ts);
        int64_t ordinal = (int64_t)(epoch_ns / 86400000000000LL);
        int64_t time_ns = (int64_t)(epoch_ns % 86400000000000LL);
        if (time_ns < 0) {
            time_ns += 86400000000000LL;
            ordinal -= 1;
        }
        ordinal += sign;
        {
            js_int128_t new_epoch_ns = (js_int128_t)ordinal * 86400000000000LL + time_ns;
            js_int128_t limit = (js_int128_t)INSTANT_MAX_SECS * 1000000000LL;
            return (new_epoch_ns >= -limit && new_epoch_ns <= limit);
        }
    }
}

/* Convert a relativeTo string to a Temporal object (PlainDate, PlainDateTime,
   or ZonedDateTime). Returns the object or JS_EXCEPTION on error. */
static JSValue ti_to_temporal_relative_to_string(JSContext *ctx, const char *str)
{
    TISODateTimeParseResult r;
    memset(&r, 0, sizeof(r));
    if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0)
        return JS_ThrowRangeError(ctx, "invalid relativeTo string");
    if (!r.has_date)
        return JS_ThrowRangeError(ctx, "invalid relativeTo string");
    if (r.has_utc && !r.has_tz_annotation)
        return JS_ThrowRangeError(ctx, "invalid relativeTo string");

    if (r.has_tz_annotation) {
        /* ZonedDateTime: call ZonedDateTime.from directly for proper
           offset/timezone validation */
        JSValue str_val = JS_NewString(ctx, str);
        JSValue result = js_zoneddatetime_from(ctx, JS_UNDEFINED, 1, &str_val);
        JS_FreeValue(ctx, str_val);
        return result;
    } else {
        /* GetTemporalRelativeToOption returns PlainDate for all strings
           without time zone, discarding any time component. */
        if (r.has_calendar && r.calendar != CAL_ISO8601)
            return JS_ThrowRangeError(ctx, "invalid calendar ID");
        return js_create_plaindate(ctx, r.year, r.month, r.day);
    }
}

static JSValue ti_convert_relative_to_property_bag(JSContext *ctx, JSValueConst obj)
{
    TIPreparedFields fields;
    JSValue result;
    CalendarType cal = CAL_ISO8601;
    uint32_t field_mask = TI_FIELD_YEAR | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                          TI_FIELD_DAY | TI_FIELD_HOUR | TI_FIELD_MINUTE |
                          TI_FIELD_SECOND | TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND |
                          TI_FIELD_NANOSECOND | TI_FIELD_OFFSET | TI_FIELD_TIMEZONE;

    if (ti_resolve_calendar_from_object(ctx, obj, &cal) < 0)
        return JS_EXCEPTION;

    if (ti_prepare_calendar_fields(ctx, obj, field_mask, 0, 0, &fields, cal) < 0)
        return JS_EXCEPTION;

    if (fields.present_mask & TI_FIELD_TIMEZONE) {
        result = ti_create_zoneddatetime_from_fields(ctx, &fields, 1, JS_UNDEFINED);
    } else {
        /* GetTemporalRelativeToOption returns PlainDate for all objects
           without time zone, discarding any time component. */
        result = ti_create_plaindate_from_fields(ctx, &fields, 1);
    }

    return result;
}

/* Compute exact total for ISO calendar total() with years/months/weeks unit.
   This implements NudgeToCalendarUnit behavior for the ISO calendar.
   rel_time_ns and target_time_ns are the time-of-day nanosecond offsets
   (hour*3600*1e9 + minute*60*1e9 + second*1e9 + ms*1e6 + us*1e3 + ns). */
static double ti_iso_total_in_unit(int rel_year, int rel_month, int rel_day,
                             int target_year, int target_month, int target_day,
                             TIUnit unit, int64_t rel_time_ns, int64_t target_time_ns)
{
    int64_t years = 0, months = 0, weeks = 0, days = 0;
    int64_t start_ord, end_ord, target_ord;
    int sy, sm, sd, ey, em, ed;
    int sign;
    const int64_t DAY_NS = 86400000000000LL;

    target_ord = pdt_to_ordinal_days(target_year, target_month, target_day);

    if (unit == TI_UNIT_YEAR) {
        ti_iso_calendar_date_until(rel_year, rel_month, rel_day,
                                    target_year, target_month, target_day,
                                    TI_UNIT_YEAR, &years, &months, &weeks, &days);
        sign = (years < 0 || (years == 0 && months < 0) ||
                (years == 0 && months == 0 && weeks < 0) ||
                (years == 0 && months == 0 && weeks == 0 && days < 0)) ? -1 : 1;
        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, years, 0, 0, 0, &sy, &sm, &sd);
        start_ord = pdt_to_ordinal_days(sy, sm, sd);
        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, years + sign, 0, 0, 0, &ey, &em, &ed);
        end_ord = pdt_to_ordinal_days(ey, em, ed);
        if (end_ord == start_ord) end_ord = start_ord + sign; /* shouldn't happen */
        js_int128_t num = (js_int128_t)years * (end_ord - start_ord) * DAY_NS
                     + ((js_int128_t)(target_ord - start_ord) * DAY_NS + (target_time_ns - rel_time_ns)) * sign;
        js_int128_t den = (js_int128_t)(end_ord - start_ord) * DAY_NS;
        js_int128_t q = num / den;
        js_int128_t r = num % den;
        if (q <= 1 && q >= -1)
            return (double)num / (double)den;
        else
            return (double)q + (double)r / (double)den;
    } else if (unit == TI_UNIT_MONTH) {
        ti_iso_calendar_date_until(rel_year, rel_month, rel_day,
                                    target_year, target_month, target_day,
                                    TI_UNIT_MONTH, &years, &months, &weeks, &days);
        int64_t total_months = years * 12 + months;
        sign = (total_months < 0 || (total_months == 0 && weeks < 0) ||
                (total_months == 0 && weeks == 0 && days < 0)) ? -1 : 1;
        int64_t r1 = total_months;
        int64_t r2 = r1 + sign;

        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, r1, 0, 0, &sy, &sm, &sd);
        start_ord = pdt_to_ordinal_days(sy, sm, sd);
        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, r2, 0, 0, &ey, &em, &ed);
        end_ord = pdt_to_ordinal_days(ey, em, ed);

        js_int128_t startEpochNs = (js_int128_t)start_ord * DAY_NS + rel_time_ns;
        js_int128_t endEpochNs = (js_int128_t)end_ord * DAY_NS + rel_time_ns;
        js_int128_t destEpochNs = (js_int128_t)target_ord * DAY_NS + target_time_ns;

        BOOL in_bounds = (sign == 1) ? (startEpochNs <= destEpochNs && destEpochNs <= endEpochNs)
                                     : (endEpochNs <= destEpochNs && destEpochNs <= startEpochNs);
        if (!in_bounds) {
            r1 += sign;
            r2 += sign;
            ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, r1, 0, 0, &sy, &sm, &sd);
            start_ord = pdt_to_ordinal_days(sy, sm, sd);
            ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, r2, 0, 0, &ey, &em, &ed);
            end_ord = pdt_to_ordinal_days(ey, em, ed);
        }

        if (end_ord == start_ord) end_ord = start_ord + sign; /* shouldn't happen */

        js_int128_t num = (js_int128_t)r1 * (end_ord - start_ord) * DAY_NS
                     + ((js_int128_t)(target_ord - start_ord) * DAY_NS + (target_time_ns - rel_time_ns)) * sign;
        js_int128_t den = (js_int128_t)(end_ord - start_ord) * DAY_NS;
        js_int128_t q = num / den;
        js_int128_t r = num % den;
        if (q <= 1 && q >= -1)
            return (double)num / (double)den;
        else
            return (double)q + (double)r / (double)den;
    } else if (unit == TI_UNIT_WEEK) {
        ti_iso_calendar_date_until(rel_year, rel_month, rel_day,
                                    target_year, target_month, target_day,
                                    TI_UNIT_WEEK, &years, &months, &weeks, &days);
        sign = (weeks < 0 || (weeks == 0 && days < 0)) ? -1 : 1;
        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, 0, weeks, 0, &sy, &sm, &sd);
        start_ord = pdt_to_ordinal_days(sy, sm, sd);
        ti_iso_calendar_date_add(rel_year, rel_month, rel_day, 0, 0, weeks + sign, 0, &ey, &em, &ed);
        end_ord = pdt_to_ordinal_days(ey, em, ed);
        if (end_ord == start_ord) end_ord = start_ord + sign; /* shouldn't happen */
        js_int128_t num = (js_int128_t)weeks * (end_ord - start_ord) * DAY_NS
                     + ((js_int128_t)(target_ord - start_ord) * DAY_NS + (target_time_ns - rel_time_ns)) * sign;
        js_int128_t den = (js_int128_t)(end_ord - start_ord) * DAY_NS;
        js_int128_t q = num / den;
        js_int128_t r = num % den;
        if (q <= 1 && q >= -1)
            return (double)num / (double)den;
        else
            return (double)q + (double)r / (double)den;
    }
    return 0.0;
}

static JSValue js_duration_total(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)argc;
    JSDurationData *s;
    TIUnit unit_enum = TI_UNIT_INVALID;
    JSValue relativeTo_val = JS_UNDEFINED;
    double total;
    int has_relativeTo = 0;
    int is_calendar_unit;
    int has_calendar_units;

    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "total requires a unit argument");

    if (!JS_IsString(argv[0]) && !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "total requires a unit argument");

    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (JS_IsString(argv[0])) {
        /* String shorthand: unit is the argument, relativeTo is undefined */
        const char *unit_str = JS_ToCString(ctx, argv[0]);
        if (!unit_str)
            return JS_EXCEPTION;
        unit_enum = ti_string_to_unit(unit_str, strlen(unit_str));
        JS_FreeCString(ctx, unit_str);
        if (unit_enum == TI_UNIT_INVALID)
            return JS_ThrowRangeError(ctx, "invalid unit");
    } else {
        /* Object argument: read relativeTo first, then unit */
        JSValue options = JS_ToObject(ctx, argv[0]);
        if (JS_IsException(options))
            return JS_EXCEPTION;

        relativeTo_val = JS_GetPropertyStr(ctx, options, "relativeTo");
        if (JS_IsException(relativeTo_val)) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(relativeTo_val)) {
            if (!JS_IsString(relativeTo_val) && !JS_IsObject(relativeTo_val)) {
                JS_FreeValue(ctx, options);
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowTypeError(ctx, "invalid relativeTo");
            }
            has_relativeTo = 1;
        }

        /* Convert relativeTo string to Temporal object */
        if (has_relativeTo && JS_IsString(relativeTo_val)) {
            const char *rt_str = JS_ToCString(ctx, relativeTo_val);
            if (!rt_str) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
            JS_FreeValue(ctx, relativeTo_val);
            relativeTo_val = ti_to_temporal_relative_to_string(ctx, rt_str);
            JS_FreeCString(ctx, rt_str);
            if (JS_IsException(relativeTo_val)) {
                JS_FreeValue(ctx, options);
                goto fail;
            }
        }
        /* Convert PlainDateTime objects to PlainDate per GetTemporalRelativeToOption */
        if (has_relativeTo && JS_IsObject(relativeTo_val)) {
            JSPlainDateTimeData *pdt = JS_GetOpaque(relativeTo_val, js_plaindatetime_class_id);
            if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
                JSValue plainDate = js_create_plaindate(ctx, pdt->year, pdt->month, pdt->day);
                JS_FreeValue(ctx, relativeTo_val);
                relativeTo_val = plainDate;
                if (JS_IsException(relativeTo_val)) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
            }
        }
        /* Validate and convert relativeTo property bag before reading unit */
        if (has_relativeTo && JS_IsObject(relativeTo_val)) {
            if (!JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id) &&
                !JS_GetOpaque(relativeTo_val, js_plaindate_class_id) &&
                !JS_GetOpaque(relativeTo_val, js_plaindatetime_class_id)) {
                JSValue converted = ti_convert_relative_to_property_bag(ctx, relativeTo_val);
                JS_FreeValue(ctx, relativeTo_val);
                relativeTo_val = converted;
                if (JS_IsException(relativeTo_val)) {
                    JS_FreeValue(ctx, options);
                    goto fail;
                }
            }
        }

        if (ti_parse_unit_option(ctx, options, "unit", &unit_enum, 0, 0) < 0) {
            JS_FreeValue(ctx, options);
            goto fail;
        }
        JS_FreeValue(ctx, options);
    }

    is_calendar_unit = (unit_enum >= TI_UNIT_YEAR && unit_enum <= TI_UNIT_WEEK);
    has_calendar_units = duration_has_calendar_units(s);

    /* Extract ISO date fields from relativeTo object */
    int rel_year = 0, rel_month = 0, rel_day = 0;
    int rel_hour = 0, rel_minute = 0, rel_second = 0, rel_ms = 0, rel_us = 0, rel_ns = 0;
    int use_iso = 0;
    if (has_relativeTo && JS_IsObject(relativeTo_val)) {
        JSPlainDateData *pd = JS_GetOpaque(relativeTo_val, js_plaindate_class_id);
        if (pd && (pd->calendar == CAL_ISO8601 || pd->calendar == CAL_GREGORY)) {
            rel_year = pd->year; rel_month = pd->month; rel_day = pd->day;
            use_iso = 1;
        } else {
            JSPlainDateTimeData *pdt = JS_GetOpaque(relativeTo_val, js_plaindatetime_class_id);
            if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
                rel_year = pdt->year; rel_month = pdt->month; rel_day = pdt->day;
                rel_hour = pdt->hour; rel_minute = pdt->minute; rel_second = pdt->second;
                rel_ms = pdt->millisecond; rel_us = pdt->microsecond; rel_ns = pdt->nanosecond;
                use_iso = 1;
            } else {
                JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
                if (zdt && (zdt->calendar == CAL_ISO8601 || zdt->calendar == CAL_GREGORY)) {
                    int64_t offset_sec = 0;
                    int tz_id = ti_tz_find_id(zdt->timeZone);
                    if (tz_id >= 0) {
                        offset_sec = ti_tz_offset_at_sec(tz_id, zdt->ts.secs);
                    } else {
                        int64_t offset_ns = 0;
                        int ret = ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                        if (ret >= 0)
                            offset_sec = offset_ns / 1000000000LL;
                    }
                    int64_t local_sec = zdt->ts.secs + offset_sec;
                    int local_nsec = (int)zdt->ts.nsecs;
                    if (local_nsec >= 1000000000) {
                        local_sec += 1;
                        local_nsec -= 1000000000;
                    } else if (local_nsec < 0) {
                        local_sec -= 1;
                        local_nsec += 1000000000;
                    }
                    {
                        int64_t year64;
                        ti_epoch_sec_to_date(local_sec, &year64, &rel_month, &rel_day, &rel_hour, &rel_minute, &rel_second);
                        rel_year = (int)year64;
                    }
                    rel_ms = local_nsec / 1000000;
                    rel_us = (local_nsec / 1000) % 1000;
                    rel_ns = local_nsec % 1000;
                    use_iso = 1;
                }
            }
        }
    }

    /* If no relativeTo and either the duration or the unit involves calendar units, throw */
    if (!has_relativeTo && (has_calendar_units || is_calendar_unit)) {
        JS_FreeValue(ctx, relativeTo_val);
        return JS_ThrowRangeError(ctx, "a starting point is required for durations with calendar units");
    }

    js_int128_t total_ns;
    int ty, tm, td, th, tmin, ts, tms, tus, tns;

    /* Zero duration always totals to 0, unless ZonedDateTime relativeTo
       requires NudgeToCalendarUnit validation for days. */
    if (s->years == 0 && s->months == 0 && s->weeks == 0 && s->days == 0 &&
        s->hours == 0 && s->minutes == 0 && s->seconds == 0 &&
        s->milliseconds == 0 && s->microseconds.int128_value == 0 && s->nanoseconds.int128_value == 0) {
        JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
        if (zdt && unit_enum == TI_UNIT_DAY) {
            if (!ti_zoneddatetime_day_boundary_in_range(zdt, 1)) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "relativeTo outside valid instant range");
            }
        }
        JS_FreeValue(ctx, relativeTo_val);
        return JS_NewFloat64(ctx, 0);
    }

    /* ZonedDateTime relativeTo with ISO-like calendar: use AddZonedDateTime
       + DifferenceZonedDateTime per spec. */
    {
        JSZonedDateTimeData *zdt = NULL;
        if (has_relativeTo && JS_IsObject(relativeTo_val))
            zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
        if (zdt && (zdt->calendar == CAL_ISO8601 || zdt->calendar == CAL_GREGORY)) {
            JSTimeSpec origin = zdt->ts;
            JSTimeSpec target;
            JSDurationData add_d = *s;
            if (ti_zoneddatetime_add_duration_iso(zdt->timeZone, zdt->calendar, origin, &add_d, 1, &target) < 0) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
            }
            js_int128_t zdt_total_ns = ti_timespec_to_ns(&target)
                                  - (ti_timespec_to_ns(&origin));

            if (unit_enum == TI_UNIT_DAY) {
                JSDurationData diff;
                if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, TI_UNIT_DAY, &diff) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                if (duration_total_nudge(zdt->timeZone, zdt->calendar, origin, target, &diff, TI_UNIT_DAY, &total) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                JS_FreeValue(ctx, relativeTo_val);
                return JS_NewFloat64(ctx, total);
            } else if (unit_enum == TI_UNIT_YEAR) {
                JSDurationData diff;
                if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, TI_UNIT_YEAR, &diff) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                if (duration_total_nudge(zdt->timeZone, zdt->calendar, origin, target, &diff, TI_UNIT_YEAR, &total) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                JS_FreeValue(ctx, relativeTo_val);
                return JS_NewFloat64(ctx, total);
            } else if (unit_enum == TI_UNIT_MONTH) {
                JSDurationData diff;
                if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, TI_UNIT_MONTH, &diff) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                if (duration_total_nudge(zdt->timeZone, zdt->calendar, origin, target, &diff, TI_UNIT_MONTH, &total) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                JS_FreeValue(ctx, relativeTo_val);
                return JS_NewFloat64(ctx, total);
            } else if (unit_enum == TI_UNIT_WEEK) {
                JSDurationData diff;
                if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, TI_UNIT_WEEK, &diff) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                if (duration_total_nudge(zdt->timeZone, zdt->calendar, origin, target, &diff, TI_UNIT_WEEK, &total) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                }
                JS_FreeValue(ctx, relativeTo_val);
                return JS_NewFloat64(ctx, total);
            } else if (unit_enum == TI_UNIT_HOUR) {
                total = duration_total_div(zdt_total_ns, 3600000000000ULL);
            } else if (unit_enum == TI_UNIT_MINUTE) {
                total = duration_total_div(zdt_total_ns, 60000000000ULL);
            } else if (unit_enum == TI_UNIT_SECOND) {
                total = duration_total_div(zdt_total_ns, 1000000000ULL);
            } else if (unit_enum == TI_UNIT_MILLISECOND) {
                total = duration_total_div(zdt_total_ns, 1000000ULL);
            } else if (unit_enum == TI_UNIT_MICROSECOND) {
                total = duration_total_div(zdt_total_ns, 1000ULL);
            } else if (unit_enum == TI_UNIT_NANOSECOND) {
                total = (double)zdt_total_ns;
            } else {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "unit out of range");
            }
            JS_FreeValue(ctx, relativeTo_val);
            return JS_NewFloat64(ctx, total);
        }
    }

    /* Compute total nanoseconds as exact integer, then divide to avoid
       floating-point rounding order differences. For ISO calendar relativeTo
       with calendar-unit durations, compute the actual difference between
       relativeTo and relativeTo + duration. */
    if (has_relativeTo && (has_calendar_units || s->days != 0 || is_calendar_unit) && use_iso) {
        js_int128_t us_val = s->microseconds.int128_value;
        js_int128_t ns_val = s->nanoseconds.int128_value;
        if (ti_iso_datetime_add_duration(
            rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
            s->years, s->months, s->weeks, s->days,
            s->hours, s->minutes, s->seconds, s->milliseconds, us_val, ns_val,
            &ty, &tm, &td, &th, &tmin, &ts, &tms, &tus, &tns) < 0) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
            }
            if (!ti_is_date_within_iso_limits(ty, tm, td)) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
            }
            if (JS_GetOpaque(relativeTo_val, js_plaindate_class_id)) {
                if (!pdt_is_within_limits(rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns) ||
                    !pdt_is_within_limits(ty, tm, td, th, tmin, ts, tms, tus, tns)) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                }
            }
            {
                int64_t rel_sec = pdt_to_ordinal_days(rel_year, rel_month, rel_day) * 86400LL + rel_hour * 3600LL + rel_minute * 60LL + rel_second;
                int64_t target_sec = pdt_to_ordinal_days(ty, tm, td) * 86400LL + th * 3600LL + tmin * 60LL + ts;
                js_int128_t rel_epoch_ns = (js_int128_t)rel_sec * 1000000000LL + (js_int128_t)rel_ms * 1000000LL + (js_int128_t)rel_us * 1000LL + (js_int128_t)rel_ns;
                js_int128_t target_epoch_ns = (js_int128_t)target_sec * 1000000000LL + (js_int128_t)tms * 1000000LL + (js_int128_t)tus * 1000LL + (js_int128_t)tns;
                total_ns = target_epoch_ns - rel_epoch_ns;
            }
            /* NudgeToCalendarUnit validation: for calendar units with PlainDate
               or ZonedDateTime, ensure relativeTo + {unit: sign} is within ISO limits. */
            if (is_calendar_unit && (JS_GetOpaque(relativeTo_val, js_plaindate_class_id) ||
                                     (js_zoneddatetime_class_id && JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id)))) {
                int sign = (total_ns > 0) ? 1 : (total_ns < 0) ? -1 : 0;
                if (sign != 0) {
                    int ny, nm, nd;
                    int64_t add_y = 0, add_m = 0, add_w = 0, add_d = 0;
                    if (unit_enum == TI_UNIT_YEAR) add_y = sign;
                    else if (unit_enum == TI_UNIT_MONTH) add_m = sign;
                    else if (unit_enum == TI_UNIT_WEEK) add_w = sign;
                    else if (unit_enum == TI_UNIT_DAY) add_d = sign;
                    if (ti_iso_calendar_date_add(rel_year, rel_month, rel_day, add_y, add_m, add_w, add_d, &ny, &nm, &nd) < 0 ||
                        !ti_is_date_within_iso_limits(ny, nm, nd)) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "total result outside valid ISO range");
                    }
                }
            }
        } else {
            if (has_relativeTo && use_iso && JS_GetOpaque(relativeTo_val, js_plaindate_class_id)) {
                int ety, etm, etd, eth, etmin, ets, etms, etus, etns;
                if (ti_iso_datetime_add_duration(
                    rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                    0, 0, 0, 0,
                    s->hours, s->minutes, s->seconds, s->milliseconds,
                    s->microseconds.int128_value, s->nanoseconds.int128_value,
                    &ety, &etm, &etd, &eth, &etmin, &ets, &etms, &etus, &etns) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                }
                if (!pdt_is_within_limits(rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns) ||
                    !pdt_is_within_limits(ety, etm, etd, eth, etmin, ets, etms, etus, etns)) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                }
            }
            {
                int64_t total_seconds = s->days * 86400LL +
                                        s->hours * 3600LL +
                                        s->minutes * 60LL +
                                        s->seconds;
                total_ns = (js_int128_t)total_seconds * 1000000000LL +
                           (js_int128_t)s->milliseconds * 1000000LL +
                           s->microseconds.int128_value * 1000LL +
                           s->nanoseconds.int128_value;
            }
        }

        /* For ZonedDateTime relativeTo with time-only durations, validate
           that the result epoch nanoseconds are within Instant limits. */
        if (has_relativeTo && JS_IsObject(relativeTo_val)) {
            JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
            if (zdt && s->years == 0 && s->months == 0 && s->weeks == 0 && s->days == 0) {
                js_int128_t zdt_epoch_ns = ti_timespec_to_ns(&zdt->ts);
                js_int128_t target_epoch_ns = zdt_epoch_ns + total_ns;
                js_int128_t limit = (js_int128_t)INSTANT_MAX_SECS * 1000000000LL;
                if (target_epoch_ns < -limit || target_epoch_ns > limit) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "epoch nanoseconds outside valid limits");
                }
            }
        }

        if (unit_enum == TI_UNIT_DAY) {
            total = duration_total_div(total_ns, 86400000000000ULL);
        } else if (unit_enum == TI_UNIT_HOUR) {
            total = duration_total_div(total_ns, 3600000000000ULL);
        } else if (unit_enum == TI_UNIT_MINUTE) {
            total = duration_total_div(total_ns, 60000000000ULL);
        } else if (unit_enum == TI_UNIT_SECOND) {
            total = duration_total_div(total_ns, 1000000000ULL);
        } else if (unit_enum == TI_UNIT_MILLISECOND) {
            total = duration_total_div(total_ns, 1000000ULL);
        } else if (unit_enum == TI_UNIT_MICROSECOND) {
            total = duration_total_div(total_ns, 1000ULL);
        } else if (unit_enum == TI_UNIT_NANOSECOND) {
            total = (double)total_ns;
        } else {
            /* Not a fixed-length time unit, fall through to calendar unit checks */
            goto calendar_units;
        }

    JS_FreeValue(ctx, relativeTo_val);
    return JS_NewFloat64(ctx, total);

calendar_units:
    if (has_relativeTo && use_iso) {
        int64_t rel_time_ns = (int64_t)rel_hour * 3600000000000LL
                            + (int64_t)rel_minute * 60000000000LL
                            + (int64_t)rel_second * 1000000000LL
                            + (int64_t)rel_ms * 1000000LL
                            + (int64_t)rel_us * 1000LL
                            + (int64_t)rel_ns;
        int64_t target_time_ns = (int64_t)th * 3600000000000LL
                               + (int64_t)tmin * 60000000000LL
                               + (int64_t)ts * 1000000000LL
                               + (int64_t)tms * 1000000LL
                               + (int64_t)tus * 1000LL
                               + (int64_t)tns;
        total = ti_iso_total_in_unit(rel_year, rel_month, rel_day, ty, tm, td, unit_enum, rel_time_ns, target_time_ns);
        JS_FreeValue(ctx, relativeTo_val);
        return JS_NewFloat64(ctx, total);
    }

    goto fail_range;

fail_range:
    JS_FreeValue(ctx, relativeTo_val);
    return JS_ThrowRangeError(ctx, "unit out of range");
fail:
    JS_FreeValue(ctx, relativeTo_val);
    return JS_EXCEPTION;
}

static void duration_balance_subseconds(JSDurationData *d)
{
    js_int128_t total_ns;
    /* Balance nanoseconds -> microseconds -> milliseconds -> seconds only */
    total_ns = d->nanoseconds.int128_value;
    d->nanoseconds.int128_value = total_ns % 1000;
    d->microseconds.int128_value += total_ns / 1000;
    
    total_ns = d->microseconds.int128_value;
    d->microseconds.int128_value = total_ns % 1000;
    d->milliseconds += (int64_t)(total_ns / 1000);
    
    total_ns = d->milliseconds;
    d->milliseconds = (int64_t)(total_ns % 1000);
    d->seconds += (int64_t)(total_ns / 1000);
}

/* Temporal.Duration.prototype.toString */
static int duration_sign(const JSDurationData *d)
{
    if (d->years < 0) return -1;
    if (d->years > 0) return 1;
    if (d->months < 0) return -1;
    if (d->months > 0) return 1;
    if (d->weeks < 0) return -1;
    if (d->weeks > 0) return 1;
    if (d->days < 0) return -1;
    if (d->days > 0) return 1;
    if (d->hours < 0) return -1;
    if (d->hours > 0) return 1;
    if (d->minutes < 0) return -1;
    if (d->minutes > 0) return 1;
    if (d->seconds < 0) return -1;
    if (d->seconds > 0) return 1;
    if (d->milliseconds < 0) return -1;
    if (d->milliseconds > 0) return 1;
    if (d->microseconds.int128_value < 0) return -1;
    if (d->microseconds.int128_value > 0) return 1;
    if (d->nanoseconds.int128_value < 0) return -1;
    if (d->nanoseconds.int128_value > 0) return 1;
    return 0;
}

static js_int128_t round_int128_to_increment(js_int128_t x, js_int128_t increment, TIRoundingMode mode)
{
    js_int128_t q, r, q_euclid, r_euclid, result;

    if (increment == 1)
        return x;

    q = x / increment;
    r = x % increment;

    if (r < 0) {
        q_euclid = q - 1;
        r_euclid = r + increment;
    } else {
        q_euclid = q;
        r_euclid = r;
    }

    if (r_euclid == 0)
        return x;

    switch (mode) {
    case ROUND_CEIL:
        result = (q_euclid + 1) * increment;
        break;
    case ROUND_FLOOR:
        result = q_euclid * increment;
        break;
    case ROUND_TRUNC:
        result = (x >= 0 ? q_euclid : (q_euclid + 1)) * increment;
        break;
    case ROUND_EXPAND:
        result = (x >= 0 ? (q_euclid + 1) : q_euclid) * increment;
        break;
    case ROUND_HALF_EVEN:
        if (r_euclid * 2 < increment)
            result = q_euclid * increment;
        else if (r_euclid * 2 > increment)
            result = (q_euclid + 1) * increment;
        else
            result = ((q_euclid % 2) == 0) ? q_euclid * increment : (q_euclid + 1) * increment;
        break;
    case ROUND_HALF_CEIL:
        result = (r_euclid * 2 < increment) ? q_euclid * increment : (q_euclid + 1) * increment;
        break;
    case ROUND_HALF_FLOOR:
        result = (r_euclid * 2 <= increment) ? q_euclid * increment : (q_euclid + 1) * increment;
        break;
    case ROUND_HALF_EXPAND:
        result = (r_euclid * 2 < increment) ? q_euclid * increment : (q_euclid + 1) * increment;
        break;
    case ROUND_HALF_TRUNC:
        if (r_euclid * 2 < increment)
            result = q_euclid * increment;
        else if (r_euclid * 2 > increment)
            result = (q_euclid + 1) * increment;
        else
            result = (x >= 0 ? q_euclid : (q_euclid + 1)) * increment;
        break;
    default:
        result = x;
        break;
    }
    return result;
}

static JSValue js_duration_toString(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSDurationData *s;
    JSDurationData d;
    DynBuf dbuf;
    JSValue options = JS_UNDEFINED;
    int precision = -1; /* -1 = auto, 0-9 = digits */
    TIRoundingMode roundingMode = ROUND_TRUNC;
    int has_smallestUnit = 0;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int64_t increment_ns = 1;
    int needs_rounding = 0;
    js_int128_t total_ns, rounded_ns;

    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;

    /* Step 3: GetOptionsObject */
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (!JS_IsObject(argv[0]))
            return JS_ThrowTypeError(ctx, "options must be an object");
        options = JS_DupValue(ctx, argv[0]);
    }

    /* Read fractionalSecondDigits */
    if (!JS_IsUndefined(options)) {
        if (ti_get_fractional_second_digits_option(ctx, options, &precision) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
    }

    /* Read roundingMode */
    if (!JS_IsUndefined(options)) {
        if (ti_get_rounding_mode_option_default(ctx, options, &roundingMode, ROUND_TRUNC) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
    }

    /* Read smallestUnit */
    if (!JS_IsUndefined(options)) {
        TIUnit unit;
        if (ti_parse_unit_option(ctx, options, "smallestUnit", &unit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        if (unit != TI_UNIT_INVALID) {
            smallestUnit = unit;
            has_smallestUnit = 1;
        }
    }

    JS_FreeValue(ctx, options);

    /* Validate smallestUnit */
    if (has_smallestUnit) {
        if (smallestUnit == TI_UNIT_INVALID) {
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        }
        /* Duration toString: hour and minute are invalid */
        if (smallestUnit == TI_UNIT_HOUR || smallestUnit == TI_UNIT_MINUTE) {
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        }
        /* Must be a time unit (second, ms, us, ns) */
        if (smallestUnit < TI_UNIT_SECOND || smallestUnit > TI_UNIT_NANOSECOND) {
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        }
        /* smallestUnit overrides fractionalSecondDigits */
        switch (smallestUnit) {
        case TI_UNIT_SECOND: precision = 0; break;
        case TI_UNIT_MILLISECOND: precision = 3; break;
        case TI_UNIT_MICROSECOND: precision = 6; break;
        case TI_UNIT_NANOSECOND: precision = 9; break;
        default: break;
        }
    }

    /* Compute increment in nanoseconds */
    if (precision == -1) {
        increment_ns = 1;
    } else {
        static const int64_t inc_table[10] = {
            1000000000LL, 100000000LL, 10000000LL, 1000000LL,
            100000LL, 10000LL, 1000LL, 100LL, 10LL, 1LL
        };
        increment_ns = inc_table[precision];
    }

    /* Determine if rounding is needed */
    if (increment_ns == 1) {
        needs_rounding = 0;
    } else {
        needs_rounding = 1;
    }

    d = *s;
    duration_round_fields_to_float64(&d);
    duration_balance_subseconds(&d);

    if (needs_rounding) {
        /* Convert time components to total nanoseconds */
        {
            int64_t total_seconds = d.hours * 3600LL + d.minutes * 60LL + d.seconds;
            total_ns = (js_int128_t)total_seconds * 1000000000LL;
            total_ns += (js_int128_t)d.milliseconds * 1000000LL;
            total_ns += d.microseconds.int128_value * 1000LL;
            total_ns += d.nanoseconds.int128_value;
        }

        /* Round to increment */
        rounded_ns = round_int128_to_increment(total_ns, increment_ns, roundingMode);

        /* Check maxTimeDuration */
        {
            const js_int128_t MAX_TIME_DURATION = (((js_int128_t)9007199254740992LL) * 1000000000LL) - 1;
            if (rounded_ns < 0) {
                if (-rounded_ns > MAX_TIME_DURATION)
                    return JS_ThrowRangeError(ctx, "rounded duration out of range");
            } else {
                if (rounded_ns > MAX_TIME_DURATION)
                    return JS_ThrowRangeError(ctx, "rounded duration out of range");
            }
        }

        /* Determine roundedLargestUnit */
        TIUnit largestUnit = duration_get_largest_unit(&d);
        TIUnit roundedLargestUnit;
        if (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH ||
            largestUnit == TI_UNIT_WEEK || largestUnit == TI_UNIT_DAY) {
            roundedLargestUnit = largestUnit;
        } else if (largestUnit == TI_UNIT_HOUR) {
            roundedLargestUnit = TI_UNIT_HOUR;
        } else if (largestUnit == TI_UNIT_MINUTE) {
            roundedLargestUnit = TI_UNIT_MINUTE;
        } else {
            roundedLargestUnit = TI_UNIT_SECOND;
        }

        /* Zero out time components and set nanoseconds to rounded value */
        d.hours = 0;
        d.minutes = 0;
        d.seconds = 0;
        d.milliseconds = 0;
        d.microseconds.int128_value = 0;
        d.nanoseconds.int128_value = rounded_ns;

        /* Balance time components up to roundedLargestUnit */
        ti_balance_time_duration(&d, roundedLargestUnit);

        /* Check if result is out of range */
        if (duration_is_out_of_range(&d))
            return JS_ThrowRangeError(ctx, "rounded duration out of range");
    }

    /* Format using TemporalDurationToString */
    dbuf_init2(&dbuf, ctx, (DynBufReallocFunc *)js_realloc);
    if (dbuf_error(&dbuf))
        return JS_EXCEPTION;

    {
        int sign = duration_sign(&d);
        int has_time = 0;
        char time_buf[256];
        int time_len = 0;

        /* Compute time part first */
        if (d.hours != 0) {
            has_time = 1;
            time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len,
                                 "%"PRIi64"H", d.hours < 0 ? -d.hours : d.hours);
        }
        if (d.minutes != 0) {
            has_time = 1;
            time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len,
                                 "%"PRIi64"M", d.minutes < 0 ? -d.minutes : d.minutes);
        }

        /* Determine zeroMinutesAndHigher */
        int zeroMinutesAndHigher = 0;
        TIUnit defaultLargestUnit = duration_get_largest_unit(&d);
        if (defaultLargestUnit == TI_UNIT_SECOND || defaultLargestUnit == TI_UNIT_MILLISECOND ||
            defaultLargestUnit == TI_UNIT_MICROSECOND || defaultLargestUnit == TI_UNIT_NANOSECOND) {
            zeroMinutesAndHigher = 1;
        }

        /* secondsDuration = time components from seconds down */
        js_int128_t secondsDuration = (js_int128_t)d.seconds * 1000000000LL +
                                   (js_int128_t)d.milliseconds * 1000000LL +
                                   d.microseconds.int128_value * 1000LL +
                                   d.nanoseconds.int128_value;

        if (secondsDuration != 0 || zeroMinutesAndHigher || precision != -1) {
            has_time = 1;
            js_int128_t sec128 = secondsDuration / 1000000000LL;
            if (sec128 < 0) sec128 = -sec128;
            int64_t sec = (int64_t)sec128;

            js_int128_t subsec128 = secondsDuration % 1000000000LL;
            if (subsec128 < 0) subsec128 = -subsec128;
            int64_t subsec_ns = (int64_t)subsec128;

            time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len,
                                 "%"PRIi64, sec);

            /* FormatFractionalSeconds */
            if (precision == -1) {
                /* auto */
                if (subsec_ns != 0) {
                    char frac[16];
                    snprintf(frac, sizeof(frac), "%09"PRIi64, subsec_ns);
                    int frac_len = 9;
                    while (frac_len > 0 && frac[frac_len - 1] == '0')
                        frac_len--;
                    frac[frac_len] = '\0';
                    time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len,
                                         ".%s", frac);
                }
            } else if (precision > 0) {
                char frac[16];
                snprintf(frac, sizeof(frac), "%09"PRIi64, subsec_ns);
                frac[precision] = '\0';
                time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len,
                                     ".%s", frac);
            }
            time_len += snprintf(time_buf + time_len, sizeof(time_buf) - (size_t)time_len, "S");
        }

        /* Output sign and date part */
        if (sign < 0)
            dbuf_putstr(&dbuf, "-");
        dbuf_putstr(&dbuf, "P");

        if (d.years != 0)
            dbuf_printf(&dbuf, "%"PRIi64"Y", d.years < 0 ? -d.years : d.years);
        if (d.months != 0)
            dbuf_printf(&dbuf, "%"PRIi64"M", d.months < 0 ? -d.months : d.months);
        if (d.weeks != 0)
            dbuf_printf(&dbuf, "%"PRIi64"W", d.weeks < 0 ? -d.weeks : d.weeks);
        if (d.days != 0)
            dbuf_printf(&dbuf, "%"PRIi64"D", d.days < 0 ? -d.days : d.days);

        if (has_time) {
            dbuf_putstr(&dbuf, "T");
            dbuf_putstr(&dbuf, time_buf);
        }
    }

    dbuf_putc(&dbuf, '\0');
    if (dbuf_error(&dbuf)) {
        dbuf_free(&dbuf);
        return JS_ThrowOutOfMemory(ctx);
    }
    {
        JSValue ret = JS_NewString(ctx, (const char *)dbuf.buf);
        dbuf_free(&dbuf);
        return ret;
    }
}
static JSValue js_duration_toJSON(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_duration_toString(ctx, this_val, 0, NULL);
}

static JSValue js_duration_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    JSDurationData *s;
    JSValue df_ctor, df_obj, result;
    JSValue args[2];
    int i;

    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;

    df_ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DURATIONFORMAT);

    for (i = 0; i < argc && i < 2; i++)
        args[i] = argv[i];
    for (; i < 2; i++)
        args[i] = JS_UNDEFINED;

    df_obj = JS_CallConstructor(ctx, df_ctor, argc, args);
    JS_FreeValue(ctx, df_ctor);
    if (JS_IsException(df_obj))
        return JS_EXCEPTION;

    {
        JSValue format_fn = JS_GetPropertyStr(ctx, df_obj, "format");
        if (JS_IsException(format_fn)) {
            JS_FreeValue(ctx, format_fn);
            JS_FreeValue(ctx, df_obj);
            return JS_EXCEPTION;
        }
        JSValue dur = JS_DupValue(ctx, this_val);
        result = JS_Call(ctx, format_fn, df_obj, 1, &dur);
        JS_FreeValue(ctx, format_fn);
        JS_FreeValue(ctx, dur);
    }
    JS_FreeValue(ctx, df_obj);
    return result;
}

static int64_t ti_unit_to_nanoseconds(TIUnit unit)
{
    switch (unit) {
    case TI_UNIT_DAY: return 86400000000000LL;
    case TI_UNIT_HOUR: return 3600000000000LL;
    case TI_UNIT_MINUTE: return 60000000000LL;
    case TI_UNIT_SECOND: return 1000000000LL;
    case TI_UNIT_MILLISECOND: return 1000000LL;
    case TI_UNIT_MICROSECOND: return 1000LL;
    case TI_UNIT_NANOSECOND: return 1LL;
    default: return 0;
    }
}

static JSValue js_duration_round(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    (void)argc;
    JSDurationData *s;
    JSValue options, relativeTo_val = JS_UNDEFINED;
    const char *smallestUnit = NULL;
    TIRoundingMode mode = ROUND_HALF_EXPAND;
    int64_t roundingIncrement = 1;
    TIUnit unit = TI_UNIT_INVALID;
    js_int128_t total_ns, rounded_ns, unit_ns, inc_ns, day_time_ns;
    int64_t time_ns;
    JSDurationData result = {0};
    int has_relativeTo = 0;
    TIUnit largestUnit = TI_UNIT_INVALID;

    s = JS_GetOpaque2(ctx, this_val, js_duration_class_id);
    if (!s)
        return JS_EXCEPTION;

    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "roundTo is required");

    if (JS_IsString(argv[0])) {
        /* If argument is a string, use it as smallestUnit */
        size_t su_len;
        smallestUnit = JS_ToCStringLen(ctx, &su_len, argv[0]);
        if (!smallestUnit)
            return JS_EXCEPTION;
        options = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(options)) {
            JS_FreeCString(ctx, smallestUnit);
            return JS_EXCEPTION;
        }
        {
            JSValue su_str = JS_NewStringLen(ctx, smallestUnit, su_len);
            JS_FreeCString(ctx, smallestUnit);
            if (JS_IsException(su_str)) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
            if (JS_SetPropertyStr(ctx, options, "smallestUnit", su_str) < 0) {
                JS_FreeValue(ctx, options);
                return JS_EXCEPTION;
            }
        }
    } else if (JS_IsObject(argv[0])) {
        options = JS_ToObject(ctx, argv[0]);
        if (JS_IsException(options))
            return JS_EXCEPTION;
    } else {
        return JS_ThrowTypeError(ctx, "roundTo must be a string or object");
    }

    {
        int has_smallest_unit = 0, has_largest_unit = 0;
        if (ti_parse_unit_option(ctx, options, "largestUnit", &largestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED | TI_UNIT_OPT_ALLOW_AUTO, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, options);
            JS_FreeValue(ctx, relativeTo_val);
            return JS_EXCEPTION;
        }
        has_largest_unit = (largestUnit != TI_UNIT_INVALID);

        /* Read and validate relativeTo */
        {
            JSValue rt = JS_GetPropertyStr(ctx, options, "relativeTo");
            if (JS_IsException(rt)) {
                JS_FreeValue(ctx, options);
                JS_FreeValue(ctx, relativeTo_val);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(rt)) {
                has_relativeTo = 1;
                if (JS_IsString(rt)) {
                    const char *rt_str = JS_ToCString(ctx, rt);
                    if (!rt_str) {
                        JS_FreeValue(ctx, rt);
                        JS_FreeValue(ctx, options);
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_EXCEPTION;
                    }
                    if (ti_is_minus_zero_year(rt_str)) {
                        JS_FreeCString(ctx, rt_str);
                        JS_FreeValue(ctx, rt);
                        JS_FreeValue(ctx, options);
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "minus zero year is not allowed");
                    }
                    JS_FreeValue(ctx, rt);
                    rt = ti_to_temporal_relative_to_string(ctx, rt_str);
                    JS_FreeCString(ctx, rt_str);
                    if (JS_IsException(rt)) {
                        JS_FreeValue(ctx, options);
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_EXCEPTION;
                    }
                    relativeTo_val = rt;
                } else if (JS_IsObject(rt)) {
                    JSPlainDateTimeData *pdt = JS_GetOpaque(rt, js_plaindatetime_class_id);
                    if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
                        JSValue plainDate = js_create_plaindate(ctx, pdt->year, pdt->month, pdt->day);
                        JS_FreeValue(ctx, rt);
                        rt = plainDate;
                        if (JS_IsException(rt)) {
                            JS_FreeValue(ctx, options);
                            JS_FreeValue(ctx, relativeTo_val);
                            return JS_EXCEPTION;
                        }
                    }
                    if (!JS_GetOpaque(rt, js_zoneddatetime_class_id) &&
                        !JS_GetOpaque(rt, js_plaindate_class_id) &&
                        !JS_GetOpaque(rt, js_plaindatetime_class_id)) {
                        JSValue converted = ti_convert_relative_to_property_bag(ctx, rt);
                        JS_FreeValue(ctx, rt);
                        rt = converted;
                        if (JS_IsException(rt)) {
                            JS_FreeValue(ctx, options);
                            JS_FreeValue(ctx, relativeTo_val);
                            return JS_EXCEPTION;
                        }
                    }
                    relativeTo_val = rt;
                } else {
                    JS_FreeValue(ctx, rt);
                    JS_FreeValue(ctx, options);
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowTypeError(ctx, "invalid relativeTo");
                }
            } else {
                JS_FreeValue(ctx, rt);
            }
        }

        if (ti_get_rounding_increment_option(ctx, options, &roundingIncrement) < 0) {
            JS_FreeValue(ctx, options);
            JS_FreeValue(ctx, relativeTo_val);
            return JS_EXCEPTION;
        }

        /* Read roundingMode */
        if (ti_get_rounding_mode_option(ctx, options, &mode) < 0) {
            JS_FreeValue(ctx, options);
            JS_FreeValue(ctx, relativeTo_val);
            return JS_EXCEPTION;
        }

        if (ti_parse_unit_option(ctx, options, "smallestUnit", &unit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, options);
            JS_FreeValue(ctx, relativeTo_val);
            return JS_EXCEPTION;
        }
        if (unit != TI_UNIT_INVALID)
            has_smallest_unit = 1;

        if (!has_smallest_unit && !has_largest_unit) {
            JS_FreeValue(ctx, options);
            JS_FreeValue(ctx, relativeTo_val);
            return JS_ThrowRangeError(ctx, "smallestUnit or largestUnit is required");
        }
        if (!has_smallest_unit) {
            unit = TI_UNIT_NANOSECOND;
        }
    }

    JS_FreeValue(ctx, options);

    /* Compute existing largest unit and default largestUnit if not provided */
    {
        TIUnit existingLargestUnit = duration_get_largest_unit(s);
        if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO) {
            largestUnit = (existingLargestUnit < unit) ? existingLargestUnit : unit;
        }
        if (!has_relativeTo) {
            if (existingLargestUnit <= TI_UNIT_WEEK || largestUnit <= TI_UNIT_WEEK) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "a starting point is required for durations with calendar units");
            }
        }
    }
    /* Validate largestUnit >= smallestUnit (smallestUnit must not be larger than largestUnit) */
    if (unit < largestUnit) {
        JS_FreeValue(ctx, relativeTo_val);
        return JS_ThrowRangeError(ctx, "smallestUnit must not be larger than largestUnit");
    }
    /* Validate roundingIncrement */
    {
        int64_t maximum = 0;
        if (unit == TI_UNIT_HOUR) {
            maximum = 24;
        } else if (unit == TI_UNIT_MINUTE || unit == TI_UNIT_SECOND) {
            maximum = 60;
        } else if (unit == TI_UNIT_MILLISECOND || unit == TI_UNIT_MICROSECOND || unit == TI_UNIT_NANOSECOND) {
            maximum = 1000;
        }
        if (maximum > 0 && (roundingIncrement >= maximum || maximum % roundingIncrement != 0)) {
            JS_FreeValue(ctx, relativeTo_val);
            return JS_ThrowRangeError(ctx, "invalid roundingIncrement");
        }
        /* Calendar units with roundingIncrement > 1 require largestUnit == smallestUnit */
        if (roundingIncrement > 1 && unit <= TI_UNIT_DAY && unit != largestUnit) {
            JS_FreeValue(ctx, relativeTo_val);
            return JS_ThrowRangeError(ctx, "invalid roundingIncrement");
        }
    }

    {
        int use_iso_rounding = 0;
        int has_timeZone = 0;
        int has_calendar_units = duration_has_calendar_units(s) || s->days != 0;
        int is_calendar_unit = (largestUnit >= TI_UNIT_YEAR && largestUnit <= TI_UNIT_DAY);
        int rel_year = 0, rel_month = 0, rel_day = 0;
        int rel_hour = 0, rel_minute = 0, rel_second = 0;
        int rel_ms = 0, rel_us = 0, rel_ns = 0;

        if (JS_IsObject(relativeTo_val)) {
            JSPlainDateData *pd = JS_GetOpaque(relativeTo_val, js_plaindate_class_id);
            if (pd && (pd->calendar == CAL_ISO8601 || pd->calendar == CAL_GREGORY)) {
                rel_year = pd->year;
                rel_month = pd->month;
                rel_day = pd->day;
                use_iso_rounding = 1;
            } else {
                JSPlainDateTimeData *pdt = JS_GetOpaque(relativeTo_val, js_plaindatetime_class_id);
                if (pdt && (pdt->calendar == CAL_ISO8601 || pdt->calendar == CAL_GREGORY)) {
                    rel_year = pdt->year;
                    rel_month = pdt->month;
                    rel_day = pdt->day;
                    rel_hour = pdt->hour;
                    rel_minute = pdt->minute;
                    rel_second = pdt->second;
                    rel_ms = pdt->millisecond;
                    rel_us = pdt->microsecond;
                    rel_ns = pdt->nanosecond;
                    use_iso_rounding = 1;
                } else {
                    JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
                    if (zdt && (zdt->calendar == CAL_ISO8601 || zdt->calendar == CAL_GREGORY)) {
                        int64_t offset_sec = 0;
                        int tz_id = ti_tz_find_id(zdt->timeZone);
                        if (tz_id >= 0) {
                            offset_sec = ti_tz_offset_at_sec(tz_id, zdt->ts.secs);
                        } else {
                            int64_t offset_ns = 0;
                            int ret = ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                            if (ret >= 0)
                                offset_sec = offset_ns / 1000000000LL;
                        }
                        int64_t local_sec = zdt->ts.secs + offset_sec;
                        int local_nsec = (int)zdt->ts.nsecs;
                        if (local_nsec >= 1000000000) {
                            local_sec += 1;
                            local_nsec -= 1000000000;
                        } else if (local_nsec < 0) {
                            local_sec -= 1;
                            local_nsec += 1000000000;
                        }
                        {
                            int64_t year64;
                            ti_epoch_sec_to_date(local_sec, &year64, &rel_month, &rel_day, &rel_hour, &rel_minute, &rel_second);
                            rel_year = (int)year64;
                        }
                        rel_ms = local_nsec / 1000000;
                        rel_us = (local_nsec / 1000) % 1000;
                        rel_ns = local_nsec % 1000;
                        use_iso_rounding = 1;
                        has_timeZone = 1;
                    }
                }
            }
        }

        if (use_iso_rounding) {
            /* Zero duration with ISO relativeTo: return zero immediately.
               This matches the spec's early return in
               DifferencePlainDateTimeWithRounding/DifferencePlainDateTimeWithTotal.
               For ZonedDateTime, NudgeToZonedTime always runs and may throw
               when the next day boundary is out of range. */
            if (s->years == 0 && s->months == 0 && s->weeks == 0 && s->days == 0 &&
                s->hours == 0 && s->minutes == 0 && s->seconds == 0 &&
                s->milliseconds == 0 && s->microseconds.int128_value == 0 && s->nanoseconds.int128_value == 0) {
                /* Only check day boundary for ZonedDateTime when RoundRelativeDuration
                   would be called (largestUnit is date and smallestUnit is not the
                   default nanosecond/1 early-return case). */
                if (has_timeZone && is_calendar_unit && (unit != TI_UNIT_NANOSECOND || roundingIncrement != 1)) {
                    JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
                    if (zdt && !ti_zoneddatetime_day_boundary_in_range(zdt, 1)) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "relativeTo outside valid instant range");
                    }
                }
                JS_FreeValue(ctx, relativeTo_val);
                return js_create_duration(ctx, s);
            }

            /* ZonedDateTime-specific path for ISO-like calendars */
            if (has_timeZone) {
                JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
                if (zdt) {
                    JSTimeSpec origin = zdt->ts;
                    JSTimeSpec target;
                    JSDurationData add_d = *s;
                    if (ti_zoneddatetime_add_duration_iso(zdt->timeZone, zdt->calendar, origin, &add_d, 1, &target) < 0) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "round result outside valid range");
                    }

                    TIUnit effective_largest = largestUnit;
                    if (effective_largest == TI_UNIT_INVALID) {
                        if (s->years != 0) effective_largest = TI_UNIT_YEAR;
                        else if (s->months != 0) effective_largest = TI_UNIT_MONTH;
                        else if (s->weeks != 0) effective_largest = TI_UNIT_WEEK;
                        else if (s->days != 0) effective_largest = TI_UNIT_DAY;
                        else if (s->hours != 0) effective_largest = TI_UNIT_HOUR;
                        else if (s->minutes != 0) effective_largest = TI_UNIT_MINUTE;
                        else if (s->seconds != 0) effective_largest = TI_UNIT_SECOND;
                        else if (s->milliseconds != 0) effective_largest = TI_UNIT_MILLISECOND;
                        else if (s->microseconds.int128_value != 0) effective_largest = TI_UNIT_MICROSECOND;
                        else effective_largest = TI_UNIT_NANOSECOND;
                    }

                    /* No rounding requested: return exact difference */
                    if (unit == TI_UNIT_NANOSECOND && roundingIncrement == 1) {
                        if (effective_largest >= TI_UNIT_YEAR && effective_largest <= TI_UNIT_DAY) {
                            JSDurationData diff;
                            if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, effective_largest, &diff) < 0) {
                                JS_FreeValue(ctx, relativeTo_val);
                                return JS_ThrowRangeError(ctx, "round result outside valid range");
                            }
                            JS_FreeValue(ctx, relativeTo_val);
                            return js_create_duration(ctx, &diff);
                        } else {
                            js_int128_t pdt_total_ns = ti_timespec_to_ns(&target)
                                              - (ti_timespec_to_ns(&origin));
                            JSDurationData pdt_diff;
                            duration_format_time_ns(pdt_total_ns, effective_largest, &pdt_diff);
                            JS_FreeValue(ctx, relativeTo_val);
                            return js_create_duration(ctx, &pdt_diff);
                        }
                    }

                    if (effective_largest >= TI_UNIT_YEAR && effective_largest <= TI_UNIT_DAY) {
                        /* Date largestUnit */
                        JSDurationData diff;
                        if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, effective_largest, &diff) < 0) {
                            JS_FreeValue(ctx, relativeTo_val);
                            return JS_ThrowRangeError(ctx, "round result outside valid range");
                        }
                        if (unit >= TI_UNIT_YEAR && unit <= TI_UNIT_DAY) {
                            /* Calendar unit rounding using NudgeToCalendarUnit */
                            double total;
                            if (duration_total_nudge(zdt->timeZone, zdt->calendar, origin, target, &diff, unit, &total) < 0) {
                                JS_FreeValue(ctx, relativeTo_val);
                                return JS_ThrowRangeError(ctx, "round result outside valid range");
                            }
                            double q = total / (double)roundingIncrement;
                            double rounded_q;
                            switch (mode) {
                            case ROUND_CEIL: rounded_q = ceil(q); break;
                            case ROUND_FLOOR: rounded_q = floor(q); break;
                            case ROUND_EXPAND: rounded_q = (q >= 0) ? ceil(q) : floor(q); break;
                            case ROUND_TRUNC: rounded_q = (q >= 0) ? floor(q) : ceil(q); break;
                            case ROUND_HALF_CEIL: rounded_q = floor(q + 0.5); break;
                            case ROUND_HALF_FLOOR: rounded_q = ceil(q - 0.5); break;
                            case ROUND_HALF_TRUNC: rounded_q = (q >= 0) ? ceil(q - 0.5) : floor(q + 0.5); break;
                            case ROUND_HALF_EXPAND: default: rounded_q = (q >= 0) ? floor(q + 0.5) : ceil(q - 0.5); break;
                            }
                            int rounded_units = (int)(rounded_q * (double)roundingIncrement);
                            JSDurationData new_d = diff;
                            switch (unit) {
                            case TI_UNIT_YEAR:
                                new_d.years = rounded_units;
                                new_d.months = 0;
                                new_d.weeks = 0;
                                new_d.days = 0;
                                break;
                            case TI_UNIT_MONTH:
                                new_d.months = rounded_units;
                                new_d.weeks = 0;
                                new_d.days = 0;
                                break;
                            case TI_UNIT_WEEK:
                                new_d.weeks = rounded_units;
                                new_d.days = 0;
                                break;
                            case TI_UNIT_DAY:
                                new_d.days = rounded_units;
                                break;
                            default:
                                break;
                            }
                            new_d.hours = 0;
                            new_d.minutes = 0;
                            new_d.seconds = 0;
                            new_d.milliseconds = 0;
                            new_d.microseconds.int128_value = 0;
                            new_d.nanoseconds.int128_value = 0;
                            JS_FreeValue(ctx, relativeTo_val);
                            return js_create_duration(ctx, &new_d);
                        } else {
                            /* Time unit rounding using NudgeToZonedTime */
                            JSDurationData zdt_time_diff;
                            if (ti_zoneddatetime_difference_iso(zdt->timeZone, zdt->calendar, origin, target, effective_largest, &zdt_time_diff) < 0) {
                                JS_FreeValue(ctx, relativeTo_val);
                                return JS_ThrowRangeError(ctx, "round result outside valid range");
                            }
                            {
                                int64_t total_seconds = zdt_time_diff.hours * 3600LL + zdt_time_diff.minutes * 60LL + zdt_time_diff.seconds;
                                time_ns = total_seconds * 1000000000LL
                                        + diff.milliseconds * 1000000LL
                                        + (int64_t)diff.microseconds.int128_value * 1000LL
                                        + (int64_t)diff.nanoseconds.int128_value;
                            }
                            if (time_ns == 0) {
                                JS_FreeValue(ctx, relativeTo_val);
                                return js_create_duration(ctx, &diff);
                            }
                            int sign = duration_sign(&diff);
                            if (sign == 0) sign = 1;
                            /* Get origin local time */
                            int tz_id = ti_tz_find_id(zdt->timeZone);
                            int64_t origin_local_sec;
                            if (tz_id >= 0) {
                                int offset_sec = ti_tz_offset_at_sec(tz_id, origin.secs);
                                origin_local_sec = origin.secs + offset_sec;
                            } else {
                                int64_t offset_ns = 0;
                                ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                                origin_local_sec = origin.secs + offset_ns / 1000000000LL;
                            }
                            int64_t origin_y64;
                            int origin_m, origin_d, origin_h, origin_min, origin_sec;
                            ti_epoch_sec_to_date(origin_local_sec, &origin_y64, &origin_m, &origin_d, &origin_h, &origin_min, &origin_sec);
                            /* startDate = originLocalDate + diff.date */
                            int start_y, start_m, start_d;
                            ti_iso_calendar_date_add((int)origin_y64, origin_m, origin_d,
                                                     diff.years, diff.months, diff.weeks, diff.days,
                                                     &start_y, &start_m, &start_d);
                            int64_t start_days = pdt_to_ordinal_days(start_y, start_m, start_d);
                            int64_t end_days = start_days + sign;
                            int64_t start_local_sec = (start_days - 719162) * 86400LL
                                                    + origin_h * 3600LL + origin_min * 60LL + origin_sec;
                            int64_t end_local_sec = (end_days - 719162) * 86400LL
                                                  + origin_h * 3600LL + origin_min * 60LL + origin_sec;
                            int64_t start_epoch_sec, end_epoch_sec;
                            if (tz_id >= 0) {
                                int offset_sec;
                                ti_tz_local_to_utc(tz_id, start_local_sec, 2, &start_epoch_sec, &offset_sec);
                                ti_tz_local_to_utc(tz_id, end_local_sec, 2, &end_epoch_sec, &offset_sec);
                            } else {
                                int64_t offset_ns = 0;
                                ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                                int64_t offset_sec = offset_ns / 1000000000LL;
                                start_epoch_sec = start_local_sec - offset_sec;
                                end_epoch_sec = end_local_sec - offset_sec;
                            }
                            int64_t daySpan = (end_epoch_sec - start_epoch_sec) * 1000000000LL;
                            js_int128_t round_unit_ns = ti_unit_to_nanoseconds(unit);
                            js_int128_t round_inc_ns = round_unit_ns * roundingIncrement;
                            js_int128_t rounded_time_ns = ti_round_to_increment128(time_ns, round_inc_ns, mode);
                            js_int128_t beyond_day_span = rounded_time_ns - daySpan;
                            int beyond_sign = (beyond_day_span < 0) ? -1 : (beyond_day_span > 0) ? 1 : 0;
                            int didRoundBeyondDay = (beyond_sign != -sign);
                            JSDurationData round_result = diff;
                            if (didRoundBeyondDay) {
                                js_int128_t remainder_ns = ti_round_to_increment128(beyond_day_span, round_inc_ns, mode);
                                round_result.days += sign;
                                int rsign = (remainder_ns < 0) ? -1 : 1;
                                int64_t abs_ns = (int64_t)(remainder_ns >= 0 ? remainder_ns : -remainder_ns);
                                int64_t total_seconds = abs_ns / 1000000000LL;
                                int64_t ns = abs_ns % 1000000000LL;
                                int64_t total_minutes = total_seconds / 60;
                                int64_t seconds = total_seconds % 60;
                                int64_t total_hours = total_minutes / 60;
                                int64_t minutes = total_minutes % 60;
                                int64_t hours = total_hours;
                                round_result.hours = rsign * hours;
                                round_result.minutes = rsign * minutes;
                                round_result.seconds = rsign * seconds;
                                round_result.milliseconds = rsign * (ns / 1000000LL);
                                ns %= 1000000LL;
                                round_result.microseconds.int128_value = rsign * (ns / 1000LL);
                                round_result.nanoseconds.int128_value = rsign * (ns % 1000LL);
                            } else {
                                int rsign = (rounded_time_ns < 0) ? -1 : 1;
                                int64_t abs_ns = (int64_t)(rounded_time_ns >= 0 ? rounded_time_ns : -rounded_time_ns);
                                int64_t total_seconds = abs_ns / 1000000000LL;
                                int64_t ns = abs_ns % 1000000000LL;
                                int64_t total_minutes = total_seconds / 60;
                                int64_t seconds = total_seconds % 60;
                                int64_t total_hours = total_minutes / 60;
                                int64_t minutes = total_minutes % 60;
                                int64_t hours = total_hours;
                                round_result.hours = rsign * hours;
                                round_result.minutes = rsign * minutes;
                                round_result.seconds = rsign * seconds;
                                round_result.milliseconds = rsign * (ns / 1000000LL);
                                ns %= 1000000LL;
                                round_result.microseconds.int128_value = rsign * (ns / 1000LL);
                                round_result.nanoseconds.int128_value = rsign * (ns % 1000LL);
                            }
                            JS_FreeValue(ctx, relativeTo_val);
                            return js_create_duration(ctx, &round_result);
                        }
                    } else {
                        /* Time largestUnit: use DifferenceInstant-style rounding */
                        js_int128_t inst_total_ns = ti_timespec_to_ns(&target)
                                          - (ti_timespec_to_ns(&origin));
                        js_int128_t inst_unit_ns = ti_unit_to_nanoseconds(unit);
                        js_int128_t inst_inc_ns = inst_unit_ns * roundingIncrement;
                        js_int128_t inst_rounded_ns = ti_round_to_increment128(inst_total_ns, inst_inc_ns, mode);
                        JSDurationData inst_diff;
                        duration_format_time_ns(inst_rounded_ns, effective_largest, &inst_diff);
                        JS_FreeValue(ctx, relativeTo_val);
                        return js_create_duration(ctx, &inst_diff);
                    }
                }
            }

            if (has_calendar_units || is_calendar_unit) {
                /* Calendar unit rounding with ISO relativeTo.
                   Compute diff = (relativeTo + duration) - relativeTo,
                   then round diff using RoundRelativeDuration. */
                int ty, tm, td, th, tmin, ts, tms, tus, tns;
                js_int128_t us_val = s->microseconds.int128_value;
                js_int128_t ns_val = s->nanoseconds.int128_value;
                if (ti_iso_datetime_add_duration(
                    rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                    s->years, s->months, s->weeks, s->days,
                    s->hours, s->minutes, s->seconds, s->milliseconds, us_val, ns_val,
                    &ty, &tm, &td, &th, &tmin, &ts, &tms, &tus, &tns) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                }
                if (!ti_is_date_within_iso_limits(ty, tm, td)) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                }
                if (!has_timeZone) {
                    if (!pdt_is_within_limits(rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns) ||
                        !pdt_is_within_limits(ty, tm, td, th, tmin, ts, tms, tus, tns)) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                    }
                }

                JSDurationData diff;
                memset(&diff, 0, sizeof(diff));
                if (ti_iso_datetime_difference(
                        rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                        ty, tm, td, th, tmin, ts, tms, tus, tns,
                        largestUnit,
                        &diff) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_EXCEPTION;
                }

                JSDurationData out;
                memset(&out, 0, sizeof(out));
                if (ti_round_relative_duration_iso(ctx,
                                                    rel_year, rel_month, rel_day,
                                                    rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                                                    &diff, largestUnit, unit,
                                                    roundingIncrement, mode,
                                                    has_timeZone, &out) < 0) {
                    JS_FreeValue(ctx, relativeTo_val);
                    return JS_EXCEPTION;
                }
                memset(&result, 0, sizeof(result));
                result.years = out.years;
                result.months = out.months;
                result.weeks = out.weeks;
                result.days = out.days;
                result.hours = out.hours;
                result.minutes = out.minutes;
                result.seconds = out.seconds;
                result.milliseconds = out.milliseconds;
                result.microseconds.int128_value = out.microseconds.int128_value;
                result.nanoseconds.int128_value = out.nanoseconds.int128_value;
            } else {
                if (!has_timeZone) {
                    int ty, tm, td, th, tmin, ts, tms, tus, tns;
                    if (ti_iso_datetime_add_duration(
                        rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                        0, 0, 0, 0,
                        s->hours, s->minutes, s->seconds, s->milliseconds,
                        s->microseconds.int128_value, s->nanoseconds.int128_value,
                        &ty, &tm, &td, &th, &tmin, &ts, &tms, &tus, &tns) < 0) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                    }
                    if (!pdt_is_within_limits(rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns) ||
                        !pdt_is_within_limits(ty, tm, td, th, tmin, ts, tms, tus, tns)) {
                        JS_FreeValue(ctx, relativeTo_val);
                        return JS_ThrowRangeError(ctx, "relativeTo outside valid ISO range");
                    }
                } else {
                    JSZonedDateTimeData *zdt = JS_GetOpaque(relativeTo_val, js_zoneddatetime_class_id);
                    if (zdt) {
                        js_int128_t zdt_epoch_ns = ti_timespec_to_ns(&zdt->ts);
                        {
                            int64_t total_seconds = s->days * 86400LL +
                                                    s->hours * 3600LL +
                                                    s->minutes * 60LL +
                                                    s->seconds;
                            js_int128_t duration_ns = (js_int128_t)total_seconds * 1000000000LL +
                                                   (js_int128_t)s->milliseconds * 1000000LL +
                                                   s->microseconds.int128_value * 1000LL +
                                                   s->nanoseconds.int128_value;
                            js_int128_t target_epoch_ns = zdt_epoch_ns + duration_ns;
                            js_int128_t limit = (js_int128_t)INSTANT_MAX_SECS * 1000000000LL;
                            if (target_epoch_ns < -limit || target_epoch_ns > limit) {
                            JS_FreeValue(ctx, relativeTo_val);
                                JS_FreeValue(ctx, relativeTo_val);
                                return JS_ThrowRangeError(ctx, "relativeTo outside valid instant range");
                            }
                        }
                    }
                }
                /* Time unit rounding with ISO relativeTo.
                   Place the rounded value in the smallestUnit field only,
                   preserving the original calendar fields (years/months/weeks)
                   and extracting days only when largestUnit is a date unit. */
                {
                    int64_t total_seconds = s->days * 86400LL +
                                            s->hours * 3600LL +
                                            s->minutes * 60LL +
                                            s->seconds;
                    day_time_ns = (js_int128_t)total_seconds * 1000000000LL;
                    day_time_ns += (js_int128_t)s->milliseconds * 1000000LL;
                    day_time_ns += s->microseconds.int128_value * 1000LL;
                    day_time_ns += s->nanoseconds.int128_value;
                }

                js_int128_t plain_unit_ns;
                switch (unit) {
                case TI_UNIT_DAY: plain_unit_ns = 86400000000000LL; break;
                case TI_UNIT_HOUR: plain_unit_ns = 3600000000000LL; break;
                case TI_UNIT_MINUTE: plain_unit_ns = 60000000000LL; break;
                case TI_UNIT_SECOND: plain_unit_ns = 1000000000LL; break;
                case TI_UNIT_MILLISECOND: plain_unit_ns = 1000000LL; break;
                case TI_UNIT_MICROSECOND: plain_unit_ns = 1000LL; break;
                case TI_UNIT_NANOSECOND: plain_unit_ns = 1LL; break;
                default: plain_unit_ns = 1LL; break;
                }

                js_int128_t plain_rounded_ns = ti_round_to_increment128(day_time_ns, plain_unit_ns * roundingIncrement, mode);

                int64_t extra_days = 0;
                js_int128_t remainder_ns = plain_rounded_ns;
                if (largestUnit <= TI_UNIT_DAY) {
                    extra_days = (int64_t)(plain_rounded_ns / 86400000000000LL);
                    remainder_ns = plain_rounded_ns % 86400000000000LL;
                }

                memset(&result, 0, sizeof(result));
                result.years = s->years;
                result.months = s->months;
                result.weeks = s->weeks;
                if (largestUnit <= TI_UNIT_DAY) {
                    result.days = extra_days;
                } else {
                    result.days = s->days;
                }

                {
                    int sign = (remainder_ns < 0) ? -1 : 1;
                    js_int128_t abs_ns = remainder_ns >= 0 ? remainder_ns : -remainder_ns;
                    int64_t sec = (int64_t)(abs_ns / 1000000000LL);
                    int64_t ns = (int64_t)(abs_ns % 1000000000LL);
                    int64_t us = ns / 1000; ns %= 1000;
                    int64_t ms = us / 1000; us %= 1000;
                    sec += ms / 1000; ms %= 1000;
                    int64_t min = 0, hr = 0;

                    switch (largestUnit) {
                    case TI_UNIT_NANOSECOND:
                        ns = sec * 1000000000LL + ns;
                        sec = 0;
                        ms = 0;
                        us = 0;
                        break;
                    case TI_UNIT_MICROSECOND:
                        us = sec * 1000000LL + us;
                        sec = 0;
                        ms = 0;
                        ns = 0;
                        break;
                    case TI_UNIT_MILLISECOND:
                        ms = sec * 1000LL + ms;
                        sec = 0;
                        us = 0;
                        ns = 0;
                        break;
                    case TI_UNIT_SECOND:
                        break;
                    case TI_UNIT_MINUTE:
                        min = sec / 60; sec %= 60;
                        break;
                    case TI_UNIT_HOUR:
                        min = sec / 60; sec %= 60;
                        hr = min / 60; min %= 60;
                        break;
                    default:
                        /* day/week/month/year: normalize all the way to hours */
                        min = sec / 60; sec %= 60;
                        hr = min / 60; min %= 60;
                        break;
                    }

                    result.hours = hr * sign;
                    result.minutes = min * sign;
                    result.seconds = sec * sign;
                    result.milliseconds = ms * sign;
                    result.microseconds.int128_value = (js_int128_t)(us * sign);
                    result.nanoseconds.int128_value = (js_int128_t)(ns * sign);
                }
            }
        } else if (unit >= TI_UNIT_YEAR && unit <= TI_UNIT_WEEK) {
            (void)rel_year; (void)rel_month; (void)rel_day;
            (void)rel_hour; (void)rel_minute; (void)rel_second;
            (void)rel_ms; (void)rel_us; (void)rel_ns;
            /* For date units without ISO relativeTo, approximate using total days */
            int64_t total_days = s->years * 365LL + s->months * 30LL + s->weeks * 7LL + s->days;
            total_days += (int64_t)(duration_to_total_ns(s) / 86400000000000LL);
            if (unit == TI_UNIT_DAY) {
                unit_ns = 86400000000000LL;
                inc_ns = unit_ns * roundingIncrement;
                rounded_ns = ti_round_to_increment128((js_int128_t)total_days * 86400000000000LL, inc_ns, mode);
                total_days = (int64_t)(rounded_ns / 86400000000000LL);
                result.days = total_days;
            } else {
                /* week/month/year: just return days for now */
                result.days = total_days;
            }
        } else {
            total_ns = duration_to_total_ns(s);
            unit_ns = ti_unit_to_nanoseconds(unit);
            if (unit_ns <= 0) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "invalid unit");
            }
            inc_ns = unit_ns * roundingIncrement;
            rounded_ns = ti_round_to_increment128(total_ns, inc_ns, mode);
            if (duration_from_total_ns(rounded_ns, largestUnit, &result) < 0) {
                JS_FreeValue(ctx, relativeTo_val);
                return JS_ThrowRangeError(ctx, "result out of range");
            }
        }
    }

    JS_FreeValue(ctx, relativeTo_val);
    duration_round_fields_to_float64(&result);
    if (duration_is_out_of_range(&result))
        return JS_ThrowRangeError(ctx, "duration out of range");
    return js_create_duration(ctx, &result);
}

static JSValue js_duration_valueOf(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.Duration with < or >");
}

static const JSCFunctionListEntry js_duration_proto_funcs[] = {
    JS_CGETSET_DEF("years", js_duration_get_years, NULL),
    JS_CGETSET_DEF("months", js_duration_get_months, NULL),
    JS_CGETSET_DEF("weeks", js_duration_get_weeks, NULL),
    JS_CGETSET_DEF("days", js_duration_get_days, NULL),
    JS_CGETSET_DEF("hours", js_duration_get_hours, NULL),
    JS_CGETSET_DEF("minutes", js_duration_get_minutes, NULL),
    JS_CGETSET_DEF("seconds", js_duration_get_seconds, NULL),
    JS_CGETSET_DEF("milliseconds", js_duration_get_milliseconds, NULL),
    JS_CGETSET_DEF("microseconds", js_duration_get_microseconds, NULL),
    JS_CGETSET_DEF("nanoseconds", js_duration_get_nanoseconds, NULL),
    JS_CGETSET_DEF("blank", js_duration_get_blank, NULL),
    JS_CFUNC_DEF("negated", 0, js_duration_negated),
    JS_CFUNC_DEF("abs", 0, js_duration_abs),
    JS_CGETSET_DEF("sign", js_duration_get_sign, NULL),
    JS_CFUNC_DEF("with", 1, js_duration_with),
    JS_CFUNC_DEF("add", 1, js_duration_add),
    JS_CFUNC_DEF("subtract", 1, js_duration_subtract),
    JS_CFUNC_DEF("total", 1, js_duration_total),
    JS_CFUNC_DEF("round", 1, js_duration_round),
    JS_CFUNC_DEF("toString", 0, js_duration_toString),
    JS_CFUNC_DEF("toJSON", 0, js_duration_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_duration_toLocaleString),
    JS_CFUNC_DEF("valueOf", 0, js_duration_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Duration", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_duration_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_duration_from),
    JS_CFUNC_DEF("compare", 2, js_duration_compare),
};

int js_add_duration(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_duration_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_duration_class_id, &js_duration_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_duration_class_id, "Duration",
                               (JSCFunction *)js_duration_constructor, 0,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_duration_static_funcs, countof(js_duration_static_funcs),
                               js_duration_proto_funcs, countof(js_duration_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "Duration", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
