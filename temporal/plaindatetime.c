/*
 * QuickJS Temporal.PlainDateTime implementation (minimal)
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

JSClassID js_plaindatetime_class_id;

static void js_plaindatetime_finalizer(JSRuntime *rt, JSValue val)
{
    JSPlainDateTimeData *s = JS_GetOpaque(val, js_plaindatetime_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_plaindatetime_class = {
    "Temporal.PlainDateTime",
    .finalizer = js_plaindatetime_finalizer,
};

static void pdt_get_calendar_date(JSPlainDateTimeData *s, int *cal_year, int *cal_month, int *cal_day)
{
    ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, cal_year, cal_month, cal_day);
}

int pdt_is_within_limits(int year, int month, int day,
                                 int hour, int minute, int second,
                                 int millisecond, int microsecond, int nanosecond);

JSValue js_create_plaindatetime(JSContext *ctx, int year, int month, int day,
                                        int hour, int minute, int second,
                                        int millisecond, int microsecond, int nanosecond)
{
    return js_create_plaindatetime_with_calendar(ctx, year, month, day, hour, minute, second,
                                                  millisecond, microsecond, nanosecond, CAL_ISO8601);
}

JSValue js_create_plaindatetime_with_calendar(JSContext *ctx, int year, int month, int day,
                                                int hour, int minute, int second,
                                                int millisecond, int microsecond, int nanosecond,
                                                CalendarType calendar)
{
    JSPlainDateTimeData *s;
    JSValue obj;
    if ((calendar == CAL_ISO8601 || calendar == CAL_GREGORY) &&
        !pdt_is_within_limits(year, month, day, hour, minute, second,
                              millisecond, microsecond, nanosecond))
        return JS_ThrowRangeError(ctx, "PlainDateTime outside valid ISO range");
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    s->year = year;
    s->month = month;
    s->day = day;
    s->hour = hour;
    s->minute = minute;
    s->second = second;
    s->millisecond = millisecond;
    s->microsecond = microsecond;
    s->nanosecond = nanosecond;
    s->calendar = calendar;
    obj = JS_NewObjectClass(ctx, (int)js_plaindatetime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

static JSValue ti_create_plaindatetime_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                             int overflow_constrain)
{
    int year, month, day, hour, minute, second, ms, us, ns;

    if (ti_resolve_date_from_fields(ctx, fields, overflow_constrain, &year, &month, &day, 1) < 0)
        return JS_EXCEPTION;

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

    if (fields->calendar != CAL_ISO8601 && fields->calendar != CAL_GREGORY) {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(fields->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        year = iso_y; month = iso_m; day = iso_d;
    }
    return js_create_plaindatetime_with_calendar(ctx, year, month, day, hour, minute, second, ms, us, ns, fields->calendar);
}

static void pdt_balance_date(int *year, int *month, int *day);

static int pdt_is_valid_date(int year, int month, int day)
{
    if (month < 1 || month > 12)
        return 0;
    if (day < 1 || day > pd_days_in_month(year, month))
        return 0;
    return 1;
}

static int pdt_is_valid_date_for_calendar(int year, int month, int day, CalendarType cal)
{
    int max_month = (cal == CAL_ISO8601 || cal == CAL_GREGORY) ? 12 : ti_calendar_months_in_year(cal, year);
    if (month < 1 || month > max_month)
        return 0;
    {
        int dim = (cal == CAL_ISO8601 || cal == CAL_GREGORY) ? pd_days_in_month(year, month) : ti_calendar_days_in_month(cal, year, month);
        if (day < 1 || day > dim)
            return 0;
    }
    return 1;
}

static int pdt_is_valid_time(int hour, int minute, int second,
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

/* PlainDateTime limits: -271821-04-19T00:00:00.000000001 to +275760-09-13T23:59:59.999999999 */
int pdt_is_within_limits(int year, int month, int day,
                                 int hour, int minute, int second,
                                 int millisecond, int microsecond, int nanosecond)
{
    if (year < -271821 || year > 275760)
        return 0;
    if (year == -271821) {
        if (month < 4) return 0;
        if (month == 4 && day < 19) return 0;
        if (month == 4 && day == 19) {
            if (hour < 0 || minute < 0 || second < 0 ||
                millisecond < 0 || microsecond < 0 || nanosecond < 0)
                return 0;
            /* Minimum is 00:00:00.000000001; 00:00:00.000000000 is out of range */
            if (hour == 0 && minute == 0 && second == 0 &&
                millisecond == 0 && microsecond == 0 && nanosecond == 0)
                return 0;
        }
    }
    if (year == 275760) {
        if (month > 9) return 0;
        if (month == 9 && day > 13) return 0;
        if (month == 9 && day == 13) {
            if (hour > 23 || minute > 59 || second > 59 ||
                millisecond > 999 || microsecond > 999 || nanosecond > 999)
                return 0;
        }
    }
    return 1;
}

static int pdt_to_integer_with_truncation(JSContext *ctx, JSValueConst val, int *pres)
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

/* Temporal.PlainDateTime constructor */
static JSValue js_plaindatetime_constructor(JSContext *ctx, JSValueConst new_target,
                                             int argc, JSValueConst *argv)
{
    int year, month, day, hour = 0, minute = 0, second = 0;
    int millisecond = 0, microsecond = 0, nanosecond = 0;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.PlainDateTime constructor cannot be called as a function");
    }
    if (JS_IsUndefined(argv[0])) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return JS_EXCEPTION;
    }
    if (pdt_to_integer_with_truncation(ctx, argv[0], &year) < 0)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[1])) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return JS_EXCEPTION;
    }
    if (pdt_to_integer_with_truncation(ctx, argv[1], &month) < 0)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[2])) {
        JS_ThrowRangeError(ctx, "value must be an integer");
        return JS_EXCEPTION;
    }
    if (pdt_to_integer_with_truncation(ctx, argv[2], &day) < 0)
        return JS_EXCEPTION;
    if (argc > 3 && pdt_to_integer_with_truncation(ctx, argv[3], &hour) < 0)
        return JS_EXCEPTION;
    if (argc > 4 && pdt_to_integer_with_truncation(ctx, argv[4], &minute) < 0)
        return JS_EXCEPTION;
    if (argc > 5 && pdt_to_integer_with_truncation(ctx, argv[5], &second) < 0)
        return JS_EXCEPTION;
    if (argc > 6 && pdt_to_integer_with_truncation(ctx, argv[6], &millisecond) < 0)
        return JS_EXCEPTION;
    if (argc > 7 && pdt_to_integer_with_truncation(ctx, argv[7], &microsecond) < 0)
        return JS_EXCEPTION;
    if (argc > 8 && pdt_to_integer_with_truncation(ctx, argv[8], &nanosecond) < 0)
        return JS_EXCEPTION;
    if (!pdt_is_valid_date(year, month, day))
        return JS_ThrowRangeError(ctx, "invalid PlainDateTime date");
    if (!pdt_is_valid_time(hour, minute, second, millisecond, microsecond, nanosecond))
        return JS_ThrowRangeError(ctx, "invalid PlainDateTime time");
    {
        CalendarType calendar = CAL_ISO8601;
        if (argc > 9) {
            int cal_res = ti_validate_constructor_calendar(ctx, argv[9], &calendar);
            if (cal_res < 0)
                return JS_EXCEPTION;
        }
        if ((calendar == CAL_ISO8601 || calendar == CAL_GREGORY) &&
            !pdt_is_within_limits(year, month, day, hour, minute, second,
                                   millisecond, microsecond, nanosecond))
            return JS_ThrowRangeError(ctx, "PlainDateTime outside valid range");
        {
            JSPlainDateTimeData *s = js_mallocz(ctx, sizeof(*s));
            JSValue obj;
            if (!s)
                return JS_EXCEPTION;
            s->year = year;
            s->month = month;
            s->day = day;
            s->hour = hour;
            s->minute = minute;
            s->second = second;
            s->millisecond = millisecond;
            s->microsecond = microsecond;
            s->nanosecond = nanosecond;
            s->calendar = calendar;
        obj = js_create_temporal_object(ctx, new_target, js_plaindatetime_class_id, s);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        return obj;
        }
    }
}

