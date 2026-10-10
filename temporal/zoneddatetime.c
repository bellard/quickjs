/*
 * QuickJS Temporal.ZonedDateTime implementation (minimal)
 */

#include <string.h>
#include <math.h>
#include <ctype.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"
#include "../intl/intl_private.h"
#include "timezone.h"

JSClassID js_zoneddatetime_class_id;

/* Public helper to extract timespec from a ZonedDateTime without toString conversion */
int js_zoneddatetime_get_timespec(JSContext *ctx, JSValueConst val, JSTimeSpec *ts)
{
    JSZonedDateTimeData *s = JS_GetOpaque(val, js_zoneddatetime_class_id);
    if (!s)
        return -1;
    *ts = s->ts;
    return 0;
}

static void js_zoneddatetime_finalizer(JSRuntime *rt, JSValue val)
{
    JSZonedDateTimeData *s = JS_GetOpaque(val, js_zoneddatetime_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_zoneddatetime_class = {
    "Temporal.ZonedDateTime",
    .finalizer = js_zoneddatetime_finalizer,
};

JSValue js_create_zoneddatetime(JSContext *ctx, const JSTimeSpec *ts,
                                        const char *timeZone, CalendarType calendar)
{
    JSZonedDateTimeData *s;
    JSValue obj;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    s->ts = *ts;
    pstrcpy(s->timeZone, sizeof(s->timeZone), timeZone);
    s->calendar = calendar;
    obj = JS_NewObjectClass(ctx, (int)js_zoneddatetime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

/* Apply timezone offset to UTC epoch seconds to get local wall-clock time */
/* Create a ZonedDateTime from prepared fields */
JSValue ti_create_zoneddatetime_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                             int overflow_constrain, JSValueConst options)
{
    int year, month, day, hour, minute, second, ms, us, ns;
    int64_t offset_ns = 0;
    const char *timeZone = "UTC";
    JSTimeSpec ts;
    int has_offset = 0;

    if (fields->calendar == CAL_ISO8601 || fields->calendar == CAL_GREGORY) {
        if (!(fields->present_mask & TI_FIELD_YEAR))
            return JS_ThrowTypeError(ctx, "missing required property");
        if (!(fields->present_mask & TI_FIELD_DAY))
            return JS_ThrowTypeError(ctx, "missing required property");
        if (!(fields->present_mask & (TI_FIELD_MONTH | TI_FIELD_MONTH_CODE)))
            return JS_ThrowTypeError(ctx, "missing required property");
    }

    year = (int)fields->year;
    month = (fields->present_mask & TI_FIELD_MONTH) ? fields->month : 0;
    day = fields->day;

    if (fields->present_mask & TI_FIELD_MONTH_CODE) {
        if (fields->calendar == CAL_ISO8601) {
            if (fields->month_from_code < 1 || fields->month_from_code > 12)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for ISO 8601 calendar");
            if (fields->month_code_has_l)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for ISO 8601 calendar");
            month = fields->month_from_code;
        } else {
            if (!ti_calendar_validate_month_code(fields->calendar, fields->month_from_code, fields->month_code_has_l))
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
            if (!(fields->present_mask & TI_FIELD_MONTH))
                month = fields->month_from_code;
        }
    }

    if ((fields->calendar == CAL_ISO8601 || fields->calendar == CAL_GREGORY) &&
        (fields->present_mask & TI_FIELD_MONTH_CODE) && (fields->present_mask & TI_FIELD_MONTH) &&
        fields->month != fields->month_from_code)
        return JS_ThrowRangeError(ctx, "month and monthCode must agree");

    if (month <= 0)
        return JS_ThrowRangeError(ctx, "month must be positive");
    if (day <= 0)
        return JS_ThrowRangeError(ctx, "day must be positive");

    hour = (fields->present_mask & TI_FIELD_HOUR) ? fields->hour : 0;
    minute = (fields->present_mask & TI_FIELD_MINUTE) ? fields->minute : 0;
    second = (fields->present_mask & TI_FIELD_SECOND) ? fields->second : 0;
    ms = (fields->present_mask & TI_FIELD_MILLISECOND) ? fields->millisecond : 0;
    us = (fields->present_mask & TI_FIELD_MICROSECOND) ? fields->microsecond : 0;
    ns = (fields->present_mask & TI_FIELD_NANOSECOND) ? fields->nanosecond : 0;

    if (!overflow_constrain) {
        if (hour < 0 || hour > 23)
            return JS_ThrowRangeError(ctx, "invalid hour");
        if (minute < 0 || minute > 59)
            return JS_ThrowRangeError(ctx, "invalid minute");
        if (second < 0 || second > 59)
            return JS_ThrowRangeError(ctx, "invalid second");
        if (ms < 0 || ms > 999)
            return JS_ThrowRangeError(ctx, "invalid millisecond");
        if (us < 0 || us > 999)
            return JS_ThrowRangeError(ctx, "invalid microsecond");
        if (ns < 0 || ns > 999)
            return JS_ThrowRangeError(ctx, "invalid nanosecond");
    } else {
        if (hour > 23) hour = 23; else if (hour < 0) hour = 0;
        if (minute > 59) minute = 59; else if (minute < 0) minute = 0;
        if (second > 59) second = 59; else if (second < 0) second = 0;
        if (ms > 999) ms = 999; else if (ms < 0) ms = 0;
        if (us > 999) us = 999; else if (us < 0) us = 0;
        if (ns > 999) ns = 999; else if (ns < 0) ns = 0;
    }

    if (!pd_is_valid_date(year, month, day)) {
        if (!overflow_constrain)
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
        if (month > 12) month = 12;
        else if (month < 1) month = 1;
        {
            int dim = pd_days_in_month(year, month);
            if (day > dim) day = dim;
            else if (day < 1) day = 1;
        }
    }

    /* Get timeZone */
    if (fields->present_mask & TI_FIELD_TIMEZONE) {
        if (fields->time_zone[0])
            timeZone = fields->time_zone;
    }

    /* Use offset if present */
    if (fields->present_mask & TI_FIELD_OFFSET) {
        offset_ns = fields->offset_ns;
        has_offset = 1;
    }

    /* Compute local seconds */
    {
        int64_t days = pdt_to_ordinal_days(year, month, day);
        int64_t local_sec = (days - 719162) * 86400LL + hour * 3600LL + minute * 60LL + second;
        int local_nsec = ms * 1000000 + us * 1000 + ns;
        int64_t tz_offset_ns = 0;

        /* Get timezone offset */
        {
            int ret = ti_parse_temporal_timezone_string(timeZone, &tz_offset_ns);
            if (ret < 0)
                return JS_ThrowRangeError(ctx, "invalid timeZone");
        }

        /* For named timezones, look up the exact offset */
        {
            int tz_id = ti_tz_find_id(timeZone);
            if (tz_id >= 0) {
                int iter;
                ts.secs = local_sec;
                for (iter = 0; iter < 4; iter++) {
                    int offset_sec = ti_tz_offset_at_sec(tz_id, ts.secs);
                    int64_t new_sec = local_sec - offset_sec;
                    if (new_sec == ts.secs)
                        break;
                    ts.secs = new_sec;
                }
                tz_offset_ns = ti_tz_offset_at_sec(tz_id, ts.secs) * 1000000000LL;
            }
        }

        /* Apply offset: UTC = local - offset */
        if (has_offset) {
            /* If options say "use", use property offset; otherwise validate */
            int use_prop_offset = 0;
            if (!JS_IsUndefined(options) && !JS_IsNull(options)) {
                JSValue opt_offset = JS_GetPropertyStr(ctx, options, "offset");
                if (JS_IsException(opt_offset))
                    return JS_EXCEPTION;
                if (!JS_IsUndefined(opt_offset)) {
                    const char *ostr = JS_ToCString(ctx, opt_offset);
                    if (ostr) {
                        if (strcmp(ostr, "use") == 0)
                            use_prop_offset = 1;
                        JS_FreeCString(ctx, ostr);
                    }
                }
                JS_FreeValue(ctx, opt_offset);
            }
            if (use_prop_offset) {
                tz_offset_ns = offset_ns;
            } else {
                /* Validate property bag offset against timezone (default = exact reject) */
                if (offset_ns != tz_offset_ns)
                    return JS_ThrowRangeError(ctx, "offset does not match time zone");
            }
        }

        {
            int64_t offset_sec = tz_offset_ns / 1000000000LL;
            int offset_nsec = (int)(tz_offset_ns % 1000000000LL);
            ts.secs = local_sec - offset_sec;
            {
                int new_nsec = local_nsec - offset_nsec;
                if (new_nsec < 0) {
                    new_nsec += 1000000000;
                    ts.secs -= 1;
                } else if (new_nsec >= 1000000000) {
                    new_nsec -= 1000000000;
                    ts.secs += 1;
                }
                ts.nsecs = (uint32_t)new_nsec;
            }
        }
    }

    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "epoch nanoseconds outside valid limits");

    return js_create_zoneddatetime(ctx, &ts, timeZone, fields->calendar);
}
static void zdt_apply_timezone_offset(JSContext *ctx, JSZonedDateTimeData *s,
                                       int64_t *epoch_sec, int *epoch_nsec)
{
    int tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id >= 0) {
        int offset_sec = ti_tz_offset_at_sec(tz_id, *epoch_sec);
        *epoch_sec += offset_sec;
    } else {
        int64_t offset_ns = 0;
        int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
        if (ret == 0) {
            int64_t offset_sec = offset_ns / 1000000000LL;
            int offset_nsec = (int)(offset_ns % 1000000000LL);
            *epoch_sec += offset_sec;
            *epoch_nsec += offset_nsec;
            if (*epoch_nsec >= 1000000000) {
                *epoch_nsec -= 1000000000;
                *epoch_sec += 1;
            } else if (*epoch_nsec < 0) {
                *epoch_nsec += 1000000000;
                *epoch_sec -= 1;
            }
        }
    }
}

/* Convert local wall-clock time to UTC epoch by subtracting timezone offset.
   For named timezones, iterates to find the correct epoch. */
/* Temporal.ZonedDateTime constructor */
static JSValue js_zoneddatetime_constructor(JSContext *ctx, JSValueConst new_target,
                                             int argc, JSValueConst *argv)
{
    JSZonedDateTimeData *s;
    JSValue obj;
    JSTimeSpec ts;
    const char *timeZone = "UTC";
    CalendarType calendar = CAL_ISO8601;
    int tz_allocated = 0;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.ZonedDateTime constructor cannot be called as a function");
    }
    if (ti_bigint_to_timespec(ctx, argv[0], &ts) < 0)
        return JS_EXCEPTION;
    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
    {
        int tz_res = ti_validate_timezone_identifier(ctx, argc > 1 ? argv[1] : JS_UNDEFINED, &timeZone);
        if (tz_res < 0)
            return JS_EXCEPTION;
        if (tz_res == 1)
            tz_allocated = 1;
    }
    if (argc > 2) {
        int cal_res = ti_validate_constructor_calendar(ctx, argv[2], &calendar);
        if (cal_res < 0) {
            if (tz_allocated) JS_FreeCString(ctx, timeZone);
            return JS_EXCEPTION;
        }
    }
    s = js_mallocz(ctx, sizeof(*s));
    if (!s) {
        if (tz_allocated) JS_FreeCString(ctx, timeZone);
        return JS_EXCEPTION;
    }
    s->ts = ts;
    pstrcpy(s->timeZone, sizeof(s->timeZone), timeZone);
    s->calendar = calendar;
    obj = js_create_temporal_object(ctx, new_target, js_zoneddatetime_class_id, s);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        if (tz_allocated) JS_FreeCString(ctx, timeZone);
        return JS_EXCEPTION;
    }
    if (tz_allocated) JS_FreeCString(ctx, timeZone);
    return obj;
}

/* Getters for epoch values */
static JSValue js_zoneddatetime_get_epochNanoseconds(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_timespec_to_bigint(ctx, &s->ts);
}

static JSValue js_zoneddatetime_get_epochMilliseconds(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    js_int128_t total_ns;
    int64_t ms;
    if (!s)
        return JS_EXCEPTION;
    total_ns = ti_timespec_to_ns(&s->ts);
    ms = (int64_t)(total_ns / 1000000);
    if (total_ns < 0 && total_ns % 1000000 != 0)
        ms -= 1;
    return JS_NewInt64(ctx, ms);
}

static JSValue js_zoneddatetime_get_timeZoneId(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, s->timeZone);
}

static JSValue js_zoneddatetime_get_calendarId(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar));
}

/* Getters for date/time components (apply timezone offset for local wall-clock time) */
/* Getters for date/time components (apply timezone offset for local wall-clock time) */
static BOOL zdt_get_local_fields(JSContext *ctx, JSValueConst this_val,
                                 int64_t *year, int *month, int *day,
                                 int *hour, int *minute, int *second,
                                 JSZonedDateTimeData **ps)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t epoch_sec;
    int epoch_nsec;
    if (!s)
        return FALSE;
    epoch_sec = s->ts.secs;
    epoch_nsec = (int)s->ts.nsecs;
    zdt_apply_timezone_offset(ctx, s, &epoch_sec, &epoch_nsec);
    ti_epoch_sec_to_date(epoch_sec, year, month, day, hour, minute, second);
    if (ps)
        *ps = s;
    return TRUE;
}

static JSValue js_zoneddatetime_get_year(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_month, cal_day;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_month = month; cal_day = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_month, &cal_day);
    return JS_NewInt64(ctx, cal_year);
}


static JSValue js_zoneddatetime_get_month(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_month, cal_day;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_month = month; cal_day = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_month, &cal_day);
    return JS_NewInt32(ctx, cal_month);
}


static JSValue js_zoneddatetime_get_day(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_month, cal_day;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_month = month; cal_day = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_month, &cal_day);
    return JS_NewInt32(ctx, cal_day);
}


static JSValue js_zoneddatetime_get_hour(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, NULL))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, hour);
}


static JSValue js_zoneddatetime_get_minute(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, NULL))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, minute);
}


static JSValue js_zoneddatetime_get_second(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, NULL))
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, second);
}


static JSValue js_zoneddatetime_get_millisecond(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, s->ts.nsecs / 1000000);
}


static JSValue js_zoneddatetime_get_microsecond(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, (s->ts.nsecs / 1000) % 1000);
}


static JSValue js_zoneddatetime_get_nanosecond(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, s->ts.nsecs % 1000);
}


static JSValue js_zoneddatetime_get_offset(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t offset_ns;
    int tz_id;
    char buf[16];
    if (!s)
        return JS_EXCEPTION;
    tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id >= 0) {
        int offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
        offset_ns = (int64_t)offset_sec * 1000000000LL;
    } else {
        int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
        if (ret != 0)
            offset_ns = 0;
    }
    {
        int64_t off_sec = offset_ns / 1000000000LL;
        int64_t abs_sec = off_sec >= 0 ? off_sec : -off_sec;
        int hours = (int)(abs_sec / 3600);
        int mins = (int)((abs_sec % 3600) / 60);
        int secs = (int)(abs_sec % 60);
        if (secs)
            snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d",
                     off_sec >= 0 ? '+' : '-', hours, mins, secs);
        else
            snprintf(buf, sizeof(buf), "%c%02d:%02d",
                     off_sec >= 0 ? '+' : '-', hours, mins);
        return JS_NewString(ctx, buf);
    }
}

static JSValue js_zoneddatetime_get_offsetNanoseconds(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t offset_ns;
    int tz_id;
    if (!s)
        return JS_EXCEPTION;
    tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id >= 0) {
        int offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
        offset_ns = (int64_t)offset_sec * 1000000000LL;
    } else {
        int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
        if (ret != 0)
            offset_ns = 0;
    }
    return JS_NewInt64(ctx, offset_ns);
}

static JSValue js_zoneddatetime_get_era(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    TIEra era;
    int dummy_era_year;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, (int)year, month, day, cal_year, &era, &dummy_era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewString(ctx, ti_era_to_string(era));
    return JS_UNDEFINED;
}


static JSValue js_zoneddatetime_get_eraYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    TIEra era;
    int era_year;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, (int)year, month, day, cal_year, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewInt32(ctx, era_year);
    return JS_UNDEFINED;
}


static JSValue js_zoneddatetime_get_monthCode(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return ti_get_month_code(ctx, s->calendar, cal_year, cal_m);
}



static JSValue js_zoneddatetime_get_inLeapYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return JS_NewBool(ctx, ti_calendar_is_leap_year(s->calendar, cal_year));
}


static JSValue js_zoneddatetime_get_daysInYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_year(s->calendar, cal_year));
}


