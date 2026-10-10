/*
 * QuickJS Temporal.PlainYearMonth implementation (minimal)
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

JSClassID js_plainyearmonth_class_id;

static void js_plainyearmonth_finalizer(JSRuntime *rt, JSValue val)
{
    JSPlainYearMonthData *s = JS_GetOpaque(val, js_plainyearmonth_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_plainyearmonth_class = {
    "Temporal.PlainYearMonth",
    .finalizer = js_plainyearmonth_finalizer,
};

static JSValue js_create_plainyearmonth(JSContext *ctx, int year, int month, int referenceISODay)
{
    return js_create_plainyearmonth_with_calendar(ctx, year, month, referenceISODay, CAL_ISO8601);
}

JSValue js_create_plainyearmonth_with_calendar(JSContext *ctx, int year, int month, int referenceISODay, CalendarType calendar)
{
    JSPlainYearMonthData *s;
    JSValue obj;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    s->iso_year = year;
    s->iso_month = month;
    s->iso_day = referenceISODay;
    s->calendar = calendar;
    obj = JS_NewObjectClass(ctx, (int)js_plainyearmonth_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

static JSValue ti_create_plainyearmonth_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                              int overflow_constrain)
{
    int year, month, dummy_day = 1;
    JSValue obj;
    JSPlainYearMonthData *s;

    if (ti_resolve_date_from_fields(ctx, fields, overflow_constrain, &year, &month, &dummy_day, 0) < 0)
        return JS_EXCEPTION;

    /* Constrain/reject leap monthCode if year doesn't have the leap month */
    if (fields->calendar != CAL_ISO8601 && fields->calendar != CAL_GREGORY &&
        (fields->present_mask & TI_FIELD_MONTH_CODE) && fields->month_code_has_l) {
        int result_mc, result_has_l;
        ti_calendar_month_to_month_code(fields->calendar, year, month, &result_mc, &result_has_l);
        if (result_has_l != fields->month_code_has_l) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            /* For Chinese/Dangi, fall back to regular month; Hebrew already resolved correctly */
            if (fields->calendar == CAL_CHINESE || fields->calendar == CAL_DANGI)
                month = fields->month_from_code;
        }
    }

    if (fields->calendar == CAL_ISO8601 || fields->calendar == CAL_GREGORY) {
        int is_valid = month >= 1 && month <= 12 &&
                       ((year > -271821 || (year == -271821 && month >= 4)) &&
                        (year < 275760 || (year == 275760 && month <= 9)));
        if (!is_valid)
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    }

    {
        int iso_d = 1;
        /* Convert calendar date to ISO for all non-ISO/non-Gregory calendars */
        if (fields->calendar != CAL_ISO8601 && fields->calendar != CAL_GREGORY) {
            int iso_y, iso_m;
            ti_calendar_date_to_iso(fields->calendar, year, month, 1, &iso_y, &iso_m, &iso_d);
            year = iso_y;
            month = iso_m;
        }
        {
            int is_valid = month >= 1 && month <= 12 &&
                           ((year > -271821 || (year == -271821 && month >= 4)) &&
                            (year < 275760 || (year == 275760 && month <= 9)));
            if (!is_valid)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
        }
        obj = js_create_plainyearmonth(ctx, year, month, iso_d);
    }
    if (!JS_IsException(obj)) {
        s = JS_GetOpaque(obj, js_plainyearmonth_class_id);
        if (s) {
            s->calendar = fields->calendar;
        }
    }
    return obj;
}

static int pym_is_valid_year_month(int year, int month)
{
    return month >= 1 && month <= 12 &&
           ((year > -271821 || (year == -271821 && month >= 4)) &&
            (year < 275760 || (year == 275760 && month <= 9)));
}