/* Getters */
enum {
    PLAINDATETIME_YEAR = 0,
    PLAINDATETIME_MONTH,
    PLAINDATETIME_DAY,
    PLAINDATETIME_HOUR,
    PLAINDATETIME_MINUTE,
    PLAINDATETIME_SECOND,
    PLAINDATETIME_MILLISECOND,
    PLAINDATETIME_MICROSECOND,
    PLAINDATETIME_NANOSECOND,
};

static JSValue js_plaindatetime_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    switch (magic) {
    case PLAINDATETIME_YEAR: return JS_NewInt32(ctx, cal_y);
    case PLAINDATETIME_MONTH: return JS_NewInt32(ctx, cal_m);
    case PLAINDATETIME_DAY: return JS_NewInt32(ctx, cal_d);
    case PLAINDATETIME_HOUR: return JS_NewInt32(ctx, s->hour);
    case PLAINDATETIME_MINUTE: return JS_NewInt32(ctx, s->minute);
    case PLAINDATETIME_SECOND: return JS_NewInt32(ctx, s->second);
    case PLAINDATETIME_MILLISECOND: return JS_NewInt32(ctx, s->millisecond);
    case PLAINDATETIME_MICROSECOND: return JS_NewInt32(ctx, s->microsecond);
    case PLAINDATETIME_NANOSECOND: return JS_NewInt32(ctx, s->nanosecond);
    default: return JS_UNDEFINED;
    }
}

static JSValue js_plaindatetime_get_monthCode(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return ti_get_month_code(ctx, s->calendar, cal_y, cal_m);
}

static JSValue js_plaindatetime_get_calendarId(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar));
}

static int pdt_day_of_week(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 2;
    int d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
    return (d == 0) ? 7 : d;
}

static void pdt_iso_week(int year, int month, int day, int *weekYear, int *weekNumber)
{
    int dow = pdt_day_of_week(year, month, day);
    int ordinal = pd_day_of_year(year, month, day);
    int thursdayOrdinal = ordinal + (4 - dow);
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

static JSValue js_plaindatetime_get_era(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    TIEra era;
    int dummy_era_year;
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->year, s->month, s->day, cal_y, &era, &dummy_era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewString(ctx, ti_era_to_string(era));
    return JS_UNDEFINED;
}

static JSValue js_plaindatetime_get_eraYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int era_year;
    TIEra era;
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->year, s->month, s->day, cal_y, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewInt32(ctx, era_year);
    return JS_UNDEFINED;
}

static JSValue js_plaindatetime_get_inLeapYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewBool(ctx, ti_calendar_is_leap_year(s->calendar, cal_y));
}

static JSValue js_plaindatetime_get_daysInMonth(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_month(s->calendar, cal_y, cal_m));
}

static JSValue js_plaindatetime_get_daysInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_year(s->calendar, cal_y));
}

static JSValue js_plaindatetime_get_daysInWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, 7);
}

static JSValue js_plaindatetime_get_monthsInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_months_in_year(s->calendar, cal_y));
}

static JSValue js_plaindatetime_get_dayOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pdt_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_day_of_year(s->calendar, cal_y, cal_m, cal_d));
}

static JSValue js_plaindatetime_get_dayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, pdt_day_of_week(s->year, s->month, s->day));
}

static JSValue js_plaindatetime_get_weekOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pdt_iso_week(s->year, s->month, s->day, &weekYear, &weekNumber);
    return JS_NewInt32(ctx, weekNumber);
}

static JSValue js_plaindatetime_get_yearOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pdt_iso_week(s->year, s->month, s->day, &weekYear, &weekNumber);
    return JS_NewInt64(ctx, weekYear);
}

/* Temporal.PlainDateTime.prototype.equals */
static JSValue js_plaindatetime_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv);

static JSValue js_plaindatetime_equals(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSPlainDateTimeData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsNumber(argv[0]) || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]) ||
        JS_IsBool(argv[0]) || JS_IsBigInt(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "invalid PlainDateTime");
    other_val = js_plaindatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaindatetime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    {
        int res = s->year == other->year && s->month == other->month &&
                  s->day == other->day && s->hour == other->hour &&
                  s->minute == other->minute && s->second == other->second &&
                  s->millisecond == other->millisecond &&
                  s->microsecond == other->microsecond &&
                  s->nanosecond == other->nanosecond &&
                  s->calendar == other->calendar;
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, res);
    }
}

/* Temporal.PlainDateTime.compare */
static JSValue js_plaindatetime_compare(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    JSPlainDateTimeData *s1, *s2;
    JSValue v1, v2;
    int cmp;
    v1 = js_plaindatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(v1))
        return JS_EXCEPTION;
    v2 = js_plaindatetime_from(ctx, JS_UNDEFINED, 1, argv + 1);
    if (JS_IsException(v2)) {
        JS_FreeValue(ctx, v1);
        return JS_EXCEPTION;
    }
    s1 = JS_GetOpaque(v1, js_plaindatetime_class_id);
    s2 = JS_GetOpaque(v2, js_plaindatetime_class_id);
    if (!s1 || !s2) {
        JS_FreeValue(ctx, v1);
        JS_FreeValue(ctx, v2);
        return JS_ThrowTypeError(ctx, "compare arguments must be PlainDateTime objects");
    }
    if (s1->year != s2->year)
        cmp = s1->year < s2->year ? -1 : 1;
    else if (s1->month != s2->month)
        cmp = s1->month < s2->month ? -1 : 1;
    else if (s1->day != s2->day)
        cmp = s1->day < s2->day ? -1 : 1;
    else if (s1->hour != s2->hour)
        cmp = s1->hour < s2->hour ? -1 : 1;
    else if (s1->minute != s2->minute)
        cmp = s1->minute < s2->minute ? -1 : 1;
    else if (s1->second != s2->second)
        cmp = s1->second < s2->second ? -1 : 1;
    else if (s1->millisecond != s2->millisecond)
        cmp = s1->millisecond < s2->millisecond ? -1 : 1;
    else if (s1->microsecond != s2->microsecond)
        cmp = s1->microsecond < s2->microsecond ? -1 : 1;
    else if (s1->nanosecond != s2->nanosecond)
        cmp = s1->nanosecond < s2->nanosecond ? -1 : 1;
    else
        cmp = 0;
    JS_FreeValue(ctx, v1);
    JS_FreeValue(ctx, v2);
    return JS_NewInt32(ctx, cmp);
}