static JSValue js_zoneddatetime_get_daysInWeek(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, 7);
}

static JSValue js_zoneddatetime_get_monthsInYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_months_in_year(s->calendar, cal_year));
}


static JSValue js_zoneddatetime_get_hoursInDay(JSContext *ctx, JSValueConst this_val)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t offset_sec;
    int64_t local_sec, day_number;
    int64_t start_today_utc, start_tomorrow_utc;
    JSTimeSpec today_ts, tomorrow_ts;
    int tz_id;

    if (!s)
        return JS_EXCEPTION;

    tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id >= 0) {
        offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
    } else {
        int64_t offset_ns = 0;
        int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
        if (ret < 0)
            return JS_ThrowRangeError(ctx, "invalid time zone");
        offset_sec = offset_ns / 1000000000LL;
    }

    local_sec = s->ts.secs + offset_sec;
    day_number = floor_div64(local_sec, 86400LL);

    if (tz_id >= 0) {
        start_today_utc = ti_get_start_of_day_sec(tz_id, day_number * 86400LL);
        if (start_today_utc == INT64_MIN)
            return JS_ThrowRangeError(ctx, "multiple instants found");
        start_tomorrow_utc = ti_get_start_of_day_sec(tz_id, (day_number + 1) * 86400LL);
        if (start_tomorrow_utc == INT64_MIN)
            return JS_ThrowRangeError(ctx, "multiple instants found");
    } else {
        start_today_utc = day_number * 86400LL - offset_sec;
        start_tomorrow_utc = (day_number + 1) * 86400LL - offset_sec;
    }

    today_ts.secs = start_today_utc;
    today_ts.nsecs = 0;
    tomorrow_ts.secs = start_tomorrow_utc;
    tomorrow_ts.nsecs = 0;

    if (!is_valid_epoch_timespec(&today_ts) || !is_valid_epoch_timespec(&tomorrow_ts))
        return JS_ThrowRangeError(ctx, "hoursInDay out of range");

    return JS_NewFloat64(ctx, (double)(start_tomorrow_utc - start_today_utc) / 3600.0);
}


/* ISO day of week: 1=Monday, 7=Sunday */
static int zdt_iso_day_of_week(int year, int month, int day)
{
    /* Using Zeller's congruence variant for Gregorian calendar */
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 2;
    int d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
    /* d: 0=Sunday, 1=Monday, ..., 6=Saturday */
    return (d == 0) ? 7 : d;
}

/* ISO week computation: week 1 contains the first Thursday */
static void zdt_iso_week(int year, int month, int day, int *weekYear, int *weekNumber)
{
    int dow = zdt_iso_day_of_week(year, month, day);
    int ordinal = day;
    int m;
    for (m = 1; m < month; m++) {
        ordinal += pd_days_in_month(year, m);
    }
    /* Find Thursday of current week */
    int thursdayOrdinal = ordinal + (4 - dow);
    /* Year of the Thursday */
    int y = year;
    int daysInY = pd_is_leap_year(y) ? 366 : 365;
    if (thursdayOrdinal <= 0) {
        y = year - 1;
        daysInY = pd_is_leap_year(y) ? 366 : 365;
        thursdayOrdinal += daysInY;
    } else if (thursdayOrdinal > daysInY) {
        thursdayOrdinal -= daysInY;
        y = year + 1;
    }
    *weekYear = y;
    *weekNumber = (thursdayOrdinal - 1) / 7 + 1;
}

static JSValue js_zoneddatetime_get_weekOfYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    int weekYear, weekNumber;
    JSZonedDateTimeData *s;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    if (year < 1 || year > 9999)
        return JS_UNDEFINED;
    zdt_iso_week((int)year, month, day, &weekYear, &weekNumber);
    return JS_NewInt32(ctx, weekNumber);
}


static JSValue js_zoneddatetime_get_yearOfWeek(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    int weekYear, weekNumber;
    JSZonedDateTimeData *s;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    if (year < 1 || year > 9999)
        return JS_UNDEFINED;
    zdt_iso_week((int)year, month, day, &weekYear, &weekNumber);
    return JS_NewInt64(ctx, weekYear);
}


static JSValue js_zoneddatetime_get_daysInMonth(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_month(s->calendar, cal_year, cal_m));
}


static JSValue js_zoneddatetime_get_dayOfYear(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    JSZonedDateTimeData *s;
    int cal_year, cal_m, cal_d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, &s))
        return JS_EXCEPTION;
    cal_year = (int)year; cal_m = month; cal_d = day;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_day_of_year(s->calendar, cal_year, cal_m, cal_d));
}


static JSValue js_zoneddatetime_get_dayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    int64_t year;
    int month, day, hour, minute, second;
    int a, y, m, d;
    if (!zdt_get_local_fields(ctx, this_val, &year, &month, &day, &hour, &minute, &second, NULL))
        return JS_EXCEPTION;
    a = (14 - month) / 12;
    y = (int)year - a;
    m = month + 12 * a - 2;
    d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
    return JS_NewInt32(ctx, (d == 0) ? 7 : d);
}


JSValue js_zoneddatetime_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv);

static JSValue js_zoneddatetime_compare(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    JSValue a_val, b_val;
    JSZonedDateTimeData *a, *b;
    js_int128_t ns_a, ns_b;
    a_val = js_zoneddatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(a_val))
        return JS_EXCEPTION;
    b_val = js_zoneddatetime_from(ctx, JS_UNDEFINED, 1, &argv[1]);
    if (JS_IsException(b_val)) {
        JS_FreeValue(ctx, a_val);
        return JS_EXCEPTION;
    }
    a = JS_GetOpaque(a_val, js_zoneddatetime_class_id);
    b = JS_GetOpaque(b_val, js_zoneddatetime_class_id);
    if (!a || !b) {
        JS_FreeValue(ctx, a_val);
        JS_FreeValue(ctx, b_val);
        return JS_ThrowTypeError(ctx, "invalid ZonedDateTime");
    }
    ns_a = ti_timespec_to_ns(&a->ts);
    ns_b = ti_timespec_to_ns(&b->ts);
    JS_FreeValue(ctx, a_val);
    JS_FreeValue(ctx, b_val);
    if (ns_a < ns_b)
        return JS_NewInt32(ctx, -1);
    if (ns_a > ns_b)
        return JS_NewInt32(ctx, 1);
    return JS_NewInt32(ctx, 0);
}

static JSValue js_zoneddatetime_withCalendar(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)argc;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSZonedDateTimeData *t;
    JSValue obj;
    CalendarType cal;
    if (!s)
        return JS_EXCEPTION;
    if (ti_resolve_calendar_value(ctx, argv[0], &cal) < 0)
        return JS_EXCEPTION;
    t = js_mallocz(ctx, sizeof(*t));
    if (!t)
        return JS_EXCEPTION;
    *t = *s;
    t->calendar = cal;
    obj = JS_NewObjectClass(ctx, (int)js_zoneddatetime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, t);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, t);
    return obj;
}

static JSValue js_zoneddatetime_startOfDay(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t local_sec;
    JSTimeSpec ts;
    int64_t offset_sec = 0;
    int tz_id;
    if (!s)
        return JS_EXCEPTION;
    /* Apply timezone offset to get local wall-clock time */
    tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id >= 0) {
        offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
    } else {
        int64_t offset_ns = 0;
        int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
        if (ret == 0)
            offset_sec = offset_ns / 1000000000LL;
    }
    local_sec = s->ts.secs + offset_sec;
    /* Floor local time to start of local day */
    {
        int64_t days = local_sec / 86400LL;
        if (local_sec < 0 && local_sec % 86400LL != 0)
            days--;
        ts.secs = days * 86400LL;
        ts.nsecs = 0;
    }
    /* Convert local day start back to UTC, handling DST transitions at midnight */
    if (tz_id >= 0) {
        int64_t utc_sec = ti_get_start_of_day_sec(tz_id, ts.secs);
        if (utc_sec == INT64_MIN)
            return JS_ThrowRangeError(ctx, "multiple instants found");
        ts.secs = utc_sec;
    } else {
        ts.secs -= offset_sec;
    }
    /* Validate epoch nanoseconds are within Instant limits */
    {
        js_int128_t epoch_ns = ti_timespec_to_ns(&ts);
        js_int128_t limit = (js_int128_t)8640000000000LL * 1000000000LL;
        if (epoch_ns < -limit || epoch_ns > limit)
            return JS_ThrowRangeError(ctx, "epoch nanoseconds outside valid limits");
    }
    return js_create_zoneddatetime(ctx, &ts, s->timeZone, s->calendar);
}

static JSValue js_zoneddatetime_getTimeZoneTransition(JSContext *ctx, JSValueConst this_val,
                                                        int argc, JSValueConst *argv)
{
    (void)argc;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSValue direction_val;
    const char *str;
    size_t len;
    int is_next = -1;
    int64_t trans_sec;
    int tz_id;
    JSTimeSpec ts;
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "getTimeZoneTransition requires a direction argument");
    if (JS_IsString(argv[0])) {
        str = JS_ToCStringLen(ctx, &len, argv[0]);
        if (!str)
            return JS_EXCEPTION;
        if (len == strlen(str) && strcmp(str, "next") == 0)
            is_next = 1;
        else if (len == strlen(str) && strcmp(str, "previous") == 0)
            is_next = 0;
        JS_FreeCString(ctx, str);
        if (is_next < 0)
            return JS_ThrowRangeError(ctx, "invalid direction");
    } else if (JS_IsObject(argv[0])) {
        direction_val = JS_GetPropertyStr(ctx, argv[0], "direction");
        if (JS_IsException(direction_val))
            return JS_EXCEPTION;
        if (JS_IsUndefined(direction_val))
            return JS_ThrowRangeError(ctx, "invalid direction");
        str = JS_ToCStringLen(ctx, &len, direction_val);
        JS_FreeValue(ctx, direction_val);
        if (!str)
            return JS_EXCEPTION;
        if (len == strlen(str) && strcmp(str, "next") == 0)
            is_next = 1;
        else if (len == strlen(str) && strcmp(str, "previous") == 0)
            is_next = 0;
        JS_FreeCString(ctx, str);
        if (is_next < 0)
            return JS_ThrowRangeError(ctx, "invalid direction");
    } else {
        return JS_ThrowTypeError(ctx, "options must be an object");
    }
    tz_id = ti_tz_find_id(s->timeZone);
    if (tz_id < 0)
        return JS_NULL;
    if (is_next)
        trans_sec = ti_tz_next_transition_at(tz_id, s->ts.secs, (int)s->ts.nsecs);
    else
        trans_sec = ti_tz_prev_transition_at(tz_id, s->ts.secs, (int)s->ts.nsecs);
    if (trans_sec == INT64_MIN)
        return JS_NULL;
    ts.secs = trans_sec;
    ts.nsecs = 0;
    return js_create_zoneddatetime(ctx, &ts, s->timeZone, s->calendar);
}

static void zdt_epoch_to_datetime(int64_t epoch_sec, int epoch_nsec,
                                   int *year, int *month, int *day,
                                   int *hour, int *minute, int *second,
                                   int *ms, int *us, int *ns)
{
    int64_t days = epoch_sec / 86400;
    int64_t sec_of_day = epoch_sec % 86400;
    if (sec_of_day < 0) {
        sec_of_day += 86400;
        days -= 1;
    }
    int64_t ordinal_1970 = pdt_to_ordinal_days(1970, 1, 1);
    int64_t ordinal = ordinal_1970 + days;
    pdt_from_ordinal_days(ordinal, year, month, day);
    *hour = (int)(sec_of_day / 3600);
    sec_of_day %= 3600;
    *minute = (int)(sec_of_day / 60);
    *second = (int)(sec_of_day % 60);
    *ms = epoch_nsec / 1000000;
    epoch_nsec %= 1000000;
    *us = epoch_nsec / 1000;
    *ns = epoch_nsec % 1000;
}

/* Convert a ZonedDateTime instant to local wall-clock ISO date/time */
void ti_zoneddatetime_to_local(const char *timeZone, int64_t epoch_sec, int epoch_nsec,
                                      int64_t *out_year, int *out_month, int *out_day,
                                      int *out_hour, int *out_minute, int *out_second,
                                      int *out_ms, int *out_us, int *out_ns)
{
    int64_t local_sec = epoch_sec;
    int local_nsec = epoch_nsec;
    int tz_id = ti_tz_find_id(timeZone);
    if (tz_id >= 0) {
        int offset_sec = ti_tz_offset_at_sec(tz_id, epoch_sec);
        local_sec += offset_sec;
    } else {
        int64_t offset_ns = 0;
        if (ti_parse_temporal_timezone_string(timeZone, &offset_ns) == 0) {
            int64_t offset_sec = offset_ns / 1000000000LL;
            int offset_nsec = (int)(offset_ns % 1000000000LL);
            local_sec += offset_sec;
            local_nsec += offset_nsec;
            if (local_nsec >= 1000000000) {
                local_nsec -= 1000000000;
                local_sec += 1;
            } else if (local_nsec < 0) {
                local_nsec += 1000000000;
                local_sec -= 1;
            }
        }
    }
    ti_epoch_sec_to_date(local_sec, out_year, out_month, out_day, out_hour, out_minute, out_second);
    if (out_ms)
        *out_ms = local_nsec / 1000000;
    if (out_us)
        *out_us = (local_nsec / 1000) % 1000;
    if (out_ns)
        *out_ns = local_nsec % 1000;
}

/* Spec AddZonedDateTime for ISO-like calendars.
   Returns 0 on success, -1 on error. */
int ti_zoneddatetime_add_duration_iso(const char *timeZone, CalendarType calendar,
                                       JSTimeSpec origin, JSDurationData *duration,
                                       int overflow_constrain, JSTimeSpec *out)
{
    /* Time-only path: exact instant arithmetic (AddInstant) */
    if (duration->years == 0 && duration->months == 0 && duration->weeks == 0 && duration->days == 0) {
        js_int128_t total_ns = ti_timespec_to_ns(&origin);
        total_ns += ti_duration_time_fields_to_ns(duration);
        js_int128_t limit = (js_int128_t)INSTANT_MAX_SECS * 1000000000LL;
        if (total_ns < -limit || total_ns > limit)
            return -1;
        ti_ns_to_timespec(total_ns, out);
        return 0;
    }

    /* Extract local wall-clock date/time */
    int64_t year64;
    int y, m, d, h, min, sec, ms, us, ns;
    ti_zoneddatetime_to_local(timeZone, origin.secs, (int)origin.nsecs, &year64, &m, &d, &h, &min, &sec, &ms, &us, &ns);
    y = (int)year64;

    /* Add date portion in ISO-like calendar space */
    int ay, am, ad;
    if (ti_iso_calendar_date_add(y, m, d,
                                  duration->years, duration->months, duration->weeks, duration->days,
                                  &ay, &am, &ad) < 0) {
        return -1;
    }
    if (!ti_is_date_within_iso_limits(ay, am, ad))
        return -1;

    /* Combine new date with original local time */
    int64_t days = pdt_to_ordinal_days(ay, am, ad);
    int64_t local_sec = (days - 719162) * 86400LL + h * 3600LL + min * 60LL + sec;
    int local_nsec = ms * 1000000 + us * 1000 + ns;

    /* Convert local back to UTC with compatible disambiguation */
    int tz_id = ti_tz_find_id(timeZone);
    int64_t utc_sec;
    if (tz_id >= 0) {
        int offset_sec;
        if (ti_tz_local_to_utc(tz_id, local_sec, 2, &utc_sec, &offset_sec) < 0)
            return -1;
    } else {
        int64_t offset_ns = 0;
        if (ti_parse_temporal_timezone_string(timeZone, &offset_ns) < 0)
            return -1;
        int64_t offset_sec = offset_ns / 1000000000LL;
        int offset_nsec = (int)(offset_ns % 1000000000LL);
        utc_sec = local_sec - offset_sec;
        local_nsec -= offset_nsec;
        if (local_nsec < 0) {
            local_nsec += 1000000000;
            utc_sec -= 1;
        } else if (local_nsec >= 1000000000) {
            local_nsec -= 1000000000;
            utc_sec += 1;
        }
    }

    /* Add time portion via instant arithmetic (AddInstant) */
    js_int128_t total_ns = ti_sec_nsec_to_ns(utc_sec, (uint32_t)local_nsec);
    total_ns += ti_duration_time_fields_to_ns(duration);

    js_int128_t limit = (js_int128_t)INSTANT_MAX_SECS * 1000000000LL;
    if (total_ns < -limit || total_ns > limit)
        return -1;

    ti_ns_to_timespec(total_ns, out);
    return 0;
}