static int pym_to_integer_with_truncation(JSContext *ctx, JSValueConst val, int *pres)
{
    double d;
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

/* Temporal.PlainYearMonth constructor */
static JSValue js_plainyearmonth_constructor(JSContext *ctx, JSValueConst new_target,
                                              int argc, JSValueConst *argv)
{
    int year, month, day = 1;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.PlainYearMonth constructor cannot be called as a function");
    }
    if (pym_to_integer_with_truncation(ctx, argv[0], &year) < 0)
        return JS_EXCEPTION;
    if (pym_to_integer_with_truncation(ctx, argv[1], &month) < 0)
        return JS_EXCEPTION;
    if (!pym_is_valid_year_month(year, month))
        return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    if (year < -271821 || (year == -271821 && month < 4))
        return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    if (year > 275760 || (year == 275760 && month > 9))
        return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    {
        CalendarType calendar = CAL_ISO8601;
        if (argc > 2) {
            int cal_res = ti_validate_constructor_calendar(ctx, argv[2], &calendar);
            if (cal_res < 0)
                return JS_EXCEPTION;
        }
        if (argc > 3 && !JS_IsUndefined(argv[3])) {
            if (pym_to_integer_with_truncation(ctx, argv[3], &day) < 0)
                return JS_EXCEPTION;
        }
        if (day < 1 || day > pd_days_in_month(year, month))
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
        if (argc <= 3 || JS_IsUndefined(argv[3]))
            day = 1;
        {
            JSPlainYearMonthData *s = js_mallocz(ctx, sizeof(*s));
            JSValue obj;
            if (!s)
                return JS_EXCEPTION;
            s->iso_year = year;
            s->iso_month = month;
            s->iso_day = day;
            s->calendar = calendar;
            obj = js_create_temporal_object(ctx, new_target, js_plainyearmonth_class_id, s);
            if (JS_IsException(obj)) {
                js_free(ctx, s);
                return JS_EXCEPTION;
            }
            return obj;
        }
    }
}

/* Getters */
static JSValue js_plainyearmonth_get_year(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, cal_y);
}

static JSValue js_plainyearmonth_get_month(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, cal_m);
}

static JSValue js_plainyearmonth_get_monthCode(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return ti_get_month_code(ctx, s->calendar, cal_y, cal_m);
}

static JSValue js_plainyearmonth_get_calendarId(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar));
}

static JSValue js_plainyearmonth_get_daysInMonth(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_month(s->calendar, cal_y, cal_m));
}

static JSValue js_plainyearmonth_get_daysInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_year(s->calendar, cal_y));
}

static JSValue js_plainyearmonth_get_inLeapYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewBool(ctx, ti_calendar_is_leap_year(s->calendar, cal_y));
}

static JSValue js_plainyearmonth_get_monthsInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_months_in_year(s->calendar, cal_y));
    return JS_NewInt32(ctx, 12);
}

static JSValue js_plainyearmonth_get_referenceISODay(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, s->iso_day);
}

static int pym_day_of_week(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 2;
    int d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
    return (d == 0) ? 7 : d;
}

static void pym_iso_week(int year, int month, int day, int *weekYear, int *weekNumber)
{
    int dow = pym_day_of_week(year, month, day);
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

static JSValue js_plainyearmonth_get_era(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    TIEra era;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    int dummy_era_year;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->iso_year, s->iso_month, s->iso_day,
                                  cal_y, &era, &dummy_era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewString(ctx, ti_era_to_string(era));
    return JS_UNDEFINED;
}

static JSValue js_plainyearmonth_get_eraYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    TIEra era;
    int era_year;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->iso_year, s->iso_month, s->iso_day,
                                  cal_y, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewInt32(ctx, era_year);
    return JS_UNDEFINED;
}

static JSValue js_plainyearmonth_get_daysInWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, 7);
}

static JSValue js_plainyearmonth_get_dayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, pym_day_of_week(s->iso_year, s->iso_month, s->iso_day));
}

static JSValue js_plainyearmonth_get_dayOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_day_of_year(s->calendar, cal_y, cal_m, cal_d));
}