/* Format PlainDateTime as ISO string */
static JSValue js_plaindatetime_toString(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSPlainDateTimeData *s;
    JSPlainTimeData rounded = {0};
    char buf[128];
    int n;
    int calendarName = 0; /* 0=auto (no annotation for ISO), 1=always, 2=critical */
    int precision_type = -1; /* -1=auto, 0=second, 1-9=digits, 10=minute, 11=second, 12=ms, 13=us, 14=ns */
    int fsd_is_auto = 1;
    int fsd_value = 0;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int has_smallestUnit = 0;
    TIRoundingMode rounding_mode = ROUND_TRUNC;
    if (argc >= 1 && ti_validate_options_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
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
        /* Step 3: roundingMode */
        {
            TIRoundingMode mode;
            if (ti_get_rounding_mode_option_default(ctx, argv[0], &mode, ROUND_TRUNC) < 0)
                return JS_EXCEPTION;
            rounding_mode = mode;
        }
        /* Step 4: smallestUnit */
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
    /* Determine precision */
    if (has_smallestUnit && smallestUnit >= TI_UNIT_MINUTE && smallestUnit <= TI_UNIT_NANOSECOND) {
        precision_type = ti_precision_from_smallest_unit(smallestUnit);
    } else if (!fsd_is_auto) {
        precision_type = fsd_value;
    } else {
        precision_type = -1; /* auto */
    }
    /* Apply rounding */
    {
        int fmt_year = s->year;
        int fmt_month = s->month;
        int fmt_day = s->day;
        rounded.hour = s->hour;
        rounded.minute = s->minute;
        rounded.second = s->second;
        rounded.millisecond = s->millisecond;
        rounded.microsecond = s->microsecond;
        rounded.nanosecond = s->nanosecond;
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
            if (rounded_ns >= 86400000000000LL) {
                rounded_ns -= 86400000000000LL;
                fmt_day++;
                pdt_balance_date(&fmt_year, &fmt_month, &fmt_day);
            }
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
            if ((s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) &&
                !pdt_is_within_limits(fmt_year, fmt_month, fmt_day,
                                      rounded.hour, rounded.minute, rounded.second,
                                      rounded.millisecond, rounded.microsecond, rounded.nanosecond))
                return JS_ThrowRangeError(ctx, "PlainDateTime outside valid range");
        }
        /* Format date */
        if (fmt_year < 0)
            n = snprintf(buf, sizeof(buf), "-%06d-%02d-%02dT",
                         -fmt_year, fmt_month, fmt_day);
        else if (fmt_year >= 10000)
            n = snprintf(buf, sizeof(buf), "+%06d-%02d-%02dT",
                         fmt_year, fmt_month, fmt_day);
        else
            n = snprintf(buf, sizeof(buf), "%04d-%02d-%02dT",
                         fmt_year, fmt_month, fmt_day);
    }
    /* Format time */
    if (precision_type == 10) { /* minute */
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%02d:%02d",
                      rounded.hour, rounded.minute);
    } else {
        n += snprintf(buf + n, sizeof(buf) - (size_t)n, "%02d:%02d:%02d",
                      rounded.hour, rounded.minute, rounded.second);
        if (precision_type != 0 && precision_type != 11) {
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
                while (digits > 0 && frac[digits - 1] == '0')
                    digits--;
            }
            if (digits > 0) {
                frac[digits] = '\0';
                n += snprintf(buf + n, sizeof(buf) - (size_t)n, ".%s", frac);
            }
        }
    }
    ti_append_calendar_annotation(buf, sizeof(buf), s->calendar, calendarName);
    return JS_NewString(ctx, buf);
}

static JSValue js_plaindatetime_toJSON(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_plaindatetime_toString(ctx, this_val, 0, NULL);
}

static JSValue js_plaindatetime_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 0, js_plaindatetime_toString);
}

static JSValue js_plaindatetime_valueOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.PlainDateTime with < or >");
}