/* Spec DifferenceZonedDateTime for ISO-like calendars.
   Returns 0 on success, -1 on error. */
int ti_zoneddatetime_difference_iso(const char *timeZone, CalendarType calendar,
                                     JSTimeSpec start, JSTimeSpec end,
                                     TIUnit largestUnit, JSDurationData *out)
{
    memset(out, 0, sizeof(*out));

    /* Convert to wall-clock PlainDateTimes */
    int64_t y1_64, y2_64;
    int y1, m1, d1, h1, min1, sec1, ms1, us1, ns1;
    int y2, m2, d2, h2, min2, sec2, ms2, us2, ns2;
    ti_zoneddatetime_to_local(timeZone, start.secs, (int)start.nsecs, &y1_64, &m1, &d1, &h1, &min1, &sec1, &ms1, &us1, &ns1);
    ti_zoneddatetime_to_local(timeZone, end.secs, (int)end.nsecs, &y2_64, &m2, &d2, &h2, &min2, &sec2, &ms2, &us2, &ns2);
    y1 = (int)y1_64;
    y2 = (int)y2_64;

    js_int128_t ns1_128 = ti_timespec_to_ns(&start);
    js_int128_t ns2_128 = ti_timespec_to_ns(&end);
    js_int128_t nsDiff = ns2_128 - ns1_128;

    if (largestUnit <= TI_UNIT_DAY) {
        if (nsDiff == 0)
            return 0;

        int polyfill_sign = (nsDiff < 0) ? 1 : -1;

        if (y1 == y2 && m1 == m2 && d1 == d2) {
            /* Same wall-clock calendar day: return instant difference */
            int sign = (nsDiff < 0) ? -1 : 1;
            int64_t abs_ns = (int64_t)(nsDiff >= 0 ? nsDiff : -nsDiff);
            int64_t total_seconds = abs_ns / 1000000000LL;
            int64_t ns = abs_ns % 1000000000LL;
            int64_t total_minutes = total_seconds / 60;
            int64_t seconds = total_seconds % 60;
            int64_t total_hours = total_minutes / 60;
            int64_t minutes = total_minutes % 60;
            int64_t hours = total_hours;
            out->hours = sign * hours;
            out->minutes = sign * minutes;
            out->seconds = sign * seconds;
            out->milliseconds = sign * (ns / 1000000LL);
            ns %= 1000000LL;
            out->microseconds.int128_value = sign * (ns / 1000LL);
            out->nanoseconds.int128_value = sign * (ns % 1000LL);
        } else {
            /* dayCorrection algorithm per spec */
            int dayCorrection = 0;
            int maxDayCorrection = (polyfill_sign == -1) ? 2 : 1;
            int64_t timeDiffNs = ((h2 - h1) * 3600LL + (min2 - min1) * 60LL + (sec2 - sec1)) * 1000000000LL
                               + (ms2 - ms1) * 1000000LL + (us2 - us1) * 1000LL + (ns2 - ns1);
            int timeSign = (timeDiffNs < 0) ? -1 : (timeDiffNs > 0) ? 1 : 0;
            if (timeSign == polyfill_sign)
                dayCorrection++;

            int int_y = y2, int_m = m2, int_d = d2;
            int64_t remNs = 0;
            int tz_id = ti_tz_find_id(timeZone);

            for (; dayCorrection <= maxDayCorrection; dayCorrection++) {
                int64_t end_days = pdt_to_ordinal_days(y2, m2, d2);
                int64_t int_days = end_days + dayCorrection * polyfill_sign;
                int64_t int_epoch_sec = (int_days - 719162) * 86400LL;
                int64_t tmp_y64;
                int tmp_h, tmp_min, tmp_sec;
                ti_epoch_sec_to_date(int_epoch_sec, &tmp_y64, &int_m, &int_d, &tmp_h, &tmp_min, &tmp_sec);
                int_y = (int)tmp_y64;

                int64_t local_sec = (int_days - 719162) * 86400LL
                                  + h1 * 3600LL + min1 * 60LL + sec1;
                int local_nsec = ms1 * 1000000 + us1 * 1000 + ns1;

                int64_t utc_sec;
                if (tz_id >= 0) {
                    int offset_sec;
                    if (ti_tz_local_to_utc(tz_id, local_sec, 2, &utc_sec, &offset_sec) < 0)
                        return -1;
                } else {
                    int64_t offset_ns = 0;
                    if (ti_parse_temporal_timezone_string(timeZone, &offset_ns) < 0)
                        return -1;
                    int64_t offset_sec = offset_ns / 1000000000LL;
                    utc_sec = local_sec - offset_sec;
                }

                js_int128_t intermediateNs = ti_sec_nsec_to_ns(utc_sec, (uint32_t)local_nsec);
                remNs = (int64_t)(ns2_128 - intermediateNs);
                int remSign = (remNs < 0) ? -1 : (remNs > 0) ? 1 : 0;
                if (remSign != polyfill_sign) {
                    break;
                }
            }

            TIUnit dateLargestUnit = (largestUnit <= TI_UNIT_DAY) ? largestUnit : TI_UNIT_DAY;
            int64_t date_years = 0, date_months = 0, date_weeks = 0, date_days = 0;
            ti_iso_calendar_date_until(y1, m1, d1, int_y, int_m, int_d,
                                        dateLargestUnit,
                                        &date_years, &date_months, &date_weeks, &date_days);

            out->years = date_years;
            out->months = date_months;
            out->weeks = date_weeks;
            out->days = date_days;

            int sign = (remNs < 0) ? -1 : 1;
            int64_t abs_ns = remNs >= 0 ? remNs : -remNs;
            int64_t total_seconds = abs_ns / 1000000000LL;
            int64_t ns = abs_ns % 1000000000LL;
            int64_t total_minutes = total_seconds / 60;
            int64_t seconds = total_seconds % 60;
            int64_t total_hours = total_minutes / 60;
            int64_t minutes = total_minutes % 60;
            int64_t hours = total_hours;
            out->hours = sign * hours;
            out->minutes = sign * minutes;
            out->seconds = sign * seconds;
            out->milliseconds = sign * (ns / 1000000LL);
            ns %= 1000000LL;
            out->microseconds.int128_value = sign * (ns / 1000LL);
            out->nanoseconds.int128_value = sign * (ns % 1000LL);
        }
    } else {
        /* Time units: instant difference */
        out->nanoseconds.int128_value = nsDiff;
        ti_balance_time_duration(out, largestUnit);
    }
    return 0;
}

static JSValue js_zoneddatetime_difference(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv, int is_until)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSValue other_val;
    JSZonedDateTimeData *other;
    JSDurationData d = {0};
    TIUnit largestUnit = TI_UNIT_INVALID;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int64_t roundingIncrement = 1;
    TIRoundingMode roundingMode = ROUND_TRUNC;
    if (!s)
        return JS_EXCEPTION;
    other_val = js_zoneddatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_zoneddatetime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowTypeError(ctx, "invalid ZonedDateTime");
    }
    if (s->calendar != other->calendar) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "calendars must be the same");
    }
    JSTimeSpec other_ts = other->ts;
    char other_tz[64];
    pstrcpy(other_tz, sizeof(other_tz), other->timeZone);
    JS_FreeValue(ctx, other_val);
    other = NULL;
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_parse_unit_option(ctx, argv[1], "largestUnit", &largestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED | TI_UNIT_OPT_ALLOW_AUTO, TI_UNIT_INVALID) < 0)
            return JS_EXCEPTION;

        if (ti_get_rounding_increment_option(ctx, argv[1], &roundingIncrement) < 0)
            return JS_EXCEPTION;

        if (ti_get_rounding_mode_option_default(ctx, argv[1], &roundingMode, ROUND_TRUNC) < 0)
            return JS_EXCEPTION;

        if (ti_parse_unit_option(ctx, argv[1], "smallestUnit", &smallestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
            return JS_EXCEPTION;
    }
    if (smallestUnit == TI_UNIT_INVALID)
        smallestUnit = TI_UNIT_NANOSECOND;
    if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO)
        largestUnit = (smallestUnit < TI_UNIT_HOUR) ? smallestUnit : TI_UNIT_HOUR;

    if (largestUnit > smallestUnit) {
        return JS_ThrowRangeError(ctx, "invalid unit combination");
    }
    if (largestUnit <= TI_UNIT_DAY && (s->ts.secs != other_ts.secs || s->ts.nsecs != other_ts.nsecs) &&
        !ti_timezones_are_equal(s->timeZone, other_tz)) {
        return JS_ThrowRangeError(ctx, "cannot compute difference between different time zones");
    }

    /* roundingIncrement validation for time units */
    if (smallestUnit >= TI_UNIT_HOUR) {
        int64_t maximum = 0;
        switch (smallestUnit) {
        case TI_UNIT_HOUR:        maximum = 24; break;
        case TI_UNIT_MINUTE:      maximum = 60; break;
        case TI_UNIT_SECOND:      maximum = 60; break;
        case TI_UNIT_MILLISECOND: maximum = 1000; break;
        case TI_UNIT_MICROSECOND: maximum = 1000; break;
        case TI_UNIT_NANOSECOND:  maximum = 1000; break;
        default: break;
        }
        if (maximum > 0 && (roundingIncrement >= maximum || maximum % roundingIncrement != 0))
            return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
    }

    /* Convert to wall-clock PlainDateTimes */
    int64_t epoch_sec1 = s->ts.secs;
    int epoch_nsec1 = (int)s->ts.nsecs;
    zdt_apply_timezone_offset(ctx, s, &epoch_sec1, &epoch_nsec1);
    int y1, m1, d1, h1, min1, sec1, ms1, us1, ns1;
    zdt_epoch_to_datetime(epoch_sec1, epoch_nsec1, &y1, &m1, &d1, &h1, &min1, &sec1, &ms1, &us1, &ns1);

    int64_t epoch_sec2 = other_ts.secs;
    int epoch_nsec2 = (int)other_ts.nsecs;
    JSZonedDateTimeData other_tmp = {0};
    other_tmp.ts = other_ts;
    pstrcpy(other_tmp.timeZone, sizeof(other_tmp.timeZone), other_tz);
    zdt_apply_timezone_offset(ctx, &other_tmp, &epoch_sec2, &epoch_nsec2);
    int y2, m2, d2, h2, min2, sec2, ms2, us2, ns2;
    zdt_epoch_to_datetime(epoch_sec2, epoch_nsec2, &y2, &m2, &d2, &h2, &min2, &sec2, &ms2, &us2, &ns2);

    js_int128_t diff_ns = ti_timespec_to_ns(&other_ts)
                     - ti_timespec_to_ns(&s->ts);

    if (largestUnit <= TI_UNIT_DAY) {
        if (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY ||
            s->calendar == CAL_BUDDHIST || s->calendar == CAL_ROC || s->calendar == CAL_JAPANESE) {
            /* Spec DifferenceZonedDateTime algorithm for ISO-like calendars */
            js_int128_t ns1_128 = ti_timespec_to_ns(&s->ts);
            js_int128_t ns2_128 = ti_timespec_to_ns(&other_ts);
            js_int128_t nsDiff = ns2_128 - ns1_128;
            if (nsDiff == 0) {
                memset(&d, 0, sizeof(d));
            } else {
                int polyfill_sign = (nsDiff < 0) ? 1 : -1;
                if (y1 == y2 && m1 == m2 && d1 == d2) {
                    /* Same wall-clock calendar day: return instant difference */
                    int sign = (nsDiff < 0) ? -1 : 1;
                    int64_t abs_ns = (int64_t)(nsDiff >= 0 ? nsDiff : -nsDiff);
                    int64_t total_seconds = abs_ns / 1000000000LL;
                    int64_t ns = abs_ns % 1000000000LL;
                    int64_t total_minutes = total_seconds / 60;
                    int64_t seconds = total_seconds % 60;
                    int64_t total_hours = total_minutes / 60;
                    int64_t minutes = total_minutes % 60;
                    int64_t hours = total_hours;
                    memset(&d, 0, sizeof(d));
                    d.hours = sign * hours;
                    d.minutes = sign * minutes;
                    d.seconds = sign * seconds;
                    d.milliseconds = sign * (ns / 1000000LL);
                    ns %= 1000000LL;
                    d.microseconds.int128_value = sign * (ns / 1000LL);
                    d.nanoseconds.int128_value = sign * (ns % 1000LL);
                } else {
                    /* dayCorrection algorithm */
                    int dayCorrection = 0;
                    int maxDayCorrection = (polyfill_sign == -1) ? 2 : 1;
                    int64_t timeDiffNs = ((h2 - h1) * 3600LL + (min2 - min1) * 60LL + (sec2 - sec1)) * 1000000000LL
                                       + (ms2 - ms1) * 1000000LL + (us2 - us1) * 1000LL + (ns2 - ns1);
                    int timeSign = (timeDiffNs < 0) ? -1 : (timeDiffNs > 0) ? 1 : 0;
                    if (timeSign == polyfill_sign) {
                        dayCorrection++;
                    }
                    int int_y = y2, int_m = m2, int_d = d2;
                    int64_t remNs = 0;
                    int found = 0;
                    for (; dayCorrection <= maxDayCorrection; dayCorrection++) {
                        int64_t end_days = pdt_to_ordinal_days(y2, m2, d2);
                        int64_t int_days = end_days + dayCorrection * polyfill_sign;
                        int64_t int_epoch_sec = (int_days - 719162) * 86400LL;
                        int64_t tmp_y64;
                        int tmp_h, tmp_min, tmp_sec;
                        ti_epoch_sec_to_date(int_epoch_sec, &tmp_y64, &int_m, &int_d,
                                             &tmp_h, &tmp_min, &tmp_sec);
                        int_y = (int)tmp_y64;
                        int64_t local_sec = (int_days - 719162) * 86400LL
                                          + h1 * 3600LL + min1 * 60LL + sec1;
                        int local_nsec = ms1 * 1000000 + us1 * 1000 + ns1;
                        int tz_id = ti_tz_find_id(s->timeZone);
                        int64_t utc_sec;
                        if (tz_id >= 0) {
                            int offset_sec;
                            if (ti_tz_local_to_utc(tz_id, local_sec, 2, &utc_sec, &offset_sec) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                        } else {
                            int64_t offset_ns = 0;
                            if (ti_parse_temporal_timezone_string(s->timeZone, &offset_ns) < 0)
                                return JS_ThrowRangeError(ctx, "unsupported time zone for difference");
                            int64_t offset_sec = offset_ns / 1000000000LL;
                            utc_sec = local_sec - offset_sec;
                        }
                        js_int128_t intermediateNs = ti_sec_nsec_to_ns(utc_sec, (uint32_t)local_nsec);
                        remNs = (int64_t)(ns2_128 - intermediateNs);
                        int remSign = (remNs < 0) ? -1 : (remNs > 0) ? 1 : 0;
                        if (remSign != polyfill_sign) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        /* Should not happen per spec; use last remNs */
                    }
                    TIUnit dateLargestUnit = (largestUnit <= TI_UNIT_DAY) ? largestUnit : TI_UNIT_DAY;
                    int64_t date_years = 0, date_months = 0, date_weeks = 0, date_days = 0;
                    ti_iso_calendar_date_until(y1, m1, d1, int_y, int_m, int_d,
                                                dateLargestUnit,
                                                &date_years, &date_months, &date_weeks, &date_days);
                    memset(&d, 0, sizeof(d));
                    d.years = date_years;
                    d.months = date_months;
                    d.weeks = date_weeks;
                    d.days = date_days;
                    int sign = (remNs < 0) ? -1 : 1;
                    int64_t abs_ns = remNs >= 0 ? remNs : -remNs;
                    int64_t total_seconds = abs_ns / 1000000000LL;
                    int64_t ns = abs_ns % 1000000000LL;
                    int64_t total_minutes = total_seconds / 60;
                    int64_t seconds = total_seconds % 60;
                    int64_t total_hours = total_minutes / 60;
                    int64_t minutes = total_minutes % 60;
                    int64_t hours = total_hours;
                    d.hours = sign * hours;
                    d.minutes = sign * minutes;
                    d.seconds = sign * seconds;
                    d.milliseconds = sign * (ns / 1000000LL);
                    ns %= 1000000LL;
                    d.microseconds.int128_value = sign * (ns / 1000LL);
                    d.nanoseconds.int128_value = sign * (ns % 1000LL);
                }
            }
        } else {
            /* Non-ISO calendar: fallback to wall-clock PlainDateTime difference */
            if (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) {
                int s_cal_y, s_cal_m, s_cal_d;
                int o_cal_y, o_cal_m, o_cal_d;
                ti_iso_to_calendar_date(s->calendar, y1, m1, d1, &s_cal_y, &s_cal_m, &s_cal_d);
                ti_iso_to_calendar_date(s->calendar, y2, m2, d2, &o_cal_y, &o_cal_m, &o_cal_d);
                ti_calendar_datetime_difference(s->calendar,
                    s_cal_y, s_cal_m, s_cal_d, h1, min1, sec1, ms1, us1, ns1,
                    o_cal_y, o_cal_m, o_cal_d, h2, min2, sec2, ms2, us2, ns2,
                    largestUnit, &d);
            } else {
                ti_iso_datetime_difference(y1, m1, d1, h1, min1, sec1, ms1, us1, ns1,
                                            y2, m2, d2, h2, min2, sec2, ms2, us2, ns2,
                                            largestUnit, &d);
            }
        }
        if (smallestUnit != TI_UNIT_NANOSECOND || roundingIncrement != 1) {
            TIRoundingMode rm = is_until ? roundingMode : ti_negate_rounding_mode(roundingMode);
            if (ti_round_relative_duration_iso(ctx,
                                                y1, m1, d1, h1, min1, sec1, ms1, us1, ns1,
                                                &d, largestUnit, smallestUnit, roundingIncrement,
                                                rm, TRUE, &d) < 0)
                return JS_EXCEPTION;
        }
        if (s->calendar != CAL_ISO8601 && s->calendar != CAL_GREGORY &&
            s->calendar != CAL_BUDDHIST && s->calendar != CAL_ROC && s->calendar != CAL_JAPANESE) {
            ti_balance_time_duration(&d, largestUnit);
        }
    } else {
        /* Time units: use instant difference */
        if (smallestUnit != TI_UNIT_NANOSECOND || roundingIncrement != 1) {
            js_int128_t unit_ns = 1;
            switch (smallestUnit) {
            case TI_UNIT_HOUR:        unit_ns = 3600000000000LL; break;
            case TI_UNIT_MINUTE:      unit_ns = 60000000000LL; break;
            case TI_UNIT_SECOND:      unit_ns = 1000000000LL; break;
            case TI_UNIT_MILLISECOND: unit_ns = 1000000LL; break;
            case TI_UNIT_MICROSECOND: unit_ns = 1000LL; break;
            case TI_UNIT_NANOSECOND:  unit_ns = 1LL; break;
            default: break;
            }
            TIRoundingMode rm = is_until ? roundingMode : ti_negate_rounding_mode(roundingMode);
            diff_ns = ti_round_to_increment128(diff_ns, unit_ns * roundingIncrement, rm);
        }
        d.nanoseconds.int128_value = diff_ns;
        ti_balance_time_duration(&d, largestUnit);
    }

    if (!is_until) {
        d.years = -d.years;
        d.months = -d.months;
        d.weeks = -d.weeks;
        d.days = -d.days;
        d.hours = -d.hours;
        d.minutes = -d.minutes;
        d.seconds = -d.seconds;
        d.milliseconds = -d.milliseconds;
        js_int128_t us = d.microseconds.int128_value;
        d.microseconds.int128_value = -us;
        js_int128_t ns = d.nanoseconds.int128_value;
        d.nanoseconds.int128_value = -ns;
    }

    return js_create_duration(ctx, &d);
}