static JSValue js_plainyearmonth_get_weekOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pym_iso_week(s->iso_year, s->iso_month, s->iso_day, &weekYear, &weekNumber);
    return JS_NewInt32(ctx, weekNumber);
}

static JSValue js_plainyearmonth_get_yearOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pym_iso_week(s->iso_year, s->iso_month, s->iso_day, &weekYear, &weekNumber);
    return JS_NewInt64(ctx, weekYear);
}

static JSValue js_plainyearmonth_from(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv);

/* Temporal.PlainYearMonth.prototype.equals */
static JSValue js_plainyearmonth_equals(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSPlainYearMonthData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plainyearmonth_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plainyearmonth_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    int eq = s->iso_year == other->iso_year && s->iso_month == other->iso_month && s->iso_day == other->iso_day &&
             s->calendar == other->calendar;
    JS_FreeValue(ctx, other_val);
    return JS_NewBool(ctx, eq);
}

/* Temporal.PlainYearMonth.compare */
static JSValue js_plainyearmonth_compare(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    JSPlainYearMonthData *s1, *s2;
    JSValue v1, v2;
    int cmp;
    v1 = js_plainyearmonth_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(v1))
        return JS_EXCEPTION;
    v2 = js_plainyearmonth_from(ctx, JS_UNDEFINED, 1, argv + 1);
    if (JS_IsException(v2)) {
        JS_FreeValue(ctx, v1);
        return JS_EXCEPTION;
    }
    s1 = JS_GetOpaque(v1, js_plainyearmonth_class_id);
    s2 = JS_GetOpaque(v2, js_plainyearmonth_class_id);
    if (!s1 || !s2) {
        JS_FreeValue(ctx, v1);
        JS_FreeValue(ctx, v2);
        return JS_ThrowTypeError(ctx, "compare arguments must be PlainYearMonth objects");
    }
    if (s1->iso_year != s2->iso_year)
        cmp = s1->iso_year < s2->iso_year ? -1 : 1;
    else if (s1->iso_month != s2->iso_month)
        cmp = s1->iso_month < s2->iso_month ? -1 : 1;
    else if (s1->iso_day != s2->iso_day)
        cmp = s1->iso_day < s2->iso_day ? -1 : 1;
    else
        cmp = 0;
    JS_FreeValue(ctx, v1);
    JS_FreeValue(ctx, v2);
    return JS_NewInt32(ctx, cmp);
}

/* Format PlainYearMonth as ISO string */
static JSValue js_plainyearmonth_toString(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    JSPlainYearMonthData *s;
    char buf[64];
    int calendarName = 0; /* 0=auto/never, 1=always, 2=critical */
    if (argc >= 1 && ti_validate_options_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (argc > 0 && !JS_IsUndefined(argv[0]) && !JS_IsNull(argv[0])) {
        if (ti_get_calendar_name_option(ctx, argv[0], &calendarName) < 0)
            return JS_EXCEPTION;
    }
    if (s->iso_year < 0)
        snprintf(buf, sizeof(buf), "-%06d-%02d", -s->iso_year, s->iso_month);
    else if (s->iso_year >= 10000)
        snprintf(buf, sizeof(buf), "+%06d-%02d", s->iso_year, s->iso_month);
    else
        snprintf(buf, sizeof(buf), "%04d-%02d", s->iso_year, s->iso_month);
    if (calendarName == 1 || calendarName == 2 || s->calendar != CAL_ISO8601) {
        char daybuf[16];
        snprintf(daybuf, sizeof(daybuf), "-%02d", s->iso_day);
        pstrcat(buf, sizeof(buf), daybuf);
    }
    ti_append_calendar_annotation(buf, sizeof(buf), s->calendar, calendarName);
    return JS_NewString(ctx, buf);
}

static JSValue js_plainyearmonth_toJSON(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_plainyearmonth_toString(ctx, this_val, 0, NULL);
}

static JSValue js_plainyearmonth_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                                 int argc, JSValueConst *argv)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 1, js_plainyearmonth_toString);
}