/* Temporal.PlainDateTime.prototype.with */
static JSValue js_plaindatetime_with(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSPlainDateTimeData *s;
    int year, month, day, hour, minute, second;
    int millisecond, microsecond, nanosecond;
    int orig_cal_year, orig_cal_month;
    JSValue val;
    int has_month_prop = 0;
    int has_year_prop = 0;
    int has_month_code = 0;
    int month_code_val = 0;
    int has_month_code_l = 0;
    int overflow_constrain = 1;
    TIPreparedFields fields;
    s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &orig_cal_year, &orig_cal_month, &day);
    year = orig_cal_year; month = orig_cal_month;
    hour = s->hour; minute = s->minute; second = s->second;
    millisecond = s->millisecond; microsecond = s->microsecond; nanosecond = s->nanosecond;
    val = argv[0];
    if (ti_is_partial_temporal_object(ctx, val) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, val,
                                    TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                    TI_FIELD_YEAR | TI_FIELD_HOUR | TI_FIELD_MINUTE |
                                    TI_FIELD_SECOND | TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND |
                                    TI_FIELD_NANOSECOND,
                                    0, 1, &fields, s->calendar) < 0)
        return JS_EXCEPTION;
    /* Merge fields with existing object */
    if (fields.present_mask & TI_FIELD_DAY) {
        day = fields.day;
    }
    if (fields.present_mask & TI_FIELD_HOUR) {
        hour = fields.hour;
    }
    if (fields.present_mask & TI_FIELD_MICROSECOND) {
        microsecond = fields.microsecond;
    }
    if (fields.present_mask & TI_FIELD_MILLISECOND) {
        millisecond = fields.millisecond;
    }
    if (fields.present_mask & TI_FIELD_MINUTE) {
        minute = fields.minute;
    }
    if (fields.present_mask & TI_FIELD_MONTH) {
        has_month_prop = 1;
        month = fields.month;
    }
    if (fields.present_mask & TI_FIELD_MONTH_CODE) {
        has_month_code = 1;
        month_code_val = fields.month_from_code;
        has_month_code_l = fields.month_code_has_l;
    }
    if (fields.present_mask & TI_FIELD_NANOSECOND) {
        nanosecond = fields.nanosecond;
    }
    if (fields.present_mask & TI_FIELD_SECOND) {
        second = fields.second;
    }
    if (fields.present_mask & TI_FIELD_YEAR) {
        has_year_prop = 1;
        year = (int)fields.year;
    }
    /* Validate options type */
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
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
            if (has_year_prop && year != cal_year_from_era)
                return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
            year = cal_year_from_era;
        }
    }

    /* monthCode processing */
    if (has_month_code) {
        if (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) {
            if (month_code_val < 1 || month_code_val > 12 || has_month_code_l)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for ISO 8601 calendar");
        } else {
            if (!ti_calendar_validate_month_code(s->calendar, month_code_val, has_month_code_l))
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
        }
    }
    if (has_month_code && has_month_prop) {
        int resolved_month = ti_calendar_month_code_to_month(s->calendar, year, month_code_val, has_month_code_l);
        if (resolved_month >= 0 && resolved_month != month)
            return JS_ThrowRangeError(ctx, "month and monthCode must agree");
    }
    if (has_month_code) {
        if (!(s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY)) {
            int resolved_month = ti_calendar_month_code_to_month(s->calendar, year, month_code_val, has_month_code_l);
            if (resolved_month < 0)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
            month = resolved_month;
        } else {
            month = month_code_val;
        }
    }
    /* If monthCode was preserved from source but year changed, re-resolve */
    if (!has_month_code && !has_month_prop && year != orig_cal_year) {
        int orig_mc, orig_has_l;
        int result_mc, result_has_l;
        int resolved_month;
        ti_calendar_month_to_month_code(s->calendar, orig_cal_year, orig_cal_month, &orig_mc, &orig_has_l);
        resolved_month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, orig_has_l);
        if (resolved_month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime date");
            resolved_month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, 0);
        }
        month = resolved_month;
        ti_calendar_month_to_month_code(s->calendar, year, month, &result_mc, &result_has_l);
        if (!overflow_constrain && (result_mc != orig_mc || result_has_l != orig_has_l))
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime date");
    }
    {
        int is_valid = pdt_is_valid_date_for_calendar(year, month, day, s->calendar);
        if (!is_valid) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime date");
            {
                int max_month = ti_calendar_months_in_year(s->calendar, year);
                if (month > max_month)
                    month = max_month;
                else if (month < 1)
                    month = 1;
            }
            {
                int dim = ti_calendar_days_in_month(s->calendar, year, month);
                if (day > dim)
                    day = dim;
                else if (day < 1)
                    day = 1;
            }
        }
    }
    if (!pdt_is_valid_time(hour, minute, second, millisecond, microsecond, nanosecond)) {
        if (!overflow_constrain)
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime time");
        /* Clamp each field independently to valid range */
        if (nanosecond > 999) nanosecond = 999;
        else if (nanosecond < 0) nanosecond = 0;
        if (microsecond > 999) microsecond = 999;
        else if (microsecond < 0) microsecond = 0;
        if (millisecond > 999) millisecond = 999;
        else if (millisecond < 0) millisecond = 0;
        if (second > 59) second = 59;
        else if (second < 0) second = 0;
        if (minute > 59) minute = 59;
        else if (minute < 0) minute = 0;
        if (hour > 23) {
            hour = 23;
            minute = 59;
            second = 59;
            millisecond = 999;
            microsecond = 999;
            nanosecond = 999;
        } else if (hour < 0) {
            hour = 0;
            minute = 0;
            second = 0;
            millisecond = 0;
            microsecond = 0;
            nanosecond = 0;
        }
    }
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        year = iso_y; month = iso_m; day = iso_d;
    }
    if (!pdt_is_within_limits(year, month, day, hour, minute, second,
                              millisecond, microsecond, nanosecond))
        return JS_ThrowRangeError(ctx, "PlainDateTime outside valid range");
    return js_create_plaindatetime_with_calendar(ctx, year, month, day, hour, minute, second,
                                    millisecond, microsecond, nanosecond, s->calendar);
}

static void pdt_balance_month(int *year, int *month)
{
    while (*month > 12) { *month -= 12; (*year)++; }
    while (*month < 1) { *month += 12; (*year)--; }
}

static void pdt_balance_date(int *year, int *month, int *day)
{
    int dim;
    pdt_balance_month(year, month);
    while (*day > (dim = pd_days_in_month(*year, *month))) {
        *day -= dim;
        (*month)++;
        if (*month > 12) { *month = 1; (*year)++; }
    }
    while (*day < 1) {
        (*month)--;
        if (*month < 1) { *month = 12; (*year)--; }
        dim = pd_days_in_month(*year, *month);
        *day += dim;
    }
}



static JSValue js_plaindatetime_add_subtract(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv,
                                              int is_add)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSDurationData d = {0};
    int year, month, day, hour, minute, second, ms, us, ns;
    int overflow_constrain = 1;
    js_int128_t total_ns, time_ns, dur_ns;
    int64_t day_delta;
    int sign = is_add ? 1 : -1;
    if (!s)
        return JS_EXCEPTION;
    if (js_to_temporal_duration(ctx, argv[0], &d))
        return JS_EXCEPTION;
    if (duration_has_mixed_signs(&d))
        return JS_ThrowRangeError(ctx, "mixed-sign duration not allowed");
    if (duration_is_out_of_range(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
    }
    /* Date part */
    year = s->year; month = s->month; day = s->day;
    ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &year, &month, &day);
    {
        int orig_mc = 0, orig_has_l = 0;
        ti_calendar_month_to_month_code(s->calendar, year, month, &orig_mc, &orig_has_l);
        year += sign * (int)d.years;
        month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, orig_has_l);
        if (month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
            month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, 0);
        } else if (!overflow_constrain) {
            int result_mc, result_has_l;
            ti_calendar_month_to_month_code(s->calendar, year, month, &result_mc, &result_has_l);
            if (result_mc != orig_mc || result_has_l != orig_has_l)
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
        }
    }
    month += sign * (int)d.months;
    pdt_balance_month_calendar(&year, &month, s->calendar);
    {
        int dim = ti_calendar_days_in_month(s->calendar, year, month);
        if (day > dim) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
            day = dim;
        }
    }
    day += sign * (int)(d.weeks * 7 + d.days);
    pdt_balance_date_calendar(&year, &month, &day, s->calendar);
    /* Time part */
    hour = s->hour;
    minute = s->minute;
    second = s->second;
    ms = s->millisecond;
    us = (int)s->microsecond;
    ns = (int)s->nanosecond;
    {
        int64_t time_sec = hour * 3600LL + minute * 60LL + second;
        time_ns = (js_int128_t)time_sec * 1000000000LL + (js_int128_t)ms * 1000000LL + (js_int128_t)us * 1000LL + ns;
    }
    dur_ns = ti_duration_time_fields_to_ns(&d);
    total_ns = time_ns + sign * dur_ns;
    day_delta = (int64_t)(total_ns / 86400000000000LL);
    total_ns = total_ns % 86400000000000LL;
    if (total_ns < 0) {
        day_delta--;
        total_ns += 86400000000000LL;
    }
    day += (int)day_delta;
    pdt_balance_date_calendar(&year, &month, &day, s->calendar);
    hour = (int)(total_ns / 3600000000000LL);
    total_ns %= 3600000000000LL;
    minute = (int)(total_ns / 60000000000LL);
    total_ns %= 60000000000LL;
    second = (int)(total_ns / 1000000000LL);
    total_ns %= 1000000000LL;
    ms = (int)(total_ns / 1000000LL);
    total_ns %= 1000000LL;
    us = (int)(total_ns / 1000LL);
    ns = (int)(total_ns % 1000LL);
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        year = iso_y; month = iso_m; day = iso_d;
    }
    if (!pdt_is_within_limits(year, month, day, hour, minute, second, ms, us, ns))
        return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
    {
        JSValue obj = js_create_plaindatetime(ctx, year, month, day, hour, minute, second, ms, us, ns);
        if (!JS_IsException(obj)) {
            JSPlainDateTimeData *res = JS_GetOpaque(obj, js_plaindatetime_class_id);
            if (res)
                res->calendar = s->calendar;
        }
        return obj;
    }
}