static JSValue js_zoneddatetime_round(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)argc;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSValue options;
    const char *unit_str;
    int64_t unit_ns, increment = 1;
    TIRoundingMode mode = ROUND_TRUNC;
    js_int128_t total_ns, rounded_ns;
    JSTimeSpec result;
    
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "round requires an options object");

    if (JS_IsString(argv[0])) {
        TIUnit unit;
        size_t len;
        unit_str = JS_ToCStringLen(ctx, &len, argv[0]);
        if (!unit_str)
            return JS_EXCEPTION;
        unit = ti_string_to_unit(unit_str, len);
        JS_FreeCString(ctx, unit_str);
        if (unit == TI_UNIT_INVALID)
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        unit_ns = ti_unit_to_ns(unit);
        options = JS_NULL;
        mode = ROUND_HALF_EXPAND;
    } else if (JS_IsObject(argv[0])) {
        TIUnit unit;
        options = JS_DupValue(ctx, argv[0]);
        
        if (ti_get_rounding_increment_option(ctx, options, &increment) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        if (ti_get_rounding_mode_option(ctx, options, &mode) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        
        if (ti_parse_unit_option(ctx, options, "smallestUnit", &unit, 0, 0) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
        unit_ns = ti_unit_to_ns(unit);
        
        JS_FreeValue(ctx, options);
    } else {
        return JS_ThrowTypeError(ctx, "round requires an options object");
    }
    
    if (unit_ns == 0)
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");
    
    {
        int64_t maximum;
        int inclusive;
        switch (unit_ns) {
        case 86400000000000LL: maximum = 1; inclusive = 1; break;
        case 3600000000000LL: maximum = 24; inclusive = 0; break;
        case 60000000000LL:   maximum = 60; inclusive = 0; break;
        case 1000000000LL:    maximum = 60; inclusive = 0; break;
        case 1000000LL:       maximum = 1000; inclusive = 0; break;
        case 1000LL:          maximum = 1000; inclusive = 0; break;
        case 1LL:             maximum = 1000; inclusive = 0; break;
        default:              maximum = 0; inclusive = 0; break;
        }
        if ((inclusive ? increment > maximum : increment >= maximum) || maximum % increment != 0) {
            return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
        }
    }

    if (unit_ns == 86400000000000LL) {
        /* Day rounding: use GetStartOfDay for day boundaries */
        int64_t offset_sec = 0;
        int64_t local_sec, day_number;
        int64_t start_today_utc, start_tomorrow_utc;
        int64_t day_length_ns;
        int64_t diff_ns;
        int tz_id = ti_tz_find_id(s->timeZone);
        if (tz_id >= 0) {
            offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
        } else {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret != 0)
                return JS_ThrowRangeError(ctx, "unsupported time zone");
            offset_sec = (int)(offset_ns / 1000000000LL);
        }
        local_sec = s->ts.secs + offset_sec;
        day_number = floor_div64(local_sec, 86400LL);
        if (tz_id >= 0) {
            start_today_utc = ti_get_start_of_day_sec(tz_id, day_number * 86400LL);
            start_tomorrow_utc = ti_get_start_of_day_sec(tz_id, (day_number + 1) * 86400LL);
        } else {
            start_today_utc = day_number * 86400LL - offset_sec;
            start_tomorrow_utc = (day_number + 1) * 86400LL - offset_sec;
        }
        {
            JSTimeSpec today_ts, tomorrow_ts;
            today_ts.secs = start_today_utc;
            today_ts.nsecs = 0;
            tomorrow_ts.secs = start_tomorrow_utc;
            tomorrow_ts.nsecs = 0;
            if (!is_valid_epoch_timespec(&today_ts) || !is_valid_epoch_timespec(&tomorrow_ts))
                return JS_ThrowRangeError(ctx, "outside representable range");
        }
        day_length_ns = (start_tomorrow_utc - start_today_utc) * 1000000000LL;
        {
            js_int128_t instant_ns = ti_timespec_to_ns(&s->ts);
            js_int128_t end_ns = (js_int128_t)start_tomorrow_utc * 1000000000LL;
            /* Handle the case where a transition starts after midnight and falls back
               to before midnight, and pieces of two calendar days are interleaved.
               https://github.com/tc39/proposal-temporal/issues/3312 */
            if (instant_ns >= end_ns) {
                instant_ns = end_ns - 1;
            }
            diff_ns = (int64_t)(instant_ns - (js_int128_t)start_today_utc * 1000000000LL);
        }
        if (day_length_ns > 0) {
            int64_t rounded_diff = ti_round_to_increment_as_if_positive(diff_ns, day_length_ns, mode);
            rounded_ns = (js_int128_t)start_today_utc * 1000000000LL + rounded_diff;
        } else {
            rounded_ns = ti_timespec_to_ns(&s->ts);
        }
    } else {
        /* Round in local time: apply offset, round, subtract offset */
        int64_t offset_ns = 0;
        {
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret == 0) {
                total_ns = ti_timespec_to_ns(&s->ts);
                total_ns += offset_ns;
                {
                    js_int128_t inc_ns = unit_ns * increment;
                    rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, inc_ns, mode);
                }
                rounded_ns -= offset_ns;
            } else {
                total_ns = ti_timespec_to_ns(&s->ts);
                {
                    js_int128_t inc_ns = unit_ns * increment;
                    rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, inc_ns, mode);
                }
            }
        }
    }

    ti_ns_to_timespec(rounded_ns, &result);

    if (!is_valid_epoch_timespec(&result))
        return JS_ThrowRangeError(ctx, "outside representable range");
    return js_create_zoneddatetime(ctx, &result, s->timeZone, s->calendar);
}

static JSValue js_zoneddatetime_withTimeZone(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    (void)argc;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSZonedDateTimeData *t;
    JSValue obj;
    const char *str;
    int64_t offset_ns;
    int ret;
    if (!s)
        return JS_EXCEPTION;
    if (!JS_IsString(argv[0]))
        return JS_ThrowTypeError(ctx, "timeZone must be a string");
    str = JS_ToCString(ctx, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    /* Validate timezone string */
    ret = ti_parse_temporal_timezone_string(str, &offset_ns);
    if (ret < 0) {
        JS_FreeCString(ctx, str);
        return JS_ThrowRangeError(ctx, "unsupported time zone");
    }
    t = js_mallocz(ctx, sizeof(*t));
    if (!t) {
        JS_FreeCString(ctx, str);
        return JS_EXCEPTION;
    }
    *t = *s;
    {
        size_t len = strlen(str);
        const char *bracket_open = strchr(str, '[');
        if (bracket_open) {
            const char *bracket_close = strchr(bracket_open, ']');
            if (bracket_close && bracket_close[1] == '\0') {
                int inner_len = (int)(bracket_close - bracket_open - 1);
                if (inner_len > 0 && inner_len < (int)sizeof(t->timeZone)) {
                    memcpy(t->timeZone, bracket_open + 1, (size_t)inner_len);
                    t->timeZone[inner_len] = '\0';
                }
            }
        } else {
            int is_utc = (len == 3 && (str[0] == 'U' || str[0] == 'u') &&
                          (str[1] == 'T' || str[1] == 't') &&
                          (str[2] == 'C' || str[2] == 'c'));
            int is_iso = 0;
            if (strchr(str, ':') != NULL) {
                is_iso = 1;
            } else {
                const char *p = str;
                while ((p = strchr(p, 'T')) != NULL) {
                    if (p > str && isdigit((unsigned char)p[-1]) && isdigit((unsigned char)p[1])) {
                        is_iso = 1;
                        break;
                    }
                    p++;
                }
            }
            if (!is_utc && is_iso && len > 1 && str[len - 1] == 'Z')
                is_utc = 1;
            if (!is_utc && !is_iso && str[0] != '+' && str[0] != '-') {
                /* IANA name: preserve as-is */
                pstrcpy(t->timeZone, sizeof(t->timeZone), str);
            } else {
                ti_format_canonical_timezone(offset_ns, is_utc, t->timeZone, sizeof(t->timeZone));
            }
        }
        /* Canonicalize UTC */
        if (t->timeZone[0] == 'u' || t->timeZone[0] == 'U') {
            if ((t->timeZone[1] == 't' || t->timeZone[1] == 'T') &&
                (t->timeZone[2] == 'c' || t->timeZone[2] == 'C') && t->timeZone[3] == '\0')
                pstrcpy(t->timeZone, sizeof(t->timeZone), "UTC");
        }
    }
    JS_FreeCString(ctx, str);
    obj = JS_NewObjectClass(ctx, (int)js_zoneddatetime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, t);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, t);
    return obj;
}