static JSValue js_plainyearmonth_valueOf(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.PlainYearMonth with < or >");
}

/* Temporal.PlainYearMonth.prototype.with */
static JSValue js_plainyearmonth_with(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JSPlainYearMonthData *s;
    int cal_year, cal_month;
    int orig_cal_year, orig_cal_month, orig_cal_day;
    int has_year_prop = 0;
    int has_month_code = 0;
    int month_code_val = 0;
    int has_month_code_l = 0;
    int has_month_prop = 0;
    int overflow_constrain = 1;
    TIPreparedFields fields;
    s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    if (!s)
        return JS_EXCEPTION;
    pym_get_calendar_date(s, &orig_cal_year, &orig_cal_month, &orig_cal_day);
    cal_year = orig_cal_year;
    cal_month = orig_cal_month;
    if (ti_is_partial_temporal_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, argv[0],
                                    TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                    TI_FIELD_YEAR,
                                    0, 1, &fields, s->calendar) < 0)
        return JS_EXCEPTION;
    /* Merge fields with existing object in calendar space */
    if (fields.present_mask & TI_FIELD_MONTH) {
        has_month_prop = 1;
        cal_month = fields.month;
    }
    if (fields.present_mask & TI_FIELD_MONTH_CODE) {
        has_month_code = 1;
        month_code_val = fields.month_from_code;
        has_month_code_l = fields.month_code_has_l;
    }
    if (fields.present_mask & TI_FIELD_YEAR) {
        has_year_prop = 1;
        cal_year = (int)fields.year;
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
            int era_cal_year;
            if (fields.era == TI_ERA_UNKNOWN || ti_calendar_era_to_calendar_year(s->calendar, fields.era, (int)fields.era_year, &era_cal_year) < 0)
                return JS_ThrowRangeError(ctx, "invalid era");
            if (has_year_prop && cal_year != era_cal_year)
                return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
            cal_year = era_cal_year;
        }
    }
    /* monthCode processing */
    if (has_month_code) {
        if (!ti_calendar_validate_month_code(s->calendar, month_code_val, has_month_code_l))
            return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
    }
    if (has_month_code && has_month_prop) {
        int resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, month_code_val, has_month_code_l);
        if (resolved_month >= 0 && resolved_month != cal_month)
            return JS_ThrowRangeError(ctx, "month and monthCode must agree");
    }
    if (has_month_code) {
        int resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, month_code_val, has_month_code_l);
        if (resolved_month < 0)
            return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
        cal_month = resolved_month;
    }
    /* If year changed but month not specified, recompute original monthCode */
    if (!has_month_code && !has_month_prop && cal_year != orig_cal_year) {
        int orig_mc, orig_has_l;
        int result_mc, result_has_l;
        ti_calendar_month_to_month_code(s->calendar, orig_cal_year, orig_cal_month, &orig_mc, &orig_has_l);
        int resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, orig_has_l);
        if (resolved_month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            resolved_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, 0);
        }
        cal_month = resolved_month;
        ti_calendar_month_to_month_code(s->calendar, cal_year, cal_month, &result_mc, &result_has_l);
        if (!overflow_constrain && (result_mc != orig_mc || result_has_l != orig_has_l))
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    }
    /* Validate month */
    {
        int max_month = ti_calendar_months_in_year(s->calendar, cal_year);
        if (cal_month < 1 || cal_month > max_month) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            if (cal_month > max_month)
                cal_month = max_month;
            else if (cal_month < 1)
                cal_month = 1;
        }
    }
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, cal_year, cal_month, 1, &iso_y, &iso_m, &iso_d);
        return js_create_plainyearmonth_with_calendar(ctx, iso_y, iso_m, iso_d, s->calendar);
    }
}

static void pym_balance_month(int *year, int *month, CalendarType cal)
{
    int max_month;
    while (1) {
        max_month = ti_calendar_months_in_year(cal, *year);
        if (*month <= max_month && *month >= 1)
            break;
        if (*month > max_month) {
            *month -= max_month;
            (*year)++;
        } else {
            (*year)--;
            max_month = ti_calendar_months_in_year(cal, *year);
            *month += max_month;
        }
    }
}