static JSValue js_plaindatetime_withPlainTime(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSPlainTimeData *pt;
    JSValue pt_val;
    if (!s)
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
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
        JSValue result = js_create_plaindatetime(ctx, s->year, s->month, s->day,
                                                  pt->hour, pt->minute, pt->second,
                                                  pt->millisecond, pt->microsecond, pt->nanosecond);
        JSPlainDateTimeData *res = JS_GetOpaque(result, js_plaindatetime_class_id);
        if (res)
            res->calendar = s->calendar;
        JS_FreeValue(ctx, pt_val);
        return result;
    }
}

static JSValue plaindatetime_difference(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv,
                                         int is_until)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSValue other_val;
    JSPlainDateTimeData *other;
    JSDurationData diff = {0};
    TIUnit largestUnit = TI_UNIT_INVALID;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int64_t roundingIncrement = 1;
    TIRoundingMode roundingMode = ROUND_TRUNC;
    int other_year, other_month, other_day;
    int other_hour, other_minute, other_second;
    int other_ms, other_us, other_ns;

    if (!s)
        return JS_EXCEPTION;
    other_val = js_plaindatetime_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaindatetime_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowTypeError(ctx, "invalid PlainDateTime");
    }
    if (s->calendar != other->calendar) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "calendars must be the same");
    }

    other_year = other->year;
    other_month = other->month;
    other_day = other->day;
    other_hour = other->hour;
    other_minute = other->minute;
    other_second = other->second;
    other_ms = other->millisecond;
    other_us = other->microsecond;
    other_ns = other->nanosecond;

    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0) {
        JS_FreeValue(ctx, other_val);
        return JS_EXCEPTION;
    }

    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_parse_unit_option(ctx, argv[1], "largestUnit", &largestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED | TI_UNIT_OPT_ALLOW_AUTO, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, other_val);
            return JS_EXCEPTION;
        }

        if (ti_get_rounding_increment_option(ctx, argv[1], &roundingIncrement) < 0) {
            JS_FreeValue(ctx, other_val);
            return JS_EXCEPTION;
        }

        if (ti_get_rounding_mode_option_default(ctx, argv[1], &roundingMode, ROUND_TRUNC) < 0) {
            JS_FreeValue(ctx, other_val);
            return JS_EXCEPTION;
        }

        TIUnit su = TI_UNIT_INVALID;
        if (ti_parse_unit_option(ctx, argv[1], "smallestUnit", &su, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, other_val);
            return JS_EXCEPTION;
        }

        if (su != TI_UNIT_INVALID) {
            smallestUnit = su;
        }
    }

    JS_FreeValue(ctx, other_val);

    if (smallestUnit == TI_UNIT_INVALID)
        smallestUnit = TI_UNIT_NANOSECOND;
    if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO)
        largestUnit = (smallestUnit < TI_UNIT_DAY) ? smallestUnit : TI_UNIT_DAY;

    if (largestUnit > smallestUnit) {
        return JS_ThrowRangeError(ctx, "largestUnit must be larger than smallestUnit");
    }

    {
        int64_t maximum = 0;
        switch (smallestUnit) {
        case TI_UNIT_HOUR:        maximum = 24; break;
        case TI_UNIT_MINUTE:      maximum = 60; break;
        case TI_UNIT_SECOND:      maximum = 60; break;
        case TI_UNIT_MILLISECOND: maximum = 1000; break;
        case TI_UNIT_MICROSECOND: maximum = 1000; break;
        case TI_UNIT_NANOSECOND:  maximum = 1000; break;
        default:                  maximum = 0; break;
        }
        if (maximum > 0 && (roundingIncrement >= maximum || maximum % roundingIncrement != 0)) {
            return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
        }
    }

    if (s->calendar != CAL_ISO8601 && s->calendar != CAL_GREGORY &&
        s->calendar != CAL_BUDDHIST && s->calendar != CAL_ROC && s->calendar != CAL_JAPANESE &&
        (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH)) {
        int s_cal_y, s_cal_m, s_cal_d;
        int o_cal_y, o_cal_m, o_cal_d;
        ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &s_cal_y, &s_cal_m, &s_cal_d);
        ti_iso_to_calendar_date(s->calendar, other_year, other_month, other_day, &o_cal_y, &o_cal_m, &o_cal_d);
        ti_calendar_datetime_difference(s->calendar,
            s_cal_y, s_cal_m, s_cal_d, s->hour, s->minute, s->second, s->millisecond, s->microsecond, s->nanosecond,
            o_cal_y, o_cal_m, o_cal_d, other_hour, other_minute, other_second, other_ms, other_us, other_ns,
            largestUnit, &diff);
    } else {
        ti_iso_datetime_difference(s->year, s->month, s->day, s->hour, s->minute, s->second, s->millisecond, s->microsecond, s->nanosecond,
                                    other_year, other_month, other_day, other_hour, other_minute, other_second, other_ms, other_us, other_ns,
                                    largestUnit, &diff);
    }
    if (smallestUnit != TI_UNIT_NANOSECOND || roundingIncrement != 1) {
        if (ti_round_relative_duration_iso(ctx,
                                            s->year, s->month, s->day, s->hour, s->minute, s->second, s->millisecond, s->microsecond, s->nanosecond,
                                            &diff, largestUnit, smallestUnit, roundingIncrement,
                                            is_until ? roundingMode : ti_negate_rounding_mode(roundingMode), FALSE, &diff) < 0)
            return JS_EXCEPTION;
    }
    ti_balance_time_duration(&diff, largestUnit);

    if (!is_until) {
        diff.years = -diff.years;
        diff.months = -diff.months;
        diff.weeks = -diff.weeks;
        diff.days = -diff.days;
        diff.hours = -diff.hours;
        diff.minutes = -diff.minutes;
        diff.seconds = -diff.seconds;
        diff.milliseconds = -diff.milliseconds;
        js_int128_t us = diff.microseconds.int128_value;
        diff.microseconds.int128_value = -us;
        js_int128_t ns = diff.nanoseconds.int128_value;
        diff.nanoseconds.int128_value = -ns;
    }

    return js_create_duration(ctx, &diff);
}