static JSValue js_zoneddatetime_with(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t year;
    int month, day, hour, minute, second, ms, us, ns;
    int has_year_prop = 0;
    int has_month_prop = 0;
    int has_month_code = 0;
    int month_from_code = 0;
    int mc_has_l = 0;
    int overflow_constrain = 1;
    int64_t parsed_offset_ns = 0;
    int64_t original_offset_sec = 0;
    int offset_idx = 3; /* default reject */
    int dis_idx = 2; /* default compatible */
    JSTimeSpec ts;
    TIPreparedFields fields;
    if (!s)
        return JS_EXCEPTION;
    {
        int64_t offset_sec = 0;
        int tz_id = ti_tz_find_id(s->timeZone);
        if (tz_id >= 0) {
            offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
        } else {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret < 0)
                return JS_ThrowRangeError(ctx, "invalid timeZone");
            offset_sec = offset_ns / 1000000000LL;
        }
        original_offset_sec = offset_sec;
        {
            int64_t local_sec = s->ts.secs + offset_sec;
            int local_nsec = (int)s->ts.nsecs;
            if (local_nsec < 0) {
                local_sec -= 1;
                local_nsec += 1000000000;
            }
            ti_epoch_sec_to_date(local_sec, &year, &month, &day, &hour, &minute, &second);
            ms = local_nsec / 1000000;
            us = (local_nsec / 1000) % 1000;
            ns = local_nsec % 1000;
        }
    }
    if (ti_is_partial_temporal_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, argv[0],
                                    TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                    TI_FIELD_YEAR | TI_FIELD_HOUR | TI_FIELD_MINUTE |
                                    TI_FIELD_SECOND | TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND |
                                    TI_FIELD_NANOSECOND | TI_FIELD_OFFSET,
                                    0, 1, &fields, s->calendar) < 0)
        return JS_EXCEPTION;
    /* Convert current ISO date to calendar space */
    int cal_year = (int)year, cal_month = month, cal_day = day;
    int orig_cal_year, orig_cal_month;
    ti_iso_to_calendar_date(s->calendar, (int)year, month, day, &cal_year, &cal_month, &cal_day);
    orig_cal_year = cal_year;
    orig_cal_month = cal_month;

    /* Merge fields in calendar space */
    if (fields.present_mask & TI_FIELD_DAY) {
        cal_day = fields.day;
    }
    if (fields.present_mask & TI_FIELD_HOUR) {
        hour = fields.hour;
    }
    if (fields.present_mask & TI_FIELD_MICROSECOND) {
        us = fields.microsecond;
    }
    if (fields.present_mask & TI_FIELD_MILLISECOND) {
        ms = fields.millisecond;
    }
    if (fields.present_mask & TI_FIELD_MINUTE) {
        minute = fields.minute;
    }
    if (fields.present_mask & TI_FIELD_MONTH) {
        has_month_prop = 1;
        cal_month = fields.month;
    }
    if (fields.present_mask & TI_FIELD_MONTH_CODE) {
        has_month_code = 1;
        month_from_code = fields.month_from_code;
        mc_has_l = fields.month_code_has_l;
    }
    if (fields.present_mask & TI_FIELD_NANOSECOND) {
        ns = fields.nanosecond;
    }
    if (fields.present_mask & TI_FIELD_OFFSET) {
        parsed_offset_ns = fields.offset_ns;
    } else {
        parsed_offset_ns = original_offset_sec * 1000000000LL;
    }
    if (fields.present_mask & TI_FIELD_SECOND) {
        second = fields.second;
    }
    if (fields.present_mask & TI_FIELD_YEAR) {
        has_year_prop = 1;
        cal_year = (int)fields.year;
    }
    /* era/eraYear processing */
    if (fields.present_mask & (TI_FIELD_ERA | TI_FIELD_ERA_YEAR)) {
        if (!ti_calendar_requires_era(s->calendar))
            return JS_ThrowTypeError(ctx, "era and eraYear are invalid for this calendar");
        if (!(fields.present_mask & TI_FIELD_ERA) || !(fields.present_mask & TI_FIELD_ERA_YEAR))
            return JS_ThrowTypeError(ctx, "era and eraYear must both be present");
        {
            int cal_year_from_era;
            if (fields.era == TI_ERA_UNKNOWN || ti_calendar_era_to_calendar_year(s->calendar, fields.era, (int)fields.era_year, &cal_year_from_era) < 0)
                return JS_ThrowRangeError(ctx, "invalid era");
            if (has_year_prop) {
                if (cal_year != cal_year_from_era)
                    return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
            } else {
                cal_year = cal_year_from_era;
            }
        }
    }
    /* Validate options type */
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        static const char *dis_values[] = { "earlier", "later", "compatible", "reject" };
        static const char *offset_values[] = { "use", "ignore", "prefer", "reject" };
        dis_idx = ti_get_string_option(ctx, argv[1], "disambiguation", dis_values, 4, 2);
        if (dis_idx < 0)
            return JS_EXCEPTION;
        offset_idx = ti_get_string_option(ctx, argv[1], "offset", offset_values, 4, 3);
        if (offset_idx < 0)
            return JS_EXCEPTION;
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
    }
    /* monthCode processing */
    if (has_month_code) {
        if (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) {
            if (month_from_code < 1 || month_from_code > 12 || mc_has_l)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for ISO 8601 calendar");
        } else {
            if (!ti_calendar_validate_month_code(s->calendar, month_from_code, mc_has_l))
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
        }
    }
    {
        int resolved_month = 0;
        if (has_month_code)
            resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, month_from_code, mc_has_l);
        if (has_month_prop && has_month_code) {
            if (resolved_month >= 0 && cal_month != resolved_month)
                return JS_ThrowRangeError(ctx, "month and monthCode must agree");
        }
        if (has_month_code)
            cal_month = resolved_month;
    }
    /* If monthCode was preserved from source but year changed, re-resolve */
    if (!has_month_code && !has_month_prop && cal_year != orig_cal_year) {
        int orig_mc, orig_has_l;
        int result_mc, result_has_l;
        int resolved_month;
        ti_calendar_month_to_month_code(s->calendar, orig_cal_year, orig_cal_month, &orig_mc, &orig_has_l);
        resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, orig_has_l);
        if (resolved_month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid ZonedDateTime date");
            resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, 0);
        }
        cal_month = resolved_month;
        ti_calendar_month_to_month_code(s->calendar, cal_year, cal_month, &result_mc, &result_has_l);
        if (!overflow_constrain && (result_mc != orig_mc || result_has_l != orig_has_l))
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime date");
    }
    if (!overflow_constrain) {
        if (cal_month < 1 || cal_month > ti_calendar_months_in_year(s->calendar, cal_year))
            return JS_ThrowRangeError(ctx, "invalid month");
        if (cal_day < 1 || cal_day > ti_calendar_days_in_month(s->calendar, cal_year, cal_month))
            return JS_ThrowRangeError(ctx, "invalid day");
        if (hour < 0 || hour > 23)
            return JS_ThrowRangeError(ctx, "invalid hour");
        if (minute < 0 || minute > 59)
            return JS_ThrowRangeError(ctx, "invalid minute");
        if (second < 0 || second > 59)
            return JS_ThrowRangeError(ctx, "invalid second");
        if (ms < 0 || ms > 999)
            return JS_ThrowRangeError(ctx, "invalid millisecond");
        if (us < 0 || us > 999)
            return JS_ThrowRangeError(ctx, "invalid microsecond");
        if (ns < 0 || ns > 999)
            return JS_ThrowRangeError(ctx, "invalid nanosecond");
    } else {
        if (cal_month > ti_calendar_months_in_year(s->calendar, cal_year))
            cal_month = ti_calendar_months_in_year(s->calendar, cal_year);
        else if (cal_month < 1) cal_month = 1;
        {
            int dim = ti_calendar_days_in_month(s->calendar, cal_year, cal_month);
            if (cal_day > dim) cal_day = dim;
            else if (cal_day < 1) cal_day = 1;
        }
        if (hour > 23) hour = 23; else if (hour < 0) hour = 0;
        if (minute > 59) minute = 59; else if (minute < 0) minute = 0;
        if (second > 59) second = 59; else if (second < 0) second = 0;
        if (ms > 999) ms = 999; else if (ms < 0) ms = 0;
        if (us > 999) us = 999; else if (us < 0) us = 0;
        if (ns > 999) ns = 999; else if (ns < 0) ns = 0;
    }
    {
        int iso_year, iso_month, iso_day;
        ti_calendar_date_to_iso(s->calendar, cal_year, cal_month, cal_day, &iso_year, &iso_month, &iso_day);
        year = iso_year;
        month = iso_month;
        day = iso_day;
    }
    {
        int64_t days = pdt_to_ordinal_days((int)year, month, day);
        int64_t local_sec = (days - 719162) * 86400LL + hour * 3600LL + minute * 60LL + second;
        int local_nsec = ms * 1000000 + us * 1000 + ns;
        int tz_id = ti_tz_find_id(s->timeZone);
        if (tz_id >= 0) {
            if (offset_idx == 0) { /* use */
                int64_t offset_sec = parsed_offset_ns / 1000000000LL;
                int offset_nsec = (int)(parsed_offset_ns % 1000000000LL);
                ts.secs = local_sec - offset_sec;
                {
                    int new_nsec = local_nsec - offset_nsec;
                    if (new_nsec < 0) {
                        new_nsec += 1000000000;
                        ts.secs -= 1;
                    } else if (new_nsec >= 1000000000) {
                        new_nsec -= 1000000000;
                        ts.secs += 1;
                    }
                    ts.nsecs = (uint32_t)new_nsec;
                }
            } else if (offset_idx == 1) { /* ignore */
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, local_sec, dis_idx, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                ts.secs = utc_sec;
                ts.nsecs = (uint32_t)local_nsec;
            } else { /* prefer / reject */
                int64_t cand1_utc, cand2_utc;
                int cand1_off, cand2_off;
                if (ti_tz_local_to_utc(tz_id, local_sec, 0, &cand1_utc, &cand1_off) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                if (ti_tz_local_to_utc(tz_id, local_sec, 1, &cand2_utc, &cand2_off) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                {
                    int64_t cand1_off_ns = (int64_t)cand1_off * 1000000000LL;
                    int64_t cand2_off_ns = (int64_t)cand2_off * 1000000000LL;
                    int64_t utc_sec;
                    int offset_sec;
                    if (parsed_offset_ns == cand1_off_ns) {
                        utc_sec = cand1_utc;
                        offset_sec = cand1_off;
                    } else if (parsed_offset_ns == cand2_off_ns) {
                        utc_sec = cand2_utc;
                        offset_sec = cand2_off;
                    } else if (offset_idx == 3) { /* reject: no match */
                        return JS_ThrowRangeError(ctx, "offset does not match time zone");
                    } else { /* prefer: fallback to disambiguation */
                        if (ti_tz_local_to_utc(tz_id, local_sec, dis_idx, &utc_sec, &offset_sec) < 0)
                            return JS_ThrowRangeError(ctx, "multiple instants found");
                    }
                    ts.secs = utc_sec;
                    ts.nsecs = (uint32_t)local_nsec;
                }
            }
        } else {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret < 0)
                return JS_ThrowRangeError(ctx, "invalid timeZone");
            if (offset_idx == 0)
                offset_ns = parsed_offset_ns;
            {
                int64_t offset_sec = offset_ns / 1000000000LL;
                int offset_nsec = (int)(offset_ns % 1000000000LL);
                ts.secs = local_sec - offset_sec;
                {
                    int new_nsec = local_nsec - offset_nsec;
                    if (new_nsec < 0) {
                        new_nsec += 1000000000;
                        ts.secs -= 1;
                    } else if (new_nsec >= 1000000000) {
                        new_nsec -= 1000000000;
                        ts.secs += 1;
                    }
                    ts.nsecs = (uint32_t)new_nsec;
                }
            }
        }
    }
    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "epoch nanoseconds outside valid limits");
    return js_create_zoneddatetime(ctx, &ts, s->timeZone, s->calendar);
}

static JSValue js_zoneddatetime_withPlainTime(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    int64_t year;
    int month, day;
    JSTimeSpec ts;
    JSPlainTimeData *pt;
    JSValue pt_val;
    int has_plainTime = (argc >= 1 && !JS_IsUndefined(argv[0]));
    if (!s)
        return JS_EXCEPTION;
    /* Extract local ISO date from the ZDT */
    {
        int64_t offset_sec = 0;
        int tz_id = ti_tz_find_id(s->timeZone);
        if (tz_id >= 0) {
            offset_sec = ti_tz_offset_at_sec(tz_id, s->ts.secs);
        } else {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret < 0)
                return JS_ThrowRangeError(ctx, "invalid timeZone");
            offset_sec = offset_ns / 1000000000LL;
        }
        {
            int64_t local_sec = s->ts.secs + offset_sec;
            int h = 0, m = 0, sec = 0;
            ti_epoch_sec_to_date(local_sec, &year, &month, &day, &h, &m, &sec);
        }
    }
    /* Get time from argument, default to midnight */
    if (has_plainTime) {
        pt_val = js_plaintime_from(ctx, JS_UNDEFINED, 1, argv);
    } else {
        JSPlainTimeData tmp = {0};
        pt_val = js_create_plaintime(ctx, &tmp);
    }
    if (JS_IsException(pt_val))
        return pt_val;
    pt = JS_GetOpaque(pt_val, js_plaintime_class_id);
    if (!pt) {
        JS_FreeValue(ctx, pt_val);
        return JS_ThrowTypeError(ctx, "invalid PlainTime");
    }
    {
        int64_t days = pdt_to_ordinal_days((int)year, month, day);
        int64_t local_sec = (days - 719162) * 86400LL + pt->hour * 3600LL + pt->minute * 60LL + pt->second;
        int local_nsec = pt->millisecond * 1000000 + pt->microsecond * 1000 + pt->nanosecond;
        int tz_id = ti_tz_find_id(s->timeZone);
        if (tz_id >= 0) {
            if (has_plainTime) {
                /* withPlainTime(plainTime): use compatible disambiguation */
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, local_sec, 2, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                ts.secs = utc_sec;
                ts.nsecs = (uint32_t)local_nsec;
            } else {
                /* withPlainTime(): use GetStartOfDay semantics */
                int64_t utc_sec = ti_get_start_of_day_sec(tz_id, local_sec);
                ts.secs = utc_sec;
                ts.nsecs = (uint32_t)local_nsec;
            }
        } else {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
            if (ret < 0)
                return JS_ThrowRangeError(ctx, "invalid timeZone");
            {
                int64_t offset_sec = offset_ns / 1000000000LL;
                int offset_nsec = (int)(offset_ns % 1000000000LL);
                ts.secs = local_sec - offset_sec;
                {
                    int new_nsec = local_nsec - offset_nsec;
                    if (new_nsec < 0) {
                        new_nsec += 1000000000;
                        ts.secs -= 1;
                    } else if (new_nsec >= 1000000000) {
                        new_nsec -= 1000000000;
                        ts.secs += 1;
                    }
                    ts.nsecs = (uint32_t)new_nsec;
                }
            }
        }
    }
    JS_FreeValue(ctx, pt_val);
    if (!is_valid_epoch_timespec(&ts))
        return JS_ThrowRangeError(ctx, "invalid epoch nanoseconds");
    return js_create_zoneddatetime(ctx, &ts, s->timeZone, s->calendar);
}

/* equals */
static JSValue js_zoneddatetime_equals(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)argc;
    JSZonedDateTimeData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = js_zoneddatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_zoneddatetime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    if (s->ts.secs != other->ts.secs || s->ts.nsecs != other->ts.nsecs ||
        !ti_timezones_are_equal(s->timeZone, other->timeZone) || s->calendar != other->calendar) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    JS_FreeValue(ctx, other_val);
    return JS_NewBool(ctx, 1);
}

/* toString - minimal ISO format */
static JSValue js_zoneddatetime_toString(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSZonedDateTimeData *s;
    char buf[256];
    int pos = 0;
    int64_t year, local_sec;
    int month, day, hour, minute, second;
    int frac_ms, frac_us, frac_ns;
    int64_t offset_ns = 0;
    int offset_sec;
    int calendarName = 0; /* 0=auto (no annotation for ISO), 1=always, 2=critical */
    int timeZoneName = 0; /* 0=auto, 1=never, 2=critical */
    int offset_opt = 0;   /* 0=auto, 1=never */
    int precision_type = -1; /* -1=auto, 0=second, 1-9=digits, 10=minute, 11=second, 12=ms, 13=us, 14=ns */
    int fsd_is_auto = 1;
    int fsd_value = 0;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int has_smallestUnit = 0;
    TIRoundingMode rounding_mode = ROUND_TRUNC;
    if (argc >= 1 && ti_validate_options_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    /* Parse options first */
    if (argc >= 1 && JS_IsObject(argv[0])) {
        /* Step 1: calendarName */
        if (ti_get_calendar_name_option(ctx, argv[0], &calendarName) < 0)
            return JS_EXCEPTION;
        /* Step 2: fractionalSecondDigits */
        {
            int fsd = ti_get_fractional_second_digits_option(ctx, argv[0], &fsd_value);
            if (fsd < 0)
                return JS_EXCEPTION;
            if (fsd_value != -1)
                fsd_is_auto = 0;
        }
        /* Step 3: offset */
        {
            JSValue v = JS_GetPropertyStr(ctx, argv[0], "offset");
            if (!JS_IsUndefined(v)) {
                size_t len;
                const char *str = JS_ToCStringLen(ctx, &len, v);
                if (!str) {
                    JS_FreeValue(ctx, v);
                    return JS_EXCEPTION;
                }
                if (len == strlen(str) && !strcmp(str, "never")) {
                    offset_opt = 1;
                } else if (len == strlen(str) && !strcmp(str, "auto")) {
                    offset_opt = 0;
                } else {
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, v);
                    return JS_ThrowRangeError(ctx, "invalid offset");
                }
                JS_FreeCString(ctx, str);
            }
            JS_FreeValue(ctx, v);
        }
        /* Step 4: roundingMode */
        {
            TIRoundingMode mode;
            if (ti_get_rounding_mode_option_default(ctx, argv[0], &mode, ROUND_TRUNC) < 0)
                return JS_EXCEPTION;
            rounding_mode = mode;
        }
        /* Step 5: smallestUnit */
        {
            TIUnit unit;
            if (ti_parse_unit_option(ctx, argv[0], "smallestUnit", &unit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0)
                return JS_EXCEPTION;
            if (unit != TI_UNIT_INVALID) {
                smallestUnit = unit;
                has_smallestUnit = 1;
            }
        }
        /* Step 6: timeZoneName */
        {
            JSValue v = JS_GetPropertyStr(ctx, argv[0], "timeZoneName");
            if (!JS_IsUndefined(v)) {
                size_t len;
                const char *str = JS_ToCStringLen(ctx, &len, v);
                if (!str) {
                    JS_FreeValue(ctx, v);
                    return JS_EXCEPTION;
                }
                if (len == strlen(str) && !strcmp(str, "never")) {
                    timeZoneName = 1;
                } else if (len == strlen(str) && !strcmp(str, "auto")) {
                    timeZoneName = 0;
                } else if (len == strlen(str) && !strcmp(str, "critical")) {
                    timeZoneName = 2;
                } else {
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, v);
                    return JS_ThrowRangeError(ctx, "invalid timeZoneName");
                }
                JS_FreeCString(ctx, str);
            }
            JS_FreeValue(ctx, v);
        }
    }
    /* Validation */
    if (has_smallestUnit && (smallestUnit == TI_UNIT_INVALID || smallestUnit < TI_UNIT_MINUTE || smallestUnit > TI_UNIT_NANOSECOND))
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");
    /* Determine precision */
    if (has_smallestUnit) {
        precision_type = ti_precision_from_smallest_unit(smallestUnit);
    } else if (!fsd_is_auto) {
        precision_type = fsd_value;
    } else {
        precision_type = -1; /* auto */
    }
    /* Apply rounding to UTC instant, then compute local time */
    {
        js_int128_t total_ns = ti_timespec_to_ns(&s->ts);
        js_int128_t increment_ns = 1;
        int64_t rounded_sec;
        int rounded_nsec;
        if (precision_type == 10)
            increment_ns = 60000000000LL;
        else if (precision_type == 11)
            increment_ns = 1000000000LL;
        else if (precision_type == 12)
            increment_ns = 1000000LL;
        else if (precision_type == 13)
            increment_ns = 1000LL;
        else if (precision_type == 14)
            increment_ns = 1LL;
        else if (precision_type == 0)
            increment_ns = 1000000000LL;
        else if (precision_type >= 1 && precision_type <= 9)
            increment_ns = (js_int128_t)pow(10, 9 - precision_type);
        if (increment_ns > 1 || precision_type == 0) {
            total_ns = ti_round_to_increment_as_if_positive128(total_ns, increment_ns, rounding_mode);
        }
        rounded_sec = (int64_t)(total_ns / 1000000000LL);
        rounded_nsec = (int)(total_ns % 1000000000LL);
        if (rounded_nsec < 0) {
            rounded_nsec += 1000000000;
            rounded_sec--;
        }
        /* Compute offset at rounded UTC time */
        {
            int tz_id = ti_tz_find_id(s->timeZone);
            if (tz_id >= 0) {
                offset_sec = ti_tz_offset_at_sec(tz_id, rounded_sec);
            } else {
                int ret = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
                if (ret != 0)
                    return JS_ThrowRangeError(ctx, "unsupported time zone");
                offset_sec = (int)(offset_ns / 1000000000LL);
            }
        }
        local_sec = rounded_sec + offset_sec;
        /* Handle nanosecond carry */
        {
            int64_t nsec = rounded_nsec;
            if (nsec < 0) {
                nsec += 1000000000LL;
                local_sec--;
            } else if (nsec >= 1000000000LL) {
                nsec -= 1000000000LL;
                local_sec++;
            }
            frac_ms = (int)(nsec / 1000000);
            frac_us = (int)((nsec / 1000) % 1000);
            frac_ns = (int)(nsec % 1000);
        }
        ti_epoch_sec_to_date(local_sec, &year, &month, &day, &hour, &minute, &second);
    }
    /* Format date */
    if (year < 0)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "-%06d-%02d-%02d",
                        (int)(-year), month, day);
    else if (year >= 10000)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "+%06d-%02d-%02d",
                        (int)year, month, day);
    else
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%04d-%02d-%02d",
                        (int)year, month, day);
    /* Format time */
    if (precision_type == 10)
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "T%02d:%02d",
                        hour, minute);
    else
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "T%02d:%02d:%02d",
                        hour, minute, second);
    /* Format fractional seconds */
    if (precision_type == 10) {
        /* minute precision: truncate seconds */
    } else if (precision_type == 0) {
        /* second precision: no fractions */
    } else if (precision_type >= 1 && precision_type <= 9) {
        char frac[16];
        (void)snprintf(frac, sizeof(frac), "%09d",
                       frac_ms * 1000000 + frac_us * 1000 + frac_ns);
        frac[precision_type] = '\0';
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ".%s", frac);
    } else if (precision_type == -1 && (frac_ms != 0 || frac_us != 0 || frac_ns != 0)) {
        char frac[16];
        int frac_len = snprintf(frac, sizeof(frac), "%03d%03d%03d", frac_ms, frac_us, frac_ns);
        while (frac_len > 0 && frac[frac_len - 1] == '0')
            frac_len--;
        frac[frac_len] = '\0';
        if (frac_len > 0)
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ".%s", frac);
    } else if (precision_type >= 11) {
        int digits = 0;
        if (precision_type == 11) digits = 0;
        else if (precision_type == 12) digits = 3;
        else if (precision_type == 13) digits = 6;
        else if (precision_type == 14) digits = 9;
        if (digits > 0) {
            char fmt[8];
            snprintf(fmt, sizeof(fmt), "%%0%dd", digits);
            int frac_val = 0;
            if (digits == 3) frac_val = frac_ms;
            else if (digits == 6) frac_val = frac_ms * 1000 + frac_us;
            else if (digits == 9) frac_val = frac_ms * 1000000 + frac_us * 1000 + frac_ns;
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, ".");
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, fmt, frac_val);
        }
    }
    /* Format timezone offset */
    if (offset_opt == 0) {
        int offset_min = offset_sec / 60;
        /* Round to nearest minute (half-expand away from zero) */
        {
            int rem = offset_sec % 60;
            if (offset_sec >= 0) {
                if (rem >= 30) offset_min++;
            } else {
                if (rem <= -30) offset_min--;
            }
        }
        int offset_abs = offset_min >= 0 ? offset_min : -offset_min;
        int offset_h = offset_abs / 60;
        int offset_m = offset_abs % 60;
        pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "%c%02d:%02d",
                        offset_min >= 0 ? '+' : '-', offset_h, offset_m);
    }
    /* Format timezone name */
    if (timeZoneName != 1) {
        if (timeZoneName == 2)
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "[!%s]", s->timeZone);
        else
            pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "[%s]", s->timeZone);
    }
    /* Calendar annotation */
    if (calendarName == 1 || calendarName == 2 || (calendarName == 0 && s->calendar != CAL_ISO8601)) {
        const char *cal_str = ti_calendar_enum_to_string(s->calendar);
        if (cal_str) {
            if (calendarName == 2)
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "[!u-ca=%s]", cal_str);
            else
                pos += snprintf(buf + pos, sizeof(buf) - (size_t)pos, "[u-ca=%s]", cal_str);
        }
    }
    return JS_NewString(ctx, buf);
}