static int pym_duration_has_lower_units(const JSDurationData *d)
{
    return d->weeks != 0 || d->days != 0 || d->hours != 0 || d->minutes != 0 ||
           d->seconds != 0 || d->milliseconds != 0 || d->microseconds.int128_value != 0 || d->nanoseconds.int128_value != 0;
}

static JSValue js_plainyearmonth_add_subtract(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv, int is_add)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    JSDurationData d = {0};
    int cal_year, cal_month, cal_day;
    int overflow_constrain = 1;
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
    if (duration_has_mixed_signs(&d))
        return JS_ThrowRangeError(ctx, "mixed-sign duration not allowed");
    if (duration_is_out_of_range(&d))
        return JS_ThrowRangeError(ctx, "duration out of range");
    if (pym_duration_has_lower_units(&d))
        return JS_ThrowRangeError(ctx, "duration contains units smaller than months");
    /* Convert ISO to calendar space for arithmetic */
    pym_get_calendar_date(s, &cal_year, &cal_month, &cal_day);
    /* Check that base date is within ISO limits */
    if (s->iso_year < -271821 || (s->iso_year == -271821 && (s->iso_month < 4 || (s->iso_month == 4 && s->iso_day < 19))))
        return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    if (s->iso_year > 275760 || (s->iso_year == 275760 && (s->iso_month > 9 || (s->iso_month == 9 && s->iso_day > 13))))
        return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
    {
        int orig_mc = 0, orig_has_l = 0;
        ti_calendar_month_to_month_code(s->calendar, cal_year, cal_month, &orig_mc, &orig_has_l);
        cal_year += sign * (int)d.years;
        cal_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, orig_has_l);
        if (cal_month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            cal_month = ti_calendar_month_code_to_month(s->calendar, cal_year, orig_mc, 0);
        } else if (!overflow_constrain) {
            int result_mc, result_has_l;
            ti_calendar_month_to_month_code(s->calendar, cal_year, cal_month, &result_mc, &result_has_l);
            if (result_mc != orig_mc || result_has_l != orig_has_l)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
        }
    }
    cal_month += sign * (int)d.months;
    pym_balance_month(&cal_year, &cal_month, s->calendar);
    {
        int dim = ti_calendar_days_in_month(s->calendar, cal_year, cal_month);
        if (cal_day > dim) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            cal_day = dim;
        }
    }
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, cal_year, cal_month, cal_day, &iso_y, &iso_m, &iso_d);
        if (iso_y < -271821 || (iso_y == -271821 && iso_m < 4))
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
        if (iso_y > 275760 || (iso_y == 275760 && iso_m > 9))
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
        return js_create_plainyearmonth_with_calendar(ctx, iso_y, iso_m, iso_d, s->calendar);
    }
}