static JSValue js_plaindatetime_toPlainDate(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    if (!s)
        return JS_EXCEPTION;
    return js_create_plaindate(ctx, s->year, s->month, s->day);
}

static JSValue js_plaindatetime_toPlainTime(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSPlainTimeData pt;
    if (!s)
        return JS_EXCEPTION;
    pt.hour = s->hour;
    pt.minute = s->minute;
    pt.second = s->second;
    pt.millisecond = s->millisecond;
    pt.microsecond = s->microsecond;
    pt.nanosecond = s->nanosecond;
    return js_create_plaintime(ctx, &pt);
}

static int64_t pdt_unit_to_nanoseconds(TIUnit unit)
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

static JSValue js_plaindatetime_round(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSValue options;
    const char *smallestUnit = NULL;
    TIRoundingMode mode = ROUND_HALF_EXPAND;
    int64_t roundingIncrement = 1;
    TIUnit unit = TI_UNIT_INVALID;
    js_int128_t total_ns, rounded_ns, unit_ns, inc_ns;
    int64_t days;
    int64_t year;
    int month, day, hour, minute, second, ms, us, ns;
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsUndefined(argv[0]))
        return JS_ThrowTypeError(ctx, "roundTo is required");
    if (!JS_IsString(argv[0]) && !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "roundTo must be an object or string");
    if (JS_IsString(argv[0])) {
        size_t su_len;
        smallestUnit = JS_ToCStringLen(ctx, &su_len, argv[0]);
        if (!smallestUnit)
            return JS_EXCEPTION;
        unit = ti_string_to_unit(smallestUnit, su_len);
        JS_FreeCString(ctx, smallestUnit);
        if (unit == TI_UNIT_INVALID)
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        options = JS_NewObjectProto(ctx, JS_NULL);
        if (JS_IsException(options))
            return JS_EXCEPTION;
    } else {
        options = JS_ToObject(ctx, argv[0]);
        if (JS_IsException(options))
            return JS_EXCEPTION;
    }
    if (ti_get_rounding_increment_option(ctx, options, &roundingIncrement) < 0) {
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    if (ti_get_rounding_mode_option_default(ctx, options, &mode, ROUND_HALF_EXPAND) < 0) {
        JS_FreeValue(ctx, options);
        return JS_EXCEPTION;
    }
    /* Read smallestUnit last */
    if (!JS_IsString(argv[0])) {
        if (ti_parse_unit_option(ctx, options, "smallestUnit", &unit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, options);
            return JS_EXCEPTION;
        }
    }
    if (unit == TI_UNIT_INVALID) {
        JS_FreeValue(ctx, options);
        return JS_ThrowRangeError(ctx, "smallestUnit is required");
    }
    /* PlainDateTime.round only supports day and time units */
    if (unit < TI_UNIT_DAY || unit > TI_UNIT_NANOSECOND) {
        JS_FreeValue(ctx, options);
        return JS_ThrowRangeError(ctx, "invalid smallestUnit");
    }
    JS_FreeValue(ctx, options);
    /* Validate roundingIncrement against per-unit maximum */
    {
        int64_t maximum;
        int inclusive;
        switch (unit) {
        case TI_UNIT_DAY:         maximum = 1; inclusive = 1; break;
        case TI_UNIT_HOUR:        maximum = 24; inclusive = 0; break;
        case TI_UNIT_MINUTE:      maximum = 60; inclusive = 0; break;
        case TI_UNIT_SECOND:      maximum = 60; inclusive = 0; break;
        case TI_UNIT_MILLISECOND: maximum = 1000; inclusive = 0; break;
        case TI_UNIT_MICROSECOND: maximum = 1000; inclusive = 0; break;
        case TI_UNIT_NANOSECOND:  maximum = 1000; inclusive = 0; break;
        default:                  maximum = 0; inclusive = 0; break;
        }
        if ((inclusive ? roundingIncrement > maximum : roundingIncrement >= maximum) || maximum % roundingIncrement != 0) {
            return JS_ThrowRangeError(ctx, "roundingIncrement does not divide evenly into the unit");
        }
    }
    /* Compute total nanoseconds from date-time */
    days = pdt_to_ordinal_days(s->year, s->month, s->day);
    total_ns = (js_int128_t)days * 86400000000000LL +
               (js_int128_t)s->hour * 3600000000000LL +
               (js_int128_t)s->minute * 60000000000LL +
               (js_int128_t)s->second * 1000000000LL +
               (js_int128_t)s->millisecond * 1000000LL +
               (js_int128_t)s->microsecond * 1000LL +
               (js_int128_t)s->nanosecond;
    unit_ns = pdt_unit_to_nanoseconds(unit);
    if (unit_ns <= 0)
        return JS_ThrowRangeError(ctx, "invalid unit");
    inc_ns = unit_ns * roundingIncrement;
    rounded_ns = ti_round_to_increment_as_if_positive128(total_ns, inc_ns, mode);
    /* Convert back to date-time */
    days = (int64_t)(rounded_ns / 86400000000000LL);
    rounded_ns %= 86400000000000LL;
    if (rounded_ns < 0) {
        days--;
        rounded_ns += 86400000000000LL;
    }
    ti_epoch_sec_to_date((days - 719162) * 86400LL, &year, &month, &day, NULL, NULL, NULL);
    hour = (int)(rounded_ns / 3600000000000LL);
    rounded_ns %= 3600000000000LL;
    minute = (int)(rounded_ns / 60000000000LL);
    rounded_ns %= 60000000000LL;
    second = (int)(rounded_ns / 1000000000LL);
    rounded_ns %= 1000000000LL;
    ms = (int)(rounded_ns / 1000000LL);
    rounded_ns %= 1000000LL;
    us = (int)(rounded_ns / 1000LL);
    ns = (int)(rounded_ns % 1000LL);
    return js_create_plaindatetime(ctx, (int)year, month, day, hour, minute, second, ms, us, ns);
}