static JSValue js_zoneddatetime_toJSON(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_zoneddatetime_toString(ctx, this_val, 0, NULL);
}

static JSValue js_zoneddatetime_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSValue ctor, dtf, method, ret;
    JSValue new_argv[2];
    JSValue date_val;
    if (!s)
        return JS_EXCEPTION;

    /* timeZone option is not allowed in toLocaleString for ZonedDateTime */
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSAtom tz_atom = JS_NewAtom(ctx, "timeZone");
        int has_tz = JS_HasProperty(ctx, argv[1], tz_atom);
        JS_FreeAtom(ctx, tz_atom);
        if (has_tz > 0)
            return JS_ThrowTypeError(ctx, "timeZone option disallowed");
        if (has_tz < 0)
            return JS_EXCEPTION;
    }

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
    if (JS_IsNull(ctor) || JS_IsUndefined(ctor)) {
        JS_FreeValue(ctx, ctor);
        return js_zoneddatetime_toString(ctx, this_val, 0, NULL);
    }

    new_argv[0] = argc >= 1 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
    if (argc >= 2 && JS_IsObject(argv[1])) {
        new_argv[1] = JS_DupValue(ctx, argv[1]);
    } else if (argc >= 2 && JS_IsUndefined(argv[1])) {
        new_argv[1] = JS_NewObject(ctx);
    } else if (argc >= 2) {
        /* Non-object, non-undefined options - let DateTimeFormat constructor throw */
        new_argv[1] = JS_DupValue(ctx, argv[1]);
    } else {
        new_argv[1] = JS_NewObject(ctx);
    }
    if (JS_IsException(new_argv[1])) {
        JS_FreeValue(ctx, new_argv[0]);
        JS_FreeValue(ctx, ctor);
        return new_argv[1];
    }

    if (JS_IsObject(new_argv[1])) {
        const char *canon_tz = ti_canonicalize_timezone_alias(s->timeZone);
        JSValue tz_val = JS_NewString(ctx, canon_tz);
        int has_date, has_time;
        if (JS_IsException(tz_val)) {
            JS_FreeValue(ctx, new_argv[0]);
            JS_FreeValue(ctx, new_argv[1]);
            JS_FreeValue(ctx, ctor);
            return tz_val;
        }
        JS_SetPropertyStr(ctx, new_argv[1], "timeZone", tz_val);
        {
            JSValue ds_val = JS_GetPropertyStr(ctx, new_argv[1], "dateStyle");
            if (JS_IsException(ds_val)) {
                JS_FreeValue(ctx, ds_val);
                JS_FreeValue(ctx, new_argv[0]);
                JS_FreeValue(ctx, new_argv[1]);
                JS_FreeValue(ctx, ctor);
                return JS_EXCEPTION;
            }
            JSValue ts_val = JS_GetPropertyStr(ctx, new_argv[1], "timeStyle");
            if (JS_IsException(ts_val)) {
                JS_FreeValue(ctx, ds_val);
                JS_FreeValue(ctx, ts_val);
                JS_FreeValue(ctx, new_argv[0]);
                JS_FreeValue(ctx, new_argv[1]);
                JS_FreeValue(ctx, ctor);
                return JS_EXCEPTION;
            }
            BOOL has_ds = !JS_IsUndefined(ds_val);
            BOOL has_ts = !JS_IsUndefined(ts_val);
            JS_FreeValue(ctx, ds_val);
            JS_FreeValue(ctx, ts_val);
            has_date = js_intl_options_has_date_fields(ctx, new_argv[1]);
            has_time = js_intl_options_has_time_fields(ctx, new_argv[1]);
            if (has_date < 0 || has_time < 0) {
                JS_FreeValue(ctx, new_argv[0]);
                JS_FreeValue(ctx, new_argv[1]);
                JS_FreeValue(ctx, ctor);
                return JS_EXCEPTION;
            }
            if (!has_ds && !has_ts && !has_date && !has_time) {
                JS_DefinePropertyValueStr(ctx, new_argv[1], "year",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "month",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "day",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "hour",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "minute",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                JS_DefinePropertyValueStr(ctx, new_argv[1], "second",
                                           JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
            }
            if (!has_ds && !has_ts) {
                JSAtom tzn_atom = JS_NewAtom(ctx, "timeZoneName");
                int has_tzn = JS_HasProperty(ctx, new_argv[1], tzn_atom);
                JS_FreeAtom(ctx, tzn_atom);
                if (has_tzn <= 0) {
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "timeZoneName",
                                               JS_NewString(ctx, "short"), JS_PROP_C_W_E);
                }
            }
        }
    }

    dtf = JS_CallConstructor(ctx, ctor, 2, new_argv);
    JS_FreeValue(ctx, ctor);
    JS_FreeValue(ctx, new_argv[0]);
    JS_FreeValue(ctx, new_argv[1]);
    if (JS_IsException(dtf))
        return dtf;

    /* Calendar mismatch check */
    {
        JSValue resolved = JS_GetPropertyStr(ctx, dtf, "resolvedOptions");
        if (!JS_IsException(resolved)) {
            JSValue opts = JS_Call(ctx, resolved, dtf, 0, NULL);
            JS_FreeValue(ctx, resolved);
            if (!JS_IsException(opts)) {
                JSValue dtf_cal_val = JS_GetPropertyStr(ctx, opts, "calendar");
                JSValue obj_cal_val = JS_GetPropertyStr(ctx, this_val, "calendarId");
                JS_FreeValue(ctx, opts);
                if (!JS_IsException(dtf_cal_val) && !JS_IsException(obj_cal_val)) {
                    const char *dtf_cal = JS_ToCString(ctx, dtf_cal_val);
                    const char *obj_cal = JS_ToCString(ctx, obj_cal_val);
                    if (dtf_cal && obj_cal && strcmp(obj_cal, "iso8601") != 0 &&
                        strcmp(dtf_cal, "iso8601") != 0 && strcmp(obj_cal, dtf_cal) != 0) {
                        JS_FreeCString(ctx, dtf_cal);
                        JS_FreeCString(ctx, obj_cal);
                        JS_FreeValue(ctx, dtf_cal_val);
                        JS_FreeValue(ctx, obj_cal_val);
                        JS_FreeValue(ctx, dtf);
                        return JS_ThrowRangeError(ctx, "calendar mismatch");
                    }
                    JS_FreeCString(ctx, dtf_cal);
                    JS_FreeCString(ctx, obj_cal);
                }
                JS_FreeValue(ctx, dtf_cal_val);
                JS_FreeValue(ctx, obj_cal_val);
            }
        }
    }

    /* Format as epoch milliseconds to avoid dtf_check_temporal_overlap rejection */
    date_val = JS_NewFloat64(ctx, timespec_to_ms(&s->ts));
    if (JS_IsException(date_val)) {
        JS_FreeValue(ctx, dtf);
        return date_val;
    }

    method = JS_GetPropertyStr(ctx, dtf, "format");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, dtf);
        JS_FreeValue(ctx, date_val);
        return method;
    }

    ret = JS_Call(ctx, method, dtf, 1, &date_val);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, dtf);
    JS_FreeValue(ctx, date_val);
    return ret;
}

static JSValue js_zoneddatetime_valueOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.ZonedDateTime with < or >");
}

/* toInstant */
static JSValue js_zoneddatetime_toInstant(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return js_create_instant(ctx, &s->ts);
}

static JSValue js_zoneddatetime_add_subtract(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv, int is_add)
{
    JSZonedDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    JSDurationData d = {0};
    int64_t year64;
    int year;
    int month, day, hour, minute, second, ms, us, ns;
    int overflow_constrain = 1;
    JSTimeSpec ts;
    int sign = is_add ? 1 : -1;
    if (!s)
        return JS_EXCEPTION;
    if (js_to_temporal_duration(ctx, argv[0], &d))
        return JS_EXCEPTION;
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
    }
    /* Validate duration fields fit in int before casting */
    if ((int64_t)(int)d.years != d.years ||
        (int64_t)(int)d.months != d.months ||
        (int64_t)(int)d.weeks != d.weeks ||
        (int64_t)(int)d.days != d.days)
        return JS_ThrowRangeError(ctx, "duration out of range");
    /* Time-only path: exact instant arithmetic (AddInstant) per spec */
    if (d.years == 0 && d.months == 0 && d.weeks == 0 && d.days == 0) {
        js_int128_t total_ns = ti_timespec_to_ns(&s->ts);
        total_ns += sign * ti_duration_time_fields_to_ns(&d);
        ti_ns_to_timespec(total_ns, &ts);
    } else {
        /* Date+time path per spec AddZonedDateTime */
        /* Get local ISO date/time from the UTC instant */
        {
            int64_t local_sec = s->ts.secs;
            int local_nsec = (int)s->ts.nsecs;
            zdt_apply_timezone_offset(ctx, s, &local_sec, &local_nsec);
            ti_epoch_sec_to_date(local_sec, &year64, &month, &day, &hour, &minute, &second);
            ms = (int)(local_nsec / 1000000);
            us = (int)((local_nsec / 1000) % 1000);
            ns = (int)(local_nsec % 1000);
        }
        year = (int)year64;
        /* Add date part in calendar space */
        {
            int cal_y = year, cal_m = month, cal_d = day;
            int orig_mc = 0, orig_has_l = 0;
            ti_iso_to_calendar_date(s->calendar, year, month, day, &cal_y, &cal_m, &cal_d);
            year = cal_y; month = cal_m; day = cal_d;
            ti_calendar_month_to_month_code(s->calendar, year, month, &orig_mc, &orig_has_l);
            year += sign * (int)d.years;
            month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, orig_has_l);
            if (month < 0) {
                if (!overflow_constrain)
                    return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, 0);
            } else if (!overflow_constrain) {
                int result_mc, result_has_l;
                ti_calendar_month_to_month_code(s->calendar, year, month, &result_mc, &result_has_l);
                if (result_mc != orig_mc || result_has_l != orig_has_l)
                    return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
            }
            month += sign * (int)d.months;
            pdt_balance_month_calendar(&year, &month, s->calendar);
            {
                int dim = ti_calendar_days_in_month(s->calendar, year, month);
                if (day > dim) {
                    if (!overflow_constrain)
                        return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                    day = dim;
                }
            }
            day += sign * (int)(d.weeks * 7 + d.days);
            pdt_balance_date_calendar(&year, &month, &day, s->calendar);
            {
                int iso_y, iso_m, iso_d;
                ti_calendar_date_to_iso(s->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
                year = iso_y; month = iso_m; day = iso_d;
            }
        }
        /* Combine new date with original local time */
        {
            int64_t days = pdt_to_ordinal_days(year, month, day);
            ts.secs = (days - 719162) * 86400LL + hour * 3600LL + minute * 60LL + second;
            ts.nsecs = (uint32_t)(ms * 1000000 + us * 1000 + ns);
        }
        /* Convert local back to UTC with compatible disambiguation */
        {
            int tz_id = ti_tz_find_id(s->timeZone);
            if (tz_id >= 0) {
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, ts.secs, 2, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                ts.secs = utc_sec;
            } else {
                int64_t offset_ns = 0;
                int ret2 = ti_parse_temporal_timezone_string(s->timeZone, &offset_ns);
                if (ret2 == 0) {
                    int64_t offset_sec = offset_ns / 1000000000LL;
                    int offset_nsec = (int)(offset_ns % 1000000000LL);
                    ts.secs -= offset_sec;
                    {
                        int new_nsec = (int)ts.nsecs - offset_nsec;
                        if (new_nsec < 0) {
                            new_nsec += 1000000000;
                            ts.secs -= 1;
                        } else if (new_nsec >= 1000000000) {
                            new_nsec -= 1000000000;
                            ts.secs += 1;
                        }
                        ts.nsecs = (uint32_t)new_nsec;
                    }
                }
            }
        }
        /* Add time part using exact instant arithmetic (AddInstant) */
        {
            js_int128_t total_ns = ti_timespec_to_ns(&ts);
            total_ns += sign * ti_duration_time_fields_to_ns(&d);
            ti_ns_to_timespec(total_ns, &ts);
        }
    }
    {
        js_int128_t epoch_ns = ti_timespec_to_ns(&ts);
        js_int128_t limit = (js_int128_t)8640000000000LL * 1000000000LL;
        if (epoch_ns < -limit || epoch_ns > limit)
            return JS_ThrowRangeError(ctx, "epoch nanoseconds outside valid limits");
    }
    return js_create_zoneddatetime(ctx, &ts, s->timeZone, s->calendar);
}