static JSValue js_plainyearmonth_difference(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv, int is_until)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    JSValue other_val;
    JSPlainYearMonthData *other;
    int64_t total_months;
    JSDurationData d = {0};
    TIUnit largestUnit = TI_UNIT_INVALID;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int64_t roundingIncrement = 1;
    TIRoundingMode roundingMode = ROUND_TRUNC;
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plainyearmonth_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plainyearmonth_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowTypeError(ctx, "invalid PlainYearMonth");
    }
    if (s->calendar != other->calendar) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "calendars must be the same");
    }
    int other_iso_year = other->iso_year;
    int other_iso_month = other->iso_month;
    int other_iso_day = other->iso_day;
    JS_FreeValue(ctx, other_val);
    {
        int s_cal_y, s_cal_m, s_cal_d;
        int o_cal_y, o_cal_m, o_cal_d;
        pym_get_calendar_date(s, &s_cal_y, &s_cal_m, &s_cal_d);
        ti_iso_to_calendar_date(s->calendar, other_iso_year, other_iso_month, other_iso_day, &o_cal_y, &o_cal_m, &o_cal_d);
        total_months = (int64_t)((js_int128_t)(o_cal_y - s_cal_y) * 12 + (o_cal_m - s_cal_m));
        if (total_months != 0) {
            int64_t days1 = pdt_to_ordinal_days(s->iso_year, s->iso_month, 1);
            int64_t days2 = pdt_to_ordinal_days(other_iso_year, other_iso_month, 1);
            int64_t epoch_days1 = days1 - 719162;
            int64_t epoch_days2 = days2 - 719162;
            if (epoch_days1 < -100000001LL || epoch_days1 > 100000001LL ||
                epoch_days2 < -100000001LL || epoch_days2 > 100000001LL)
                return JS_ThrowRangeError(ctx, "outside representable range");
        }
    }
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
        /* Validate unit restrictions */
        if (largestUnit != TI_UNIT_INVALID && largestUnit != TI_UNIT_AUTO &&
            largestUnit != TI_UNIT_YEAR && largestUnit != TI_UNIT_MONTH) {
            return JS_ThrowRangeError(ctx, "invalid largestUnit");
        }
        if (smallestUnit != TI_UNIT_INVALID && smallestUnit != TI_UNIT_YEAR &&
            smallestUnit != TI_UNIT_MONTH) {
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        }
    }
    if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO)
        largestUnit = TI_UNIT_YEAR;
    if (smallestUnit == TI_UNIT_INVALID)
        smallestUnit = TI_UNIT_MONTH;
    if (smallestUnit < largestUnit) {
        return JS_ThrowRangeError(ctx, "invalid unit combination");
    }
    {
        int s_cal_y, s_cal_m, s_cal_d;
        int o_cal_y, o_cal_m, o_cal_d;
        int s_mc, s_has_l, o_mc, o_has_l;
        pym_get_calendar_date(s, &s_cal_y, &s_cal_m, &s_cal_d);
        ti_iso_to_calendar_date(s->calendar, other_iso_year, other_iso_month, other_iso_day, &o_cal_y, &o_cal_m, &o_cal_d);
        ti_calendar_month_to_month_code(s->calendar, s_cal_y, s_cal_m, &s_mc, &s_has_l);
        ti_calendar_month_to_month_code(s->calendar, o_cal_y, o_cal_m, &o_mc, &o_has_l);
        if ((largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) &&
            (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI || s->calendar == CAL_HEBREW)) {
            ti_calendar_date_until_with_monthcode(s->calendar,
                s_cal_y, s_cal_m, 1, s_mc, 1, s_has_l,
                o_cal_y, o_cal_m, 1, o_mc, 1, o_has_l,
                largestUnit,
                &d.years, &d.months, NULL, NULL);
        } else if (s->calendar != CAL_ISO8601 && s->calendar != CAL_GREGORY) {
            ti_calendar_date_until(s->calendar,
                s_cal_y, s_cal_m, 1,
                o_cal_y, o_cal_m, 1,
                largestUnit,
                &d.years, &d.months, NULL, NULL);
        } else {
            ti_iso_calendar_date_until(s->iso_year, s->iso_month, 1,
                                        other_iso_year, other_iso_month, 1,
                                        largestUnit,
                                        &d.years, &d.months, NULL, NULL);
        }
        if (smallestUnit != TI_UNIT_MONTH || roundingIncrement != 1) {
            TIRoundingMode rm = is_until ? roundingMode : ti_negate_rounding_mode(roundingMode);
            if (ti_round_iso_date_duration(ctx,
                                            s->iso_year, s->iso_month, 1,
                                            other_iso_year, other_iso_month, 1,
                                            0, 0,
                                            d.years, d.months, 0, 0,
                                            largestUnit, smallestUnit,
                                            roundingIncrement, rm,
                                            1,
                                            FALSE,
                                            &d.years, &d.months, &d.weeks, &d.days) < 0)
                return JS_EXCEPTION;
        }
    }
    if (!is_until) {
        d.years = -d.years;
        d.months = -d.months;
    }
    return js_create_duration(ctx, &d);
}