static JSValue js_plaindatetime_withCalendar(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSPlainDateTimeData *t;
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
    obj = JS_NewObjectClass(ctx, (int)js_plaindatetime_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, t);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, t);
    return obj;
}

static JSValue js_plaindatetime_toZonedDateTime(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSPlainDateTimeData *s = JS_GetOpaque2(ctx, this_val, js_plaindatetime_class_id);
    JSTimeSpec ts;
    char tz_buf[64] = "UTC";
    const char *timeZone = "UTC";
    int64_t tz_offset_ns = 0;
    int has_tz = 0;
    int unsupported_tz = 0;
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsString(argv[0])) {
        const char *str = JS_ToCString(ctx, argv[0]);
        if (!str)
            return JS_EXCEPTION;
        {
            int64_t offset_ns = 0;
            int ret = ti_parse_temporal_timezone_string(str, &offset_ns);
            if (ret < 0) {
                JS_FreeCString(ctx, str);
                return JS_ThrowRangeError(ctx, "invalid time zone");
            }
            if (ti_canonicalize_timezone_identifier(str, offset_ns, tz_buf, sizeof(tz_buf)) < 0) {
                unsupported_tz = 1;
            } else {
                tz_offset_ns = offset_ns;
                timeZone = tz_buf;
            }
        }
        JS_FreeCString(ctx, str);
        has_tz = 1;
    } else if (JS_IsObject(argv[0])) {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], "timeZone");
        if (JS_IsException(v)) {
            JS_FreeValue(ctx, v);
            return JS_EXCEPTION;
        }
        if (JS_IsUndefined(v)) {
            JS_FreeValue(ctx, v);
            return JS_ThrowTypeError(ctx, "unsupported time zone argument");
        }
        if (JS_IsString(v)) {
            const char *str = JS_ToCString(ctx, v);
            if (!str) {
                JS_FreeValue(ctx, v);
                return JS_EXCEPTION;
            }
            {
                int64_t offset_ns = 0;
                int ret = ti_parse_temporal_timezone_string(str, &offset_ns);
                if (ret < 0) {
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, v);
                    return JS_ThrowRangeError(ctx, "invalid time zone");
                }
                if (ti_canonicalize_timezone_identifier(str, offset_ns, tz_buf, sizeof(tz_buf)) < 0) {
                    unsupported_tz = 1;
                } else {
                    tz_offset_ns = offset_ns;
                    timeZone = tz_buf;
                }
            }
            JS_FreeCString(ctx, str);
            has_tz = 1;
        }
        JS_FreeValue(ctx, v);
        if (!has_tz)
            return JS_ThrowTypeError(ctx, "unsupported time zone argument");
    } else {
        return JS_ThrowTypeError(ctx, "unsupported time zone argument");
    }
    {
        int dis_idx = 2; /* default compatible */
        if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
            return JS_EXCEPTION;
        if (argc >= 2 && JS_IsObject(argv[1])) {
            static const char *dis_values[] = { "earlier", "later", "compatible", "reject" };
            dis_idx = ti_get_string_option(ctx, argv[1], "disambiguation", dis_values, 4, 2);
            if (dis_idx < 0)
                return JS_EXCEPTION;
        }
        if (unsupported_tz)
            return JS_ThrowRangeError(ctx, "unsupported time zone");
        {
            int64_t days = pdt_to_ordinal_days(s->year, s->month, s->day);
            int64_t epoch_days = days - 719162;
            int64_t epoch_sec = epoch_days * 86400LL;
            epoch_sec += s->hour * 3600LL + s->minute * 60LL + s->second;
            ts.secs = epoch_sec;
            ts.nsecs = (uint32_t)(s->millisecond * 1000000 + s->microsecond * 1000 + s->nanosecond);
        }
        {
            int tz_id = ti_tz_find_id(timeZone);
            if (tz_id >= 0) {
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, ts.secs, dis_idx, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                ts.secs = utc_sec;
            } else {
                int64_t offset_sec = tz_offset_ns / 1000000000LL;
                int offset_nsec = (int)(tz_offset_ns % 1000000000LL);
                ts.secs -= offset_sec;
                {
                    int64_t new_nsec = (int64_t)ts.nsecs - offset_nsec;
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
        return js_create_zoneddatetime(ctx, &ts, timeZone, s->calendar);
    }
}

/* Temporal.PlainDateTime.from */
static JSValue js_plaindatetime_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)this_val;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    int millisecond = 0, microsecond = 0, nanosecond = 0;
    JSValue val;
    int overflow_constrain = 1; /* default is constrain */
    val = argv[0];
    if (JS_IsObject(val)) {
        JSPlainDateTimeData *s = JS_GetOpaque(val, js_plaindatetime_class_id);
        if (s) {
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return js_create_plaindatetime_with_calendar(ctx, s->year, s->month, s->day,
                                            s->hour, s->minute, s->second,
                                            s->millisecond, s->microsecond, s->nanosecond, s->calendar);
        }
        {
            JSPlainDateData *pd = JS_GetOpaque(val, js_plaindate_class_id);
            if (pd) {
                if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                    return JS_EXCEPTION;
                if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                    return JS_EXCEPTION;
                return js_create_plaindatetime_with_calendar(ctx, pd->year, pd->month, pd->day,
                                                0, 0, 0, 0, 0, 0, pd->calendar);
            }
        }
        {
            JSZonedDateTimeData *zdt = JS_GetOpaque(val, js_zoneddatetime_class_id);
            if (zdt) {
                int64_t epoch_sec = zdt->ts.secs;
                int epoch_nsec = (int)zdt->ts.nsecs;
                {
                    int64_t offset_ns = 0;
                    int ret = ti_parse_temporal_timezone_string(zdt->timeZone, &offset_ns);
                    if (ret == 0) {
                        int64_t offset_sec = offset_ns / 1000000000LL;
                        int offset_nsec = (int)(offset_ns % 1000000000LL);
                        epoch_sec += offset_sec;
                        epoch_nsec += offset_nsec;
                        if (epoch_nsec >= 1000000000) {
                            epoch_nsec -= 1000000000;
                            epoch_sec += 1;
                        } else if (epoch_nsec < 0) {
                            epoch_nsec += 1000000000;
                            epoch_sec -= 1;
                        }
                    }
                }
                {
                    int64_t y;
                    int mo, d, h, mi, se;
                    ti_epoch_sec_to_date(epoch_sec, &y, &mo, &d, &h, &mi, &se);
                    int ms = epoch_nsec / 1000000;
                    int us = (epoch_nsec % 1000000) / 1000;
                    int ns = epoch_nsec % 1000;
                    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                        return JS_EXCEPTION;
                    if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                        return JS_EXCEPTION;
                    return js_create_plaindatetime(ctx, (int)y, mo, d, h, mi, se, ms, us, ns);
                }
            }
        }
        {
            TIPreparedFields fields;
            uint32_t field_mask = TI_FIELD_YEAR | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                  TI_FIELD_DAY | TI_FIELD_HOUR | TI_FIELD_MINUTE |
                                  TI_FIELD_SECOND | TI_FIELD_MILLISECOND | TI_FIELD_MICROSECOND |
                                  TI_FIELD_NANOSECOND;
            CalendarType cal = CAL_ISO8601;
            if (ti_resolve_calendar_from_object(ctx, val, &cal) < 0)
                return JS_EXCEPTION;
            if (ti_prepare_calendar_fields(ctx, val, field_mask, 0, 0, &fields, cal) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return ti_create_plaindatetime_from_fields(ctx, &fields, overflow_constrain);
        }
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        TISODateTimeParseResult r;
        if (!str)
            return JS_EXCEPTION;
        if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime string");
        }
        JS_FreeCString(ctx, str);
        if (!r.has_date)
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime string");
        if (r.has_utc)
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime string");
        if (r.has_tz_offset && !r.has_time)
            return JS_ThrowRangeError(ctx, "invalid PlainDateTime string");
        {
            CalendarType cal = r.has_calendar ? r.calendar : CAL_ISO8601;
            if (cal == CAL_UNKNOWN)
                return JS_ThrowRangeError(ctx, "invalid calendar ID");
            year = r.year;
            month = r.month;
            day = r.day;
            hour = r.hour;
            minute = r.minute;
            second = r.second;
            millisecond = r.millisecond;
            microsecond = r.microsecond;
            nanosecond = r.nanosecond;
            if (!pdt_is_valid_date(year, month, day))
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
            if (!pdt_is_valid_time(hour, minute, second, millisecond, microsecond, nanosecond))
                return JS_ThrowRangeError(ctx, "invalid PlainDateTime");
            /* Validate options AFTER string parsing */
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return js_create_plaindatetime_with_calendar(ctx, year, month, day, hour, minute, second,
                                            millisecond, microsecond, nanosecond, cal);
        }
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.PlainDateTime.from");
}

static const JSCFunctionListEntry js_plaindatetime_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("year", js_plaindatetime_get_field, NULL, PLAINDATETIME_YEAR),
    JS_CGETSET_MAGIC_DEF("month", js_plaindatetime_get_field, NULL, PLAINDATETIME_MONTH),
    JS_CGETSET_DEF("monthCode", js_plaindatetime_get_monthCode, NULL),
    JS_CGETSET_MAGIC_DEF("day", js_plaindatetime_get_field, NULL, PLAINDATETIME_DAY),
    JS_CGETSET_MAGIC_DEF("hour", js_plaindatetime_get_field, NULL, PLAINDATETIME_HOUR),
    JS_CGETSET_MAGIC_DEF("minute", js_plaindatetime_get_field, NULL, PLAINDATETIME_MINUTE),
    JS_CGETSET_MAGIC_DEF("second", js_plaindatetime_get_field, NULL, PLAINDATETIME_SECOND),
    JS_CGETSET_MAGIC_DEF("millisecond", js_plaindatetime_get_field, NULL, PLAINDATETIME_MILLISECOND),
    JS_CGETSET_MAGIC_DEF("microsecond", js_plaindatetime_get_field, NULL, PLAINDATETIME_MICROSECOND),
    JS_CGETSET_MAGIC_DEF("nanosecond", js_plaindatetime_get_field, NULL, PLAINDATETIME_NANOSECOND),
    JS_CGETSET_DEF("calendarId", js_plaindatetime_get_calendarId, NULL),
    JS_CGETSET_DEF("era", js_plaindatetime_get_era, NULL),
    JS_CGETSET_DEF("eraYear", js_plaindatetime_get_eraYear, NULL),
    JS_CGETSET_DEF("inLeapYear", js_plaindatetime_get_inLeapYear, NULL),
    JS_CGETSET_DEF("daysInMonth", js_plaindatetime_get_daysInMonth, NULL),
    JS_CGETSET_DEF("daysInYear", js_plaindatetime_get_daysInYear, NULL),
    JS_CGETSET_DEF("daysInWeek", js_plaindatetime_get_daysInWeek, NULL),
    JS_CGETSET_DEF("monthsInYear", js_plaindatetime_get_monthsInYear, NULL),
    JS_CGETSET_DEF("dayOfYear", js_plaindatetime_get_dayOfYear, NULL),
    JS_CGETSET_DEF("dayOfWeek", js_plaindatetime_get_dayOfWeek, NULL),
    JS_CGETSET_DEF("weekOfYear", js_plaindatetime_get_weekOfYear, NULL),
    JS_CGETSET_DEF("yearOfWeek", js_plaindatetime_get_yearOfWeek, NULL),
    JS_CFUNC_DEF("equals", 1, js_plaindatetime_equals),
    JS_CFUNC_DEF("toString", 0, js_plaindatetime_toString),
    JS_CFUNC_DEF("toJSON", 0, js_plaindatetime_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_plaindatetime_toLocaleString),
    JS_CFUNC_MAGIC_DEF("add", 1, js_plaindatetime_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_plaindatetime_add_subtract, 0),
    JS_CFUNC_MAGIC_DEF("until", 1, plaindatetime_difference, 1),
    JS_CFUNC_MAGIC_DEF("since", 1, plaindatetime_difference, 0),
    JS_CFUNC_DEF("round", 1, js_plaindatetime_round),
    JS_CFUNC_DEF("toPlainDate", 0, js_plaindatetime_toPlainDate),
    JS_CFUNC_DEF("toPlainTime", 0, js_plaindatetime_toPlainTime),
    JS_CFUNC_DEF("toZonedDateTime", 1, js_plaindatetime_toZonedDateTime),
    JS_CFUNC_DEF("with", 1, js_plaindatetime_with),
    JS_CFUNC_DEF("withCalendar", 1, js_plaindatetime_withCalendar),
    JS_CFUNC_DEF("withPlainTime", 0, js_plaindatetime_withPlainTime),
    JS_CFUNC_DEF("valueOf", 0, js_plaindatetime_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainDateTime", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_plaindatetime_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_plaindatetime_from),
    JS_CFUNC_DEF("compare", 2, js_plaindatetime_compare),
};

int js_add_plaindatetime(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_plaindatetime_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_plaindatetime_class_id, &js_plaindatetime_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_plaindatetime_class_id, "PlainDateTime",
                               (JSCFunction *)js_plaindatetime_constructor, 3,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_plaindatetime_static_funcs, countof(js_plaindatetime_static_funcs),
                               js_plaindatetime_proto_funcs, countof(js_plaindatetime_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "PlainDateTime", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