JSValue js_zoneddatetime_add(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    return js_zoneddatetime_add_subtract(ctx, this_val, argc, argv, 1);
}

/* toPlainDateTime */
static JSValue js_zoneddatetime_toPlainDateTime(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSZonedDateTimeData *s;
    int64_t year;
    int month, day, hour, minute, second;
    int ms, us, ns;
    s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        int64_t epoch_sec = s->ts.secs;
        int epoch_nsec = (int)s->ts.nsecs;
        zdt_apply_timezone_offset(ctx, s, &epoch_sec, &epoch_nsec);
        ti_epoch_sec_to_date(epoch_sec, &year, &month, &day, &hour, &minute, &second);
        ms = epoch_nsec / 1000000;
        us = (epoch_nsec % 1000000) / 1000;
        ns = epoch_nsec % 1000;
    }
    return js_create_plaindatetime_with_calendar(ctx, (int)year, month, day, hour, minute, second, ms, us, ns, s->calendar);
}

/* toPlainDate */
static JSValue js_zoneddatetime_toPlainDate(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSZonedDateTimeData *s;
    int64_t year;
    int month, day, hour, minute, second;
    s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        int64_t epoch_sec = s->ts.secs;
        int epoch_nsec = (int)s->ts.nsecs;
        zdt_apply_timezone_offset(ctx, s, &epoch_sec, &epoch_nsec);
        ti_epoch_sec_to_date(epoch_sec, &year, &month, &day, &hour, &minute, &second);
    }
    return js_create_plaindate_with_calendar(ctx, (int)year, month, day, s->calendar);
}