static JSValue js_plainyearmonth_toPlainDate(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSPlainYearMonthData *s = JS_GetOpaque2(ctx, this_val, js_plainyearmonth_class_id);
    int day;
    int overflow_constrain = 1;
    if (!s)
        return JS_EXCEPTION;
    if (!JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "toPlainDate argument must be an object");
    /* Read options if provided */
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
    }
    {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], "day");
        if (JS_IsUndefined(v)) {
            return JS_ThrowTypeError(ctx, "missing required property day");
        }
        {
            double d;
            if (JS_ToFloat64(ctx, &d, v) < 0) {
                JS_FreeValue(ctx, v);
                return JS_EXCEPTION;
            }
            JS_FreeValue(ctx, v);
            if (!isfinite(d))
                return JS_ThrowRangeError(ctx, "invalid day");
            day = (int)d;
        }
    }
    {
        int cal_y, cal_m, cal_d;
        int iso_y, iso_m, iso_d;
        pym_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
        {
            int dim = ti_calendar_days_in_month(s->calendar, cal_y, cal_m);
            if (day < 1 || day > dim) {
                if (!overflow_constrain)
                    return JS_ThrowRangeError(ctx, "invalid PlainDate");
                if (day > dim)
                    day = dim;
                else if (day < 1)
                    day = 1;
            }
        }
        ti_calendar_date_to_iso(s->calendar, cal_y, cal_m, day, &iso_y, &iso_m, &iso_d);
        return js_create_plaindate_with_calendar(ctx, iso_y, iso_m, iso_d, s->calendar);
    }
}