/* toPlainTime */
static JSValue js_zoneddatetime_toPlainTime(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSZonedDateTimeData *s;
    int64_t year;
    int month, day, hour, minute, second;
    int ms, us, ns;
    s = JS_GetOpaque2(ctx, this_val, js_zoneddatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    {
        int64_t epoch_sec = s->ts.secs;
        int epoch_nsec = (int)s->ts.nsecs;
        zdt_apply_timezone_offset(ctx, s, &epoch_sec, &epoch_nsec);
        ti_epoch_sec_to_date(epoch_sec, &year, &month, &day, &hour, &minute, &second);
        ms = epoch_nsec / 1000000;
        us = (epoch_nsec % 1000000) / 1000;
        ns = epoch_nsec % 1000;
    }
    {
        JSPlainTimeData t = {hour, minute, second, ms, us, ns};
        return js_create_plaintime(ctx, &t);
    }
}

/* Temporal.ZonedDateTime.from */
JSValue js_zoneddatetime_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    if (JS_IsObject(argv[0])) {
        JSZonedDateTimeData *s = JS_GetOpaque(argv[0], js_zoneddatetime_class_id);
        if (s) {
            int overflow_dummy = 1;
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2) {
                static const char *dis_values[] = { "earlier", "later", "compatible", "reject" };
                if (ti_get_string_option(ctx, argv[1], "disambiguation", dis_values, 4, 2) < 0)
                    return JS_EXCEPTION;
                static const char *offset_values[] = { "use", "ignore", "prefer", "reject" };
                if (ti_get_string_option(ctx, argv[1], "offset", offset_values, 4, 3) < 0)
                    return JS_EXCEPTION;
            }
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_dummy) < 0)
                return JS_EXCEPTION;
            return js_create_zoneddatetime(ctx, &s->ts, s->timeZone, s->calendar);
        }
        /* Property bag */
        {
            TIPreparedFields fields;
            int year = 0, month = 0, day = 0;
            int month_from_code = 0;
            int mc_has_l = 0;
            int hour = 0, minute = 0, second = 0, ms = 0, us = 0, ns = 0;
            int has_year = 0, has_month = 0, has_month_code = 0, has_day = 0;
            int has_time_zone = 0;
            int has_offset_prop = 0;
            int64_t prop_offset_ns = 0;
            int is_iso_calendar = 1;
            char timeZone[TI_TIMEZONE_BUF_SIZE] = "UTC";
            CalendarType calendar = CAL_ISO8601;
            JSTimeSpec ts;

            if (ti_resolve_calendar_from_object(ctx, argv[0], &calendar) < 0)
                return JS_EXCEPTION;
            if (calendar != CAL_ISO8601 && calendar != CAL_GREGORY)
                is_iso_calendar = 0;

            if (ti_prepare_calendar_fields(ctx, argv[0],
                                            TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                            TI_FIELD_YEAR | TI_FIELD_HOUR | TI_FIELD_MINUTE |
                                            TI_FIELD_SECOND | TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND |
                                            TI_FIELD_NANOSECOND | TI_FIELD_OFFSET | TI_FIELD_TIMEZONE,
                                            0, 0, &fields, calendar) < 0)
                return JS_EXCEPTION;

            /* Merge fields */
            if (fields.present_mask & TI_FIELD_DAY) {
                day = fields.day;
                has_day = 1;
            }
            if (fields.present_mask & TI_FIELD_HOUR) hour = fields.hour;
            if (fields.present_mask & TI_FIELD_MICROSECOND) us = fields.microsecond;
            if (fields.present_mask & TI_FIELD_MILLISECOND) ms = fields.millisecond;
            if (fields.present_mask & TI_FIELD_MINUTE) minute = fields.minute;
            if (fields.present_mask & TI_FIELD_MONTH) {
                month = fields.month;
                has_month = 1;
            }
            if (fields.present_mask & TI_FIELD_MONTH_CODE) {
                month_from_code = fields.month_from_code;
                mc_has_l = fields.month_code_has_l;
                has_month_code = 1;
            }
            if (fields.present_mask & TI_FIELD_NANOSECOND) ns = fields.nanosecond;
            if (fields.present_mask & TI_FIELD_OFFSET) {
                prop_offset_ns = fields.offset_ns;
                has_offset_prop = 1;
            }
            if (fields.present_mask & TI_FIELD_SECOND) second = fields.second;
            if (fields.present_mask & TI_FIELD_YEAR) {
                year = (int)fields.year;
                has_year = 1;
            }

            /* Handle timezone */
            if (fields.present_mask & TI_FIELD_TIMEZONE) {
                if (fields.time_zone[0]) {
                    const char *tz = fields.time_zone;
                    const char *bracket_open = strchr(tz, '[');
                    if (bracket_open) {
                        const char *bracket_close = strchr(bracket_open, ']');
                        if (bracket_close && bracket_close[1] == '\0') {
                            int inner_len = (int)(bracket_close - bracket_open - 1);
                            if (inner_len > 0 && inner_len < (int)sizeof(timeZone)) {
                                memcpy(timeZone, bracket_open + 1, (size_t)inner_len);
                                timeZone[inner_len] = '\0';
                                ;
                            }
                        }
                    } else {
                        size_t tz_len = strlen(tz);
                        int is_utc = (tz_len == 3 && (tz[0] == 'U' || tz[0] == 'u') &&
                                      (tz[1] == 'T' || tz[1] == 't') &&
                                      (tz[2] == 'C' || tz[2] == 'c'));
                        if (!is_utc && tz_len > 0 && tz[tz_len - 1] == 'Z')
                            is_utc = 1;
                        {
                            int64_t offset_ns = 0;
                            int ret = ti_parse_temporal_timezone_string(tz, &offset_ns);
                            if (ret == 0) {
                                if (ti_tz_find_id(tz) >= 0) {
                                    /* Known IANA timezone name - preserve it */
                                    pstrcpy(timeZone, sizeof(timeZone), tz);
                                } else {
                                    ti_format_canonical_timezone(offset_ns, is_utc, timeZone, sizeof(timeZone));
                                }
                            }
                        }
                    }
                    /* Canonicalize UTC */
                    if (timeZone[0] == 'u' || timeZone[0] == 'U') {
                        if ((timeZone[1] == 't' || timeZone[1] == 'T') &&
                            (timeZone[2] == 'c' || timeZone[2] == 'C') && timeZone[3] == '\0')
                            pstrcpy(timeZone, sizeof(timeZone), "UTC");
                    }
                    has_time_zone = 1;
                } else {
                    JSValue tz_v = JS_GetPropertyStr(ctx, argv[0], "timeZone");
                    if (JS_IsException(tz_v)) {
                        JS_FreeValue(ctx, tz_v);
                        return JS_EXCEPTION;
                    }
                    if (!JS_IsUndefined(tz_v)) {
                        JSZonedDateTimeData *zdt = JS_GetOpaque(tz_v, js_zoneddatetime_class_id);
                        if (zdt) {
                            pstrcpy(timeZone, sizeof(timeZone), zdt->timeZone);
                            has_time_zone = 1;
                        }
                        JS_FreeValue(ctx, tz_v);
                    }
                }
            }

            /* era/eraYear handling */
            if (ti_calendar_requires_era(calendar) && (fields.present_mask & (TI_FIELD_ERA | TI_FIELD_ERA_YEAR))) {
                if (!(fields.present_mask & TI_FIELD_ERA) || !(fields.present_mask & TI_FIELD_ERA_YEAR))
                    return JS_ThrowTypeError(ctx, "era and eraYear must both be present");
                {
                    int cal_year_from_era;
                    if (fields.era == TI_ERA_UNKNOWN || ti_calendar_era_to_calendar_year(calendar, fields.era, (int)fields.era_year, &cal_year_from_era) < 0)
                        return JS_ThrowRangeError(ctx, "invalid era");
                    if (has_year) {
                        if (year != cal_year_from_era)
                            return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
                    } else {
                        year = cal_year_from_era;
                        has_year = 1;
                    }
                }
            }

            if (!has_time_zone)
                return JS_ThrowTypeError(ctx, "missing timeZone");
            {
                int overflow_constrain = 1;
                int offset_idx = 3; /* default reject */
                /* Validate options before algorithmic validation */
                if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                    return JS_EXCEPTION;
                int dis_idx = 2; /* default compatible */
                if (argc >= 2) {
                    static const char *dis_values[] = { "earlier", "later", "compatible", "reject" };
                    dis_idx = ti_get_string_option(ctx, argv[1], "disambiguation", dis_values, 4, 2);
                    if (dis_idx < 0)
                        return JS_EXCEPTION;
                    static const char *offset_values[] = { "use", "ignore", "prefer", "reject" };
                    offset_idx = ti_get_string_option(ctx, argv[1], "offset", offset_values, 4, 3);
                    if (offset_idx < 0)
                        return JS_EXCEPTION;
                }
                if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                    return JS_EXCEPTION;
                if (!has_year || !has_day || (!has_month && !has_month_code))
                    return JS_ThrowTypeError(ctx, "missing required property");
                {
                    int resolved_month = 0;
                    if (has_month_code)
                        resolved_month = ti_calendar_month_code_to_month(calendar, year, month_from_code, mc_has_l);
                    if (has_month && has_month_code && month != resolved_month)
                        return JS_ThrowRangeError(ctx, "monthCode and month conflict");
                    if (has_month_code && !has_month)
                        month = resolved_month;
                }
                /* Validate monthCode suitability */
                if (has_month_code) {
                    if (is_iso_calendar) {
                        if (month < 1 || month > 12 || mc_has_l)
                            return JS_ThrowRangeError(ctx, "invalid monthCode");
                    } else {
                        if (!ti_calendar_validate_month_code(calendar, month_from_code, mc_has_l))
                            return JS_ThrowRangeError(ctx, "invalid monthCode");
                    }
                }
                if (day < 1)
                    return JS_ThrowRangeError(ctx, "invalid day");
                if (month < 1)
                    return JS_ThrowRangeError(ctx, "invalid month");
                if (hour < 0 || minute < 0 || second < 0 ||
                    ms < 0 || us < 0 || ns < 0)
                    return JS_ThrowRangeError(ctx, "invalid time");
                /* Apply overflow handling for month and day in calendar space */
                if (calendar != CAL_ISO8601 && calendar != CAL_GREGORY) {
                    int max_month = ti_calendar_months_in_year(calendar, year);
                    if (month > max_month) {
                        if (!overflow_constrain)
                            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                        month = max_month;
                    }
                    {
                        int dim = ti_calendar_days_in_month(calendar, year, month);
                        if (day > dim) {
                            if (!overflow_constrain)
                                return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                            day = dim;
                        }
                    }
                    {
                        int iso_year, iso_month, iso_day;
                        ti_calendar_date_to_iso(calendar, year, month, day, &iso_year, &iso_month, &iso_day);
                        year = iso_year; month = iso_month; day = iso_day;
                    }
                } else {
                    int dim;
                    if (month > 12) {
                        if (!overflow_constrain)
                            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                        month = 12;
                    }
                    dim = pd_days_in_month(year, month);
                    if (day > dim) {
                        if (!overflow_constrain)
                            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime");
                        day = dim;
                    }
                }
                int64_t days = pdt_to_ordinal_days(year, month, day);
                ts.secs = (days - 719162) * 86400LL + hour * 3600LL + minute * 60LL + second;
                ts.nsecs = (uint32_t)(ms * 1000000 + us * 1000 + ns);
                /* Adjust for timezone offset: UTC = local - offset */
                {
                    int64_t computed_offset_ns = 0;
                    int64_t local_sec = ts.secs;
                    int tz_id = ti_tz_find_id(timeZone);
                    if (tz_id >= 0) {
                        int64_t utc_sec;
                        int offset_sec;
                        if (offset_idx == 2 && has_offset_prop) {
                            /* prefer: check both candidates for matching offset */
                            int64_t cand1_utc, cand2_utc;
                            int cand1_off, cand2_off;
                            if (ti_tz_local_to_utc(tz_id, local_sec, 0, &cand1_utc, &cand1_off) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                            if (ti_tz_local_to_utc(tz_id, local_sec, 1, &cand2_utc, &cand2_off) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                            {
                                int64_t cand1_off_ns = (int64_t)cand1_off * 1000000000LL;
                                int64_t cand2_off_ns = (int64_t)cand2_off * 1000000000LL;
                                if (prop_offset_ns == cand1_off_ns) {
                                    utc_sec = cand1_utc;
                                    offset_sec = cand1_off;
                                } else if (prop_offset_ns == cand2_off_ns) {
                                    utc_sec = cand2_utc;
                                    offset_sec = cand2_off;
                                } else {
                                    if (ti_tz_local_to_utc(tz_id, local_sec, dis_idx, &utc_sec, &offset_sec) < 0)
                                        return JS_ThrowRangeError(ctx, "multiple instants found");
                                }
                            }
                        } else {
                            if (ti_tz_local_to_utc(tz_id, local_sec, dis_idx, &utc_sec, &offset_sec) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                        }
                        ts.secs = utc_sec;
                        computed_offset_ns = (int64_t)offset_sec * 1000000000LL;
                    } else {
                        int64_t offset_ns = 0;
                        int ret2 = ti_parse_temporal_timezone_string(timeZone, &offset_ns);
                        if (ret2 == 0) {
                            int64_t offset_sec = offset_ns / 1000000000LL;
                            int offset_nsec = (int)(offset_ns % 1000000000LL);
                            ts.secs -= offset_sec;
                            {
                                int new_nsec = (int)ts.nsecs - offset_nsec;
                                if (new_nsec < 0) {
                                    new_nsec += 1000000000;
                                    ts.secs -= 1;
                                } else if (new_nsec >= 1000000000) {
                                    new_nsec -= 1000000000;
                                    ts.secs += 1;
                                }
                                ts.nsecs = (uint32_t)new_nsec;
                            }
                            computed_offset_ns = offset_ns;
                        }
                    }
                    if (has_offset_prop) {
                        if (offset_idx == 0) { /* use */
                            /* Use the property bag offset, overriding loop result */
                            int64_t offset_sec = prop_offset_ns / 1000000000LL;
                            int offset_nsec = (int)(prop_offset_ns % 1000000000LL);
                            ts.secs = local_sec - offset_sec;
                            {
                                int new_nsec = (int)(ms * 1000000 + us * 1000 + ns) - offset_nsec;
                                if (new_nsec < 0) {
                                    new_nsec += 1000000000;
                                    ts.secs -= 1;
                                } else if (new_nsec >= 1000000000) {
                                    new_nsec -= 1000000000;
                                    ts.secs += 1;
                                }
                                ts.nsecs = (uint32_t)new_nsec;
                            }
                        } else if (offset_idx == 3) { /* reject */
                            if (computed_offset_ns != prop_offset_ns)
                                return JS_ThrowRangeError(ctx, "offset does not match time zone");
                        }
                    }
                }
                return js_create_zoneddatetime(ctx, &ts, timeZone, calendar);
            }
        }
    }
    if (JS_IsString(argv[0])) {
        /* Parse ISO string using shared parser */
        const char *str = JS_ToCString(ctx, argv[0]);
        TISODateTimeParseResult r;
        char timeZone[TI_TIMEZONE_BUF_SIZE] = "UTC";
        JSTimeSpec ts;
        int overflow_constrain = 1;
        int offset_idx = 3; /* default reject */
        int64_t tz_offset_ns = 0;
        int has_computed_utc = 0;
        if (!str)
            return JS_EXCEPTION;
        if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
        }
        JS_FreeCString(ctx, str);
        /* ZonedDateTime strings must have a bracket timezone annotation */
        if (!r.has_tz_annotation)
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
        /* ZonedDateTime must have date */
        if (!r.has_date)
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
        /* UTC designator or prefix offset requires time component */
        if ((r.has_utc || r.has_tz_offset) && !r.has_time)
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
        pstrcpy(timeZone, sizeof(timeZone), r.tz_annotation);
        /* Canonicalize UTC */
        if (timeZone[0] == 'u' || timeZone[0] == 'U') {
            if ((timeZone[1] == 't' || timeZone[1] == 'T') &&
                (timeZone[2] == 'c' || timeZone[2] == 'C') && timeZone[3] == '\0')
                pstrcpy(timeZone, sizeof(timeZone), "UTC");
        }
        /* Read options in spec order: disambiguation, offset, overflow */
        int dis_idx = 2; /* default compatible */
        if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
            return JS_EXCEPTION;
        if (argc >= 2) {
            static const char *dis_values[] = { "earlier", "later", "compatible", "reject" };
            dis_idx = ti_get_string_option(ctx, argv[1], "disambiguation", dis_values, 4, 2);
            if (dis_idx < 0)
                return JS_EXCEPTION;
            static const char *offset_values[] = { "use", "ignore", "prefer", "reject" };
            offset_idx = ti_get_string_option(ctx, argv[1], "offset", offset_values, 4, 3);
            if (offset_idx < 0)
                return JS_EXCEPTION;
        }
        if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
        /* Compute epoch seconds from parsed fields */
        {
            int64_t days = pdt_to_ordinal_days(r.year, r.month, r.day);
            {
                /* CheckISODaysRange: validate local date if needed */
                int check_local = r.has_utc;
                if (!check_local && (offset_idx == 2 || offset_idx == 3))
                    check_local = 1;
                if (check_local) {
                    int64_t epoch_days = days - 719162;
                    if (epoch_days < -100000000LL || epoch_days > 100000000LL)
                        return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
                }
            }
            ts.secs = (days - 719162) * 86400LL + r.hour * 3600LL + r.minute * 60LL + r.second;
            ts.nsecs = (uint32_t)(r.millisecond * 1000000 + r.microsecond * 1000 + r.nanosecond);
        }
        /* Determine timezone offset to subtract */
        {
            int64_t prefix_offset_ns = 0;
            int64_t timezone_offset_ns = 0;
            int has_timezone_offset = 0;
            int tz_id = -1;

            if (r.has_tz_offset) {
                prefix_offset_ns = (int64_t)r.tz_offset_seconds * 1000000000LL + r.tz_offset_nanoseconds;
            }

            /* Compute timezone offset from bracket annotation */
            {
                size_t tz_len = strlen(timeZone);
                if ((timeZone[0] == '+' || timeZone[0] == '-') &&
                    (tz_len == 3 || tz_len == 5 || (tz_len == 6 && timeZone[3] == ':'))) {
                    int tz_sign = (timeZone[0] == '-') ? -1 : 1;
                    int tz_hour = (timeZone[1] - '0') * 10 + (timeZone[2] - '0');
                    int tz_minute = 0;
                    if (tz_len == 5)
                        tz_minute = (timeZone[3] - '0') * 10 + (timeZone[4] - '0');
                    else if (tz_len == 6)
                        tz_minute = (timeZone[4] - '0') * 10 + (timeZone[5] - '0');
                    timezone_offset_ns = tz_sign * ((int64_t)tz_hour * 3600 + tz_minute * 60) * 1000000000LL;
                    has_timezone_offset = 1;
                }
            }

            tz_id = ti_tz_find_id(timeZone);

            {
                int is_fixed_or_utc = has_timezone_offset || strcmp(timeZone, "UTC") == 0;
                if (r.has_utc) {
                    tz_offset_ns = 0;
                } else if (r.has_tz_offset) {
                    /* Both prefix offset and bracket timezone present */
                    if (tz_id >= 0) {
                        /* For reject and prefer-match-check, compute offset using
                           prefix as a rough guess. For ignore/prefer-mismatch, use
                           ti_tz_local_to_utc with disambiguation. */
                        int64_t guess_sec = ts.secs - prefix_offset_ns / 1000000000LL;
                        int offset_sec = ti_tz_offset_at_sec(tz_id, guess_sec);
                        int64_t computed_offset_ns = (int64_t)offset_sec * 1000000000LL;
                        /* Check for ambiguous local time near transitions (e.g. Pacific/Niue).
                           Only when an earlier candidate has an offset within 60 seconds
                           of the prefix offset (minute-rounded match). */
                        if (!r.tz_offset_has_seconds) {
                            int prev_offset_sec = ti_tz_prev_offset_at_sec(tz_id, guess_sec);
                            int64_t prev_offset_ns = (int64_t)prev_offset_sec * 1000000000LL;
                            int64_t diff = prefix_offset_ns - prev_offset_ns;
                            if (diff < 0) diff = -diff;
                            if (diff < 60 * 1000000000LL) {
                                int64_t cand1 = ts.secs - computed_offset_ns / 1000000000LL;
                                int64_t cand2 = ts.secs - prev_offset_ns / 1000000000LL;
                                if (cand2 < cand1 &&
                                    ti_tz_offset_at_sec(tz_id, cand2) == prev_offset_sec &&
                                    ti_tz_offset_at_sec(tz_id, cand1) == offset_sec) {
                                    computed_offset_ns = prev_offset_ns;
                                }
                            }
                        }
                        if (offset_idx == 3) { /* reject */
                            if (r.tz_offset_has_seconds) {
                                if (prefix_offset_ns != computed_offset_ns)
                                    return JS_ThrowRangeError(ctx, "offset does not match time zone");
                                tz_offset_ns = prefix_offset_ns;
                            } else {
                                int64_t diff_ns = prefix_offset_ns - computed_offset_ns;
                                if (diff_ns < 0)
                                    diff_ns = -diff_ns;
                                if (diff_ns >= 60 * 1000000000LL)
                                    return JS_ThrowRangeError(ctx, "offset does not match time zone");
                                tz_offset_ns = computed_offset_ns;
                            }
                        } else if (offset_idx == 0) { /* use */
                            tz_offset_ns = prefix_offset_ns;
                        } else if (offset_idx == 1) { /* ignore */
                            int64_t utc_sec;
                            int act_offset_sec;
                            if (ti_tz_local_to_utc(tz_id, ts.secs, dis_idx, &utc_sec, &act_offset_sec) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                            ts.secs = utc_sec;
                            tz_offset_ns = (int64_t)act_offset_sec * 1000000000LL;
                            has_computed_utc = 1;
                        } else { /* prefer */
                            /* prefer only uses the prefix offset when it exactly
                               matches the computed offset. Minute-rounded offsets
                               that are within 60s but not exact are treated like ignore. */
                            if (prefix_offset_ns == computed_offset_ns) {
                                tz_offset_ns = prefix_offset_ns;
                            } else {
                                int64_t utc_sec;
                                int act_offset_sec;
                                if (ti_tz_local_to_utc(tz_id, ts.secs, dis_idx, &utc_sec, &act_offset_sec) < 0)
                                    return JS_ThrowRangeError(ctx, "multiple instants found");
                                ts.secs = utc_sec;
                                tz_offset_ns = (int64_t)act_offset_sec * 1000000000LL;
                                has_computed_utc = 1;
                            }
                        }
                    } else if (!is_fixed_or_utc) {
                        /* Unsupported IANA timezone: can't verify agreement, use prefix offset */
                        tz_offset_ns = prefix_offset_ns;
                    } else if (offset_idx == 3) { /* reject */
                        if (prefix_offset_ns != timezone_offset_ns)
                            return JS_ThrowRangeError(ctx, "offset does not match time zone");
                        tz_offset_ns = prefix_offset_ns;
                    } else if (offset_idx == 0) { /* use */
                        tz_offset_ns = prefix_offset_ns;
                    } else if (offset_idx == 1) { /* ignore */
                        tz_offset_ns = has_timezone_offset ? timezone_offset_ns : 0;
                    } else { /* prefer */
                        if (prefix_offset_ns == timezone_offset_ns)
                            tz_offset_ns = prefix_offset_ns;
                        else
                            tz_offset_ns = has_timezone_offset ? timezone_offset_ns : 0;
                    }
                } else {
                    /* Only bracket timezone (no prefix offset) */
                    if (tz_id >= 0) {
                        if (!r.has_time) {
                            /* Date-only string: use GetStartOfDay semantics */
                            int64_t utc_sec = ti_get_start_of_day_sec(tz_id, ts.secs);
                            if (utc_sec == INT64_MIN)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                            ts.secs = utc_sec;
                            tz_offset_ns = (int64_t)ti_tz_offset_at_sec(tz_id, utc_sec) * 1000000000LL;
                            has_computed_utc = 1;
                        } else {
                            int64_t utc_sec;
                            int offset_sec;
                            if (ti_tz_local_to_utc(tz_id, ts.secs, dis_idx, &utc_sec, &offset_sec) < 0)
                                return JS_ThrowRangeError(ctx, "multiple instants found");
                            ts.secs = utc_sec;
                            tz_offset_ns = (int64_t)offset_sec * 1000000000LL;
                            has_computed_utc = 1;
                        }
                    } else {
                        tz_offset_ns = has_timezone_offset ? timezone_offset_ns : 0;
                    }
                }
            }
        }
        /* Apply offset: UTC = local - offset */
        if (!has_computed_utc) {
            int64_t offset_sec = tz_offset_ns / 1000000000LL;
            int offset_nsec = (int)(tz_offset_ns % 1000000000LL);
            ts.secs -= offset_sec;
            {
                int new_nsec = (int)ts.nsecs - offset_nsec;
                if (new_nsec < 0) {
                    new_nsec += 1000000000;
                    ts.secs -= 1;
                } else if (new_nsec >= 1000000000) {
                    new_nsec -= 1000000000;
                    ts.secs += 1;
                }
                ts.nsecs = (uint32_t)new_nsec;
            }
        }
        /* Validate epoch nanoseconds are within valid range */
        if (!is_valid_epoch_timespec(&ts))
            return JS_ThrowRangeError(ctx, "invalid ZonedDateTime string");
        {
            CalendarType cal = r.has_calendar ? r.calendar : CAL_ISO8601;
            if (cal == CAL_UNKNOWN)
                return JS_ThrowRangeError(ctx, "invalid calendar ID");
            return js_create_zoneddatetime(ctx, &ts, timeZone, cal);
        }
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.ZonedDateTime.from");
}

static const JSCFunctionListEntry js_zoneddatetime_proto_funcs[] = {
    JS_CGETSET_DEF("epochNanoseconds", js_zoneddatetime_get_epochNanoseconds, NULL),
    JS_CGETSET_DEF("epochMilliseconds", js_zoneddatetime_get_epochMilliseconds, NULL),
    JS_CGETSET_DEF("timeZoneId", js_zoneddatetime_get_timeZoneId, NULL),
    JS_CGETSET_DEF("calendarId", js_zoneddatetime_get_calendarId, NULL),
    JS_CGETSET_DEF("year", js_zoneddatetime_get_year, NULL),
    JS_CGETSET_DEF("month", js_zoneddatetime_get_month, NULL),
    JS_CGETSET_DEF("day", js_zoneddatetime_get_day, NULL),
    JS_CGETSET_DEF("hour", js_zoneddatetime_get_hour, NULL),
    JS_CGETSET_DEF("minute", js_zoneddatetime_get_minute, NULL),
    JS_CGETSET_DEF("second", js_zoneddatetime_get_second, NULL),
    JS_CGETSET_DEF("millisecond", js_zoneddatetime_get_millisecond, NULL),
    JS_CGETSET_DEF("microsecond", js_zoneddatetime_get_microsecond, NULL),
    JS_CGETSET_DEF("nanosecond", js_zoneddatetime_get_nanosecond, NULL),
    JS_CGETSET_DEF("offset", js_zoneddatetime_get_offset, NULL),
    JS_CGETSET_DEF("offsetNanoseconds", js_zoneddatetime_get_offsetNanoseconds, NULL),
    JS_CGETSET_DEF("era", js_zoneddatetime_get_era, NULL),
    JS_CGETSET_DEF("eraYear", js_zoneddatetime_get_eraYear, NULL),
    JS_CGETSET_DEF("monthCode", js_zoneddatetime_get_monthCode, NULL),
    JS_CGETSET_DEF("inLeapYear", js_zoneddatetime_get_inLeapYear, NULL),
    JS_CGETSET_DEF("daysInYear", js_zoneddatetime_get_daysInYear, NULL),
    JS_CGETSET_DEF("daysInWeek", js_zoneddatetime_get_daysInWeek, NULL),
    JS_CGETSET_DEF("monthsInYear", js_zoneddatetime_get_monthsInYear, NULL),
    JS_CGETSET_DEF("hoursInDay", js_zoneddatetime_get_hoursInDay, NULL),
    JS_CGETSET_DEF("weekOfYear", js_zoneddatetime_get_weekOfYear, NULL),
    JS_CGETSET_DEF("yearOfWeek", js_zoneddatetime_get_yearOfWeek, NULL),
    JS_CGETSET_DEF("daysInMonth", js_zoneddatetime_get_daysInMonth, NULL),
    JS_CGETSET_DEF("dayOfYear", js_zoneddatetime_get_dayOfYear, NULL),
    JS_CGETSET_DEF("dayOfWeek", js_zoneddatetime_get_dayOfWeek, NULL),
    JS_CFUNC_MAGIC_DEF("add", 1, js_zoneddatetime_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_zoneddatetime_add_subtract, 0),
    JS_CFUNC_DEF("round", 1, js_zoneddatetime_round),
    JS_CFUNC_MAGIC_DEF("until", 1, js_zoneddatetime_difference, 1),
    JS_CFUNC_MAGIC_DEF("since", 1, js_zoneddatetime_difference, 0),
    JS_CFUNC_DEF("startOfDay", 0, js_zoneddatetime_startOfDay),
    JS_CFUNC_DEF("getTimeZoneTransition", 1, js_zoneddatetime_getTimeZoneTransition),
    JS_CFUNC_DEF("equals", 1, js_zoneddatetime_equals),
    JS_CFUNC_DEF("with", 1, js_zoneddatetime_with),
    JS_CFUNC_DEF("withCalendar", 1, js_zoneddatetime_withCalendar),
    JS_CFUNC_DEF("withPlainTime", 0, js_zoneddatetime_withPlainTime),
    JS_CFUNC_DEF("withTimeZone", 1, js_zoneddatetime_withTimeZone),
    JS_CFUNC_DEF("toString", 0, js_zoneddatetime_toString),
    JS_CFUNC_DEF("toJSON", 0, js_zoneddatetime_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_zoneddatetime_toLocaleString),
    JS_CFUNC_DEF("valueOf", 0, js_zoneddatetime_valueOf),
    JS_CFUNC_DEF("toInstant", 0, js_zoneddatetime_toInstant),
    JS_CFUNC_DEF("toPlainDateTime", 0, js_zoneddatetime_toPlainDateTime),
    JS_CFUNC_DEF("toPlainDate", 0, js_zoneddatetime_toPlainDate),
    JS_CFUNC_DEF("toPlainTime", 0, js_zoneddatetime_toPlainTime),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.ZonedDateTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_zoneddatetime_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_zoneddatetime_from),
    JS_CFUNC_DEF("compare", 2, js_zoneddatetime_compare),
};

int js_add_zoneddatetime(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_zoneddatetime_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_zoneddatetime_class_id, &js_zoneddatetime_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_zoneddatetime_class_id, "ZonedDateTime",
                               (JSCFunction *)js_zoneddatetime_constructor, 2,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_zoneddatetime_static_funcs, countof(js_zoneddatetime_static_funcs),
                               js_zoneddatetime_proto_funcs, countof(js_zoneddatetime_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "ZonedDateTime", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