/* Temporal.PlainYearMonth.from */
static JSValue js_plainyearmonth_from(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    JSValue val;
    int overflow_constrain = 1; /* default is constrain */
    val = argv[0];
    if (JS_IsObject(val)) {
        JSPlainYearMonthData *s = JS_GetOpaque(val, js_plainyearmonth_class_id);
        if (s) {
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return js_create_plainyearmonth_with_calendar(ctx, s->iso_year, s->iso_month, s->iso_day, s->calendar);
        }
        {
            TIPreparedFields fields;
            uint32_t field_mask = TI_FIELD_YEAR | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE;
            CalendarType cal = CAL_ISO8601;
            if (ti_resolve_calendar_from_object(ctx, val, &cal) < 0)
                return JS_EXCEPTION;
            if (ti_prepare_calendar_fields(ctx, val, field_mask, 0, 0, &fields, cal) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return ti_create_plainyearmonth_from_fields(ctx, &fields, overflow_constrain);
        }
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        TISODateTimeParseResult r;
        if (!str)
            return JS_EXCEPTION;
        if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth string");
        }
        JS_FreeCString(ctx, str);
        if (!r.has_date && !r.has_yearmonth)
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth string");
        if (r.has_utc)
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth string");
        if (r.has_tz_offset && !r.has_time)
            return JS_ThrowRangeError(ctx, "invalid PlainYearMonth string");
        {
            CalendarType cal = r.has_calendar ? r.calendar : CAL_ISO8601;
            if (cal == CAL_UNKNOWN)
                return JS_ThrowRangeError(ctx, "invalid calendar ID");
            if (!pym_is_valid_year_month(r.year, r.month))
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
            /* YYYY-MM format is only valid with iso8601 calendar */
            if (!r.has_date && cal != CAL_ISO8601)
                return JS_ThrowRangeError(ctx, "invalid PlainYearMonth string");
            /* Validate options AFTER string parsing */
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            if (cal == CAL_ISO8601 || cal == CAL_GREGORY) {
                return js_create_plainyearmonth_with_calendar(ctx, r.year, r.month, 1, cal);
            } else {
                /* For non-ISO calendars, perform ISO->calendar->ISO with day=1
                   to determine referenceISODay */
                int cal_y, cal_m, cal_d, iso_y, iso_m, iso_d;
                ti_iso_to_calendar_date(cal, r.year, r.month, r.has_date ? r.day : 1, &cal_y, &cal_m, &cal_d);
                /* Override day to 1 for year-month */
                cal_d = 1;
                if (ti_calendar_date_to_iso(cal, cal_y, cal_m, cal_d, &iso_y, &iso_m, &iso_d) < 0)
                    return JS_ThrowRangeError(ctx, "invalid PlainYearMonth");
                return js_create_plainyearmonth_with_calendar(ctx, iso_y, iso_m, iso_d, cal);
            }
        }
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.PlainYearMonth.from");
}

static const JSCFunctionListEntry js_plainyearmonth_proto_funcs[] = {
    JS_CGETSET_DEF("year", js_plainyearmonth_get_year, NULL),
    JS_CGETSET_DEF("month", js_plainyearmonth_get_month, NULL),
    JS_CGETSET_DEF("monthCode", js_plainyearmonth_get_monthCode, NULL),
    JS_CGETSET_DEF("calendarId", js_plainyearmonth_get_calendarId, NULL),
    JS_CGETSET_DEF("daysInMonth", js_plainyearmonth_get_daysInMonth, NULL),
    JS_CGETSET_DEF("daysInYear", js_plainyearmonth_get_daysInYear, NULL),
    JS_CGETSET_DEF("inLeapYear", js_plainyearmonth_get_inLeapYear, NULL),
    JS_CGETSET_DEF("monthsInYear", js_plainyearmonth_get_monthsInYear, NULL),
    JS_CGETSET_DEF("referenceISODay", js_plainyearmonth_get_referenceISODay, NULL),
    JS_CGETSET_DEF("era", js_plainyearmonth_get_era, NULL),
    JS_CGETSET_DEF("eraYear", js_plainyearmonth_get_eraYear, NULL),
    JS_CGETSET_DEF("daysInWeek", js_plainyearmonth_get_daysInWeek, NULL),
    JS_CGETSET_DEF("dayOfWeek", js_plainyearmonth_get_dayOfWeek, NULL),
    JS_CGETSET_DEF("dayOfYear", js_plainyearmonth_get_dayOfYear, NULL),
    JS_CGETSET_DEF("weekOfYear", js_plainyearmonth_get_weekOfYear, NULL),
    JS_CGETSET_DEF("yearOfWeek", js_plainyearmonth_get_yearOfWeek, NULL),
    JS_CFUNC_MAGIC_DEF("add", 1, js_plainyearmonth_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_plainyearmonth_add_subtract, 0),
    JS_CFUNC_MAGIC_DEF("until", 1, js_plainyearmonth_difference, 1),
    JS_CFUNC_MAGIC_DEF("since", 1, js_plainyearmonth_difference, 0),
    JS_CFUNC_DEF("equals", 1, js_plainyearmonth_equals),
    JS_CFUNC_DEF("toString", 0, js_plainyearmonth_toString),
    JS_CFUNC_DEF("toJSON", 0, js_plainyearmonth_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_plainyearmonth_toLocaleString),
    JS_CFUNC_DEF("toPlainDate", 1, js_plainyearmonth_toPlainDate),
    JS_CFUNC_DEF("with", 1, js_plainyearmonth_with),
    JS_CFUNC_DEF("valueOf", 0, js_plainyearmonth_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainYearMonth", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_plainyearmonth_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_plainyearmonth_from),
    JS_CFUNC_DEF("compare", 2, js_plainyearmonth_compare),
};

int js_add_plainyearmonth(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_plainyearmonth_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_plainyearmonth_class_id, &js_plainyearmonth_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_plainyearmonth_class_id, "PlainYearMonth",
                               (JSCFunction *)js_plainyearmonth_constructor, 2,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_plainyearmonth_static_funcs, countof(js_plainyearmonth_static_funcs),
                               js_plainyearmonth_proto_funcs, countof(js_plainyearmonth_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "PlainYearMonth", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
