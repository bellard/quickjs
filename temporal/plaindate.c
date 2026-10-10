/*
 * QuickJS Temporal.PlainDate implementation (minimal)
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

JSClassID js_plaindate_class_id;

static void js_plaindate_finalizer(JSRuntime *rt, JSValue val)
{
    JSPlainDateData *s = JS_GetOpaque(val, js_plaindate_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_plaindate_class = {
    "Temporal.PlainDate",
    .finalizer = js_plaindate_finalizer,
};

JSValue js_create_plaindate_with_calendar(JSContext *ctx, int year, int month, int day, CalendarType calendar)
{
    if (calendar == CAL_ISO8601 || calendar == CAL_GREGORY) {
        /* ISO date range: -271821-04-19 to +275760-09-13 */
        if (year < -271821 || year > 275760 ||
            (year == -271821 && (month < 4 || (month == 4 && day < 19))) ||
            (year == 275760 && (month > 9 || (month == 9 && day > 13))))
            return JS_ThrowRangeError(ctx, "PlainDate out of range");
    }
    {
        JSPlainDateData *s;
        JSValue obj;
        s = js_mallocz(ctx, sizeof(*s));
        if (!s)
            return JS_EXCEPTION;
        s->year = year;
        s->month = month;
        s->day = day;
        s->calendar = calendar;
        obj = JS_NewObjectClass(ctx, (int)js_plaindate_class_id);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        JS_SetOpaque(obj, s);
        return obj;
    }
}

JSValue js_create_plaindate(JSContext *ctx, int year, int month, int day)
{
    return js_create_plaindate_with_calendar(ctx, year, month, day, CAL_ISO8601);
}

JSValue ti_create_plaindate_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                         int overflow_constrain)
{
    int year, month, day;
    JSValue obj;

    if (ti_resolve_date_from_fields(ctx, fields, overflow_constrain, &year, &month, &day, 1) < 0)
        return JS_EXCEPTION;

    {
        int iso_y = year, iso_m = month, iso_d = day;
        if (fields->calendar != CAL_ISO8601 && fields->calendar != CAL_GREGORY) {
            ti_calendar_date_to_iso(fields->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        }
        obj = js_create_plaindate_with_calendar(ctx, iso_y, iso_m, iso_d, fields->calendar);
    }
    return obj;
}

int pd_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int pd_days_in_month(int year, int month)
{
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && pd_is_leap_year(year))
        return 29;
    return days[month - 1];
}

int pd_is_valid_date(int year, int month, int day)
{
    if (month < 1 || month > 12)
        return 0;
    if (day < 1 || day > pd_days_in_month(year, month))
        return 0;
    return 1;
}

/* Convert stored ISO date to calendar date for the object's calendar */
static void pd_get_calendar_date(JSPlainDateData *s, int *cal_year, int *cal_month, int *cal_day)
{
    ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, cal_year, cal_month, cal_day);
}

static int pd_is_valid_date_for_calendar(int year, int month, int day, CalendarType cal)
{
    int max_month = ti_calendar_months_in_year(cal, year);
    if (month < 1 || month > max_month)
        return 0;
    {
        int dim = ti_calendar_days_in_month(cal, year, month);
        if (day < 1 || day > dim)
            return 0;
    }
    return 1;
}

static int pd_to_integer_with_truncation(JSContext *ctx, JSValueConst val, int *pres)
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

/* Temporal.PlainDate constructor */
static JSValue js_plaindate_constructor(JSContext *ctx, JSValueConst new_target,
                                         int argc, JSValueConst *argv)
{
    int year, month, day;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.PlainDate constructor cannot be called as a function");
    }
    if (pd_to_integer_with_truncation(ctx, argv[0], &year) < 0)
        return JS_EXCEPTION;
    if (pd_to_integer_with_truncation(ctx, argv[1], &month) < 0)
        return JS_EXCEPTION;
    if (pd_to_integer_with_truncation(ctx, argv[2], &day) < 0)
        return JS_EXCEPTION;
    if (!pd_is_valid_date(year, month, day))
        return JS_ThrowRangeError(ctx, "invalid PlainDate");
    if (year < -271821 || year > 275760 ||
        (year == -271821 && (month < 4 || (month == 4 && day < 19))) ||
        (year == 275760 && (month > 9 || (month == 9 && day > 13))))
        return JS_ThrowRangeError(ctx, "PlainDate out of range");
    {
        JSPlainDateData *s = js_mallocz(ctx, sizeof(*s));
        JSValue obj;
        if (!s)
            return JS_EXCEPTION;
        s->year = year;
        s->month = month;
        s->day = day;
        s->calendar = CAL_ISO8601;
        if (argc > 3) {
            int cal_res = ti_validate_constructor_calendar(ctx, argv[3], &s->calendar);
            if (cal_res < 0) {
                js_free(ctx, s);
                return JS_EXCEPTION;
            }
        }
        obj = js_create_temporal_object(ctx, new_target, js_plaindate_class_id, s);
        if (JS_IsException(obj)) {
            js_free(ctx, s);
            return JS_EXCEPTION;
        }
        return obj;
    }
}

/* Getters */
/* Getters */
enum {
    PLAINDATE_YEAR = 0,
    PLAINDATE_MONTH,
    PLAINDATE_DAY,
};

static JSValue js_plaindate_get_field(JSContext *ctx, JSValueConst this_val, int magic)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_year, cal_month, cal_day;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_year, &cal_month, &cal_day);
    switch (magic) {
    case PLAINDATE_YEAR: return JS_NewInt32(ctx, cal_year);
    case PLAINDATE_MONTH: return JS_NewInt32(ctx, cal_month);
    case PLAINDATE_DAY: return JS_NewInt32(ctx, cal_day);
    default: return JS_UNDEFINED;
    }
}

static JSValue js_plaindate_get_monthCode(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_year, cal_month, cal_day;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_year, &cal_month, &cal_day);
    return ti_get_month_code(ctx, s->calendar, cal_year, cal_month);
}

static JSValue js_plaindate_get_calendarId(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar));
}

static JSValue js_plaindate_get_era(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int era_year;
    TIEra era;
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->year, s->month, s->day, cal_y, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewString(ctx, ti_era_to_string(era));
    return JS_UNDEFINED;
}

static JSValue js_plaindate_get_eraYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int era_year;
    TIEra era;
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->year, s->month, s->day, cal_y, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewInt32(ctx, era_year);
    return JS_UNDEFINED;
}

static int pd_day_of_week(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 2;
    int d = (day + y + floor_div32(y, 4) - floor_div32(y, 100) + floor_div32(y, 400) + (31 * m) / 12) % 7;
    if (d <= 0)
        d += 7;
    return d;
}

int pd_day_of_year(int year, int month, int day)
{
    int doy = day;
    int i;
    for (i = 1; i < month; i++)
        doy += ti_iso_month_days[i];
    if (month > 2 && pd_is_leap_year(year))
        doy++;
    return doy;
}

static void pd_iso_week(int year, int month, int day, int *weekYear, int *weekNumber)
{
    int dow = pd_day_of_week(year, month, day);
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

static JSValue js_plaindate_get_inLeapYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewBool(ctx, ti_calendar_is_leap_year(s->calendar, cal_y));
}

static JSValue js_plaindate_get_daysInMonth(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_month(s->calendar, cal_y, cal_m));
}

static JSValue js_plaindate_get_daysInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_year(s->calendar, cal_y));
}

static JSValue js_plaindate_get_daysInWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, 7);
}

static JSValue js_plaindate_get_monthsInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_months_in_year(s->calendar, cal_y));
}

static JSValue js_plaindate_get_dayOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_day_of_year(s->calendar, cal_y, cal_m, cal_d));
}

static JSValue js_plaindate_get_dayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, pd_day_of_week(s->year, s->month, s->day));
}

static JSValue js_plaindate_get_weekOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pd_iso_week(s->year, s->month, s->day, &weekYear, &weekNumber);
    return JS_NewInt32(ctx, weekNumber);
}

static JSValue js_plaindate_get_yearOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar != CAL_ISO8601)
        return JS_UNDEFINED;
    pd_iso_week(s->year, s->month, s->day, &weekYear, &weekNumber);
    return JS_NewInt64(ctx, weekYear);
}

static JSValue js_plaindate_from(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv);

/* Temporal.PlainDate.prototype.equals */
static JSValue js_plaindate_equals(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSPlainDateData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (JS_IsNumber(argv[0]) || JS_IsUndefined(argv[0]) || JS_IsNull(argv[0]) ||
        JS_IsBool(argv[0]) || JS_IsBigInt(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "invalid PlainDate");
    other_val = js_plaindate_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaindate_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    {
        int res = s->year == other->year && s->month == other->month && s->day == other->day &&
                  s->calendar == other->calendar;
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, res);
    }
}

/* Temporal.PlainDate.compare */
static JSValue js_plaindate_compare(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)argc;
    (void)this_val;
    JSPlainDateData *s1, *s2;
    JSValue v1, v2;
    int cmp;
    v1 = js_plaindate_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(v1))
        return JS_EXCEPTION;
    v2 = js_plaindate_from(ctx, JS_UNDEFINED, 1, argv + 1);
    if (JS_IsException(v2)) {
        JS_FreeValue(ctx, v1);
        return JS_EXCEPTION;
    }
    s1 = JS_GetOpaque(v1, js_plaindate_class_id);
    s2 = JS_GetOpaque(v2, js_plaindate_class_id);
    if (!s1 || !s2) {
        JS_FreeValue(ctx, v1);
        JS_FreeValue(ctx, v2);
        return JS_ThrowTypeError(ctx, "compare arguments must be PlainDate objects");
    }
    if (s1->year != s2->year)
        cmp = s1->year < s2->year ? -1 : 1;
    else if (s1->month != s2->month)
        cmp = s1->month < s2->month ? -1 : 1;
    else if (s1->day != s2->day)
        cmp = s1->day < s2->day ? -1 : 1;
    else
        cmp = 0;
    JS_FreeValue(ctx, v1);
    JS_FreeValue(ctx, v2);
    return JS_NewInt32(ctx, cmp);
}

/* Format PlainDate as ISO string */
static JSValue js_plaindate_toString(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSPlainDateData *s;
    char buf[64];
    int calendarName = 0; /* 0=auto, 1=always, 2=critical, 3=never */
    if (argc >= 1 && ti_validate_options_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (argc >= 1 && JS_IsObject(argv[0])) {
        if (ti_get_calendar_name_option(ctx, argv[0], &calendarName) < 0)
            return JS_EXCEPTION;
    }
    {
        int fy = s->year, fm = s->month, fd = s->day;
        if (fy < 0)
            snprintf(buf, sizeof(buf), "-%06d-%02d-%02d", -fy, fm, fd);
        else if (fy >= 10000)
            snprintf(buf, sizeof(buf), "+%06d-%02d-%02d", fy, fm, fd);
        else
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", fy, fm, fd);
    }
    ti_append_calendar_annotation(buf, sizeof(buf), s->calendar, calendarName);
    return JS_NewString(ctx, buf);
}

static JSValue js_plaindate_toJSON(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_plaindate_toString(ctx, this_val, 0, NULL);
}

static JSValue js_plaindate_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 1, js_plaindate_toString);
}

static JSValue js_plaindate_valueOf(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.PlainDate with < or >");
}

/* Temporal.PlainDate.prototype.with */
static JSValue js_plaindate_with(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSPlainDateData *s;
    int year, month, day;
    int orig_cal_year, orig_cal_month, orig_cal_day;
    JSValue val;
    int overflow_constrain = 1; /* default is constrain */
    int month_code_iso_err = 0;
    int month_conflict_err = 0;
    int has_month = 0;
    int has_year_prop = 0;
    int with_month_code = 0;
    int with_has_month_code = 0;
    int with_has_l = 0;
    TIPreparedFields fields;
    s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    if (!s)
        return JS_EXCEPTION;
    ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &orig_cal_year, &orig_cal_month, &orig_cal_day);
    year = orig_cal_year;
    month = orig_cal_month;
    day = orig_cal_day;
    val = argv[0];
    if (ti_is_partial_temporal_object(ctx, val) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, val,
                                    TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                    TI_FIELD_YEAR,
                                    0, 1, &fields, s->calendar) < 0)
        return JS_EXCEPTION;
    /* Merge fields with existing object */
    if (fields.present_mask & TI_FIELD_DAY) {
        day = fields.day;
    }
    if (fields.present_mask & TI_FIELD_MONTH) {
        has_month = 1;
        month = fields.month;
    }
    if (fields.present_mask & TI_FIELD_YEAR) {
        has_year_prop = 1;
        year = (int)fields.year;
        if (s->calendar == CAL_ISO8601 && (year < -271821 || year > 275760))
            return JS_ThrowRangeError(ctx, "year out of range");
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
                if (year != cal_year_from_era)
                    return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
            }
            year = cal_year_from_era;
        }
    }
    /* monthCode processing */
    if (fields.present_mask & TI_FIELD_MONTH_CODE) {
        with_has_month_code = 1;
        with_month_code = fields.month_from_code;
        with_has_l = fields.month_code_has_l;
        if (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) {
            if (with_month_code < 1 || with_month_code > 12 || with_has_l)
                month_code_iso_err = 1;
            else if (has_month && month != with_month_code)
                month_conflict_err = 1;
            else
                month = with_month_code;
        } else {
            int resolved_month = ti_calendar_month_code_to_month(s->calendar, year, with_month_code, with_has_l);
            if (resolved_month < 0)
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
            if (has_month && month != resolved_month)
                month_conflict_err = 1;
            else
                month = resolved_month;
        }
    }
    /* Validate options object type */
    if (argc >= 2 && !JS_IsUndefined(argv[1])) {
        if (ti_validate_options_object(ctx, argv[1]) < 0)
            return JS_EXCEPTION;
        if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
    }
    if (month_code_iso_err)
        return JS_ThrowRangeError(ctx, "monthCode is not valid for ISO 8601 calendar");
    if (month_conflict_err)
        return JS_ThrowRangeError(ctx, "month and monthCode conflict");
    /* If monthCode was preserved from source but year changed, re-resolve */
    if (!with_has_month_code && !has_month && year != orig_cal_year) {
        int orig_mc, orig_has_l;
        int result_mc, result_has_l;
        ti_calendar_month_to_month_code(s->calendar, orig_cal_year, orig_cal_month, &orig_mc, &orig_has_l);
        {
            int resolved_month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, orig_has_l);
            if (resolved_month < 0) {
                if (!overflow_constrain)
                    return JS_ThrowRangeError(ctx, "invalid PlainDate");
                resolved_month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, 0);
            }
            month = resolved_month;
        }
        ti_calendar_month_to_month_code(s->calendar, year, month, &result_mc, &result_has_l);
        if (!overflow_constrain && (result_mc != orig_mc || result_has_l != orig_has_l))
            return JS_ThrowRangeError(ctx, "invalid PlainDate");
    }
    if (!pd_is_valid_date_for_calendar(year, month, day, s->calendar)) {
        if (!overflow_constrain)
            return JS_ThrowRangeError(ctx, "invalid PlainDate");
        /* Constrain month and day */
        {
            int max_month = ti_calendar_months_in_year(s->calendar, year);
            if (month < 1)
                month = 1;
            else if (month > max_month)
                month = max_month;
        }
        {
            int dim = ti_calendar_days_in_month(s->calendar, year, month);
            if (day > dim)
                day = dim;
            else if (day < 1)
                day = 1;
        }
    }
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        year = iso_y; month = iso_m; day = iso_d;
    }
    {
        JSValue obj = js_create_plaindate_with_calendar(ctx, year, month, day, s->calendar);
        return obj;
    }
}

static int pd_balance_month(int *year, int *month, CalendarType cal)
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
    return 0;
}

static int pd_balance_date(int *year, int *month, int *day, CalendarType cal)
{
    int dim;
    pd_balance_month(year, month, cal);
    while (*day > (dim = ti_calendar_days_in_month(cal, *year, *month))) {
        *day -= dim;
        (*month)++;
        if (*month > ti_calendar_months_in_year(cal, *year)) {
            *month = 1;
            (*year)++;
        }
    }
    while (*day < 1) {
        (*month)--;
        if (*month < 1) {
            (*year)--;
            *month = ti_calendar_months_in_year(cal, *year);
        }
        dim = ti_calendar_days_in_month(cal, *year, *month);
        *day += dim;
    }
    return 0;
}

static JSValue js_plaindate_add_subtract(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv, int is_add)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    JSDurationData d = {0};
    int year, month, day;
    int overflow_constrain = 1;
    js_int128_t total_ns;
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
    year = s->year;
    month = s->month;
    day = s->day;
    {
        int orig_mc = 0, orig_has_l = 0;
        ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &year, &month, &day);
        ti_calendar_month_to_month_code(s->calendar, year, month, &orig_mc, &orig_has_l);
        year = year + sign * (int)d.years;
        month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, orig_has_l);
        if (month < 0) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
            month = ti_calendar_month_code_to_month(s->calendar, year, orig_mc, 0);
        } else if (!overflow_constrain) {
            int result_mc, result_has_l;
            ti_calendar_month_to_month_code(s->calendar, year, month, &result_mc, &result_has_l);
            if (result_mc != orig_mc || result_has_l != orig_has_l)
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
        }
    }
    month += sign * (int)d.months;
    pd_balance_month(&year, &month, s->calendar);
    {
        int dim = ti_calendar_days_in_month(s->calendar, year, month);
        if (day > dim) {
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
            day = dim;
        }
    }
    day += sign * (int)(d.weeks * 7 + d.days);
    /* Balance smaller time units into days */
    total_ns = ti_duration_time_fields_to_ns(&d);
    day_delta = (int64_t)(total_ns / 86400000000000LL);
    day += sign * (int)day_delta;
    pd_balance_date(&year, &month, &day, s->calendar);
    {
        int iso_y, iso_m, iso_d;
        ti_calendar_date_to_iso(s->calendar, year, month, day, &iso_y, &iso_m, &iso_d);
        year = iso_y; month = iso_m; day = iso_d;
    }
    if (year < -271821 || year > 275760)
        return JS_ThrowRangeError(ctx, "invalid PlainDate");
    /* Check against ISO date bounds */
    if (year == -271821 && (month < 4 || (month == 4 && day < 19)))
        return JS_ThrowRangeError(ctx, "invalid PlainDate");
    if (year == 275760 && (month > 9 || (month == 9 && day > 13)))
        return JS_ThrowRangeError(ctx, "invalid PlainDate");
    {
        JSValue obj = js_create_plaindate_with_calendar(ctx, year, month, day, s->calendar);
        return obj;
    }
}

static JSValue js_plaindate_difference(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv, int is_until)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    JSValue other_val;
    JSPlainDateData *other;
    int other_year, other_month, other_day;
    JSDurationData d = {0};
    TIUnit largestUnit = TI_UNIT_INVALID;
    TIUnit smallestUnit = TI_UNIT_INVALID;
    int64_t roundingIncrement = 1;
    TIRoundingMode roundingMode = ROUND_TRUNC;
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plaindate_from(ctx, JS_UNDEFINED, 1, argv);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plaindate_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowTypeError(ctx, "invalid PlainDate");
    }
    if (s->calendar != other->calendar) {
        JS_FreeValue(ctx, other_val);
        return JS_ThrowRangeError(ctx, "calendars must be the same");
    }
    other_year = other->year;
    other_month = other->month;
    other_day = other->day;
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
        if (ti_parse_unit_option(ctx, argv[1], "smallestUnit", &smallestUnit, TI_UNIT_OPT_ALLOW_UNDEFINED, TI_UNIT_INVALID) < 0) {
            JS_FreeValue(ctx, other_val);
            return JS_EXCEPTION;
        }
        /* Validate unit restrictions */
        if (largestUnit != TI_UNIT_INVALID && largestUnit != TI_UNIT_AUTO &&
            largestUnit != TI_UNIT_YEAR && largestUnit != TI_UNIT_MONTH &&
            largestUnit != TI_UNIT_WEEK && largestUnit != TI_UNIT_DAY) {
            JS_FreeValue(ctx, other_val);
            return JS_ThrowRangeError(ctx, "invalid largestUnit");
        }
        if (smallestUnit != TI_UNIT_INVALID && smallestUnit != TI_UNIT_YEAR &&
            smallestUnit != TI_UNIT_MONTH && smallestUnit != TI_UNIT_WEEK &&
            smallestUnit != TI_UNIT_DAY) {
            JS_FreeValue(ctx, other_val);
            return JS_ThrowRangeError(ctx, "invalid smallestUnit");
        }
    }
    JS_FreeValue(ctx, other_val);
    if (smallestUnit == TI_UNIT_INVALID)
        smallestUnit = TI_UNIT_DAY;
    if (largestUnit == TI_UNIT_INVALID || largestUnit == TI_UNIT_AUTO)
        largestUnit = (smallestUnit < TI_UNIT_DAY) ? smallestUnit : TI_UNIT_DAY;

    if (largestUnit > smallestUnit) {
        return JS_ThrowRangeError(ctx, "invalid unit combination");
    }

    /* Compute monthCode for Chinese/Dangi/Hebrew */
    int s_mc = 0, s_has_l = 0, s_has_mc = 0;
    int o_mc = 0, o_has_l = 0, o_has_mc = 0;
    int s_cal_y = s->year, s_cal_m = s->month, s_cal_d = s->day;
    int o_cal_y = other_year, o_cal_m = other_month, o_cal_d = other_day;
    if (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI || s->calendar == CAL_HEBREW) {
        ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &s_cal_y, &s_cal_m, &s_cal_d);
        ti_iso_to_calendar_date(s->calendar, other_year, other_month, other_day, &o_cal_y, &o_cal_m, &o_cal_d);
        ti_calendar_month_to_month_code(s->calendar, s_cal_y, s_cal_m, &s_mc, &s_has_l);
        s_has_mc = 1;
        ti_calendar_month_to_month_code(s->calendar, o_cal_y, o_cal_m, &o_mc, &o_has_l);
        o_has_mc = 1;
    }

    if ((largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) &&
        (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI || s->calendar == CAL_HEBREW) &&
        s_has_mc && o_has_mc) {
        ti_calendar_date_until_with_monthcode(s->calendar,
            s_cal_y, s_cal_m, s_cal_d, s_mc, s_has_mc, s_has_l,
            o_cal_y, o_cal_m, o_cal_d, o_mc, o_has_mc, o_has_l,
            largestUnit,
            &d.years, &d.months, &d.weeks, &d.days);
    } else if ((largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) &&
               s->calendar != CAL_ISO8601 && s->calendar != CAL_GREGORY &&
               s->calendar != CAL_BUDDHIST && s->calendar != CAL_ROC && s->calendar != CAL_JAPANESE) {
        if (s->calendar != CAL_CHINESE && s->calendar != CAL_DANGI && s->calendar != CAL_HEBREW) {
            ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &s_cal_y, &s_cal_m, &s_cal_d);
            ti_iso_to_calendar_date(s->calendar, other_year, other_month, other_day, &o_cal_y, &o_cal_m, &o_cal_d);
        }
        ti_calendar_date_until(s->calendar,
            s_cal_y, s_cal_m, s_cal_d,
            o_cal_y, o_cal_m, o_cal_d,
            largestUnit,
            &d.years, &d.months, &d.weeks, &d.days);
    } else if ((largestUnit == TI_UNIT_WEEK || largestUnit == TI_UNIT_DAY) &&
               s->calendar != CAL_ISO8601 && s->calendar != CAL_GREGORY &&
               s->calendar != CAL_CHINESE && s->calendar != CAL_DANGI && s->calendar != CAL_HEBREW &&
               s->calendar != CAL_BUDDHIST && s->calendar != CAL_ROC && s->calendar != CAL_JAPANESE) {
        ti_iso_to_calendar_date(s->calendar, s->year, s->month, s->day, &s_cal_y, &s_cal_m, &s_cal_d);
        ti_iso_to_calendar_date(s->calendar, other_year, other_month, other_day, &o_cal_y, &o_cal_m, &o_cal_d);
        ti_calendar_date_until(s->calendar,
            s_cal_y, s_cal_m, s_cal_d,
            o_cal_y, o_cal_m, o_cal_d,
            largestUnit,
            &d.years, &d.months, &d.weeks, &d.days);
    } else {
        ti_iso_calendar_date_until(s->year, s->month, s->day,
                                    other_year, other_month, other_day,
                                    largestUnit,
                                    &d.years, &d.months, &d.weeks, &d.days);
    }
    if (smallestUnit != TI_UNIT_DAY || roundingIncrement != 1) {
        TIRoundingMode rm = is_until ? roundingMode : ti_negate_rounding_mode(roundingMode);
        if (smallestUnit == TI_UNIT_DAY) {
            /* For PlainDate, day is not a calendar unit; just round the day count */
            d.days = ti_round_to_increment(d.days, roundingIncrement, rm);
        } else {
            if (ti_round_iso_date_duration(ctx,
                                            s->year, s->month, s->day,
                                            other_year, other_month, other_day,
                                            0, 0,
                                            d.years, d.months, d.weeks, d.days,
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
        d.weeks = -d.weeks;
        d.days = -d.days;
    }
    return js_create_duration(ctx, &d);
}

static JSValue js_plaindate_toZonedDateTime(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    JSTimeSpec ts;
    const char *timeZone = "UTC";
    char tz_buf[64] = "UTC";
    int64_t tz_offset_ns = 0;
    int hour = 0, minute = 0, second = 0, ms = 0, us = 0, ns = 0;
    if (!s)
        return JS_EXCEPTION;
    int has_plainTime = 0;
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
                JS_FreeCString(ctx, str);
                return JS_ThrowRangeError(ctx, "unsupported time zone");
            }
            tz_offset_ns = offset_ns;
        }
        timeZone = tz_buf;
        JS_FreeCString(ctx, str);
    } else if (JS_IsObject(argv[0])) {
        JSValue v = JS_GetPropertyStr(ctx, argv[0], "timeZone");
        if (JS_IsException(v))
            return JS_EXCEPTION;
        if (JS_IsUndefined(v)) {
            JS_FreeValue(ctx, v);
            return JS_ThrowTypeError(ctx, "missing timeZone");
        }
        if (!JS_IsString(v) && !JS_IsObject(v)) {
            JS_FreeValue(ctx, v);
            return JS_ThrowTypeError(ctx, "timeZone must be a string or object");
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
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, v);
                    return JS_ThrowRangeError(ctx, "unsupported time zone");
                }
                tz_offset_ns = offset_ns;
            }
            timeZone = tz_buf;
            JS_FreeCString(ctx, str);
        } else {
            /* Object timezone: not supported yet */
            JS_FreeValue(ctx, v);
            return JS_ThrowTypeError(ctx, "unsupported time zone argument");
        }
        JS_FreeValue(ctx, v);
        v = JS_GetPropertyStr(ctx, argv[0], "plainTime");
        if (JS_IsException(v))
            return JS_EXCEPTION;
        if (!JS_IsUndefined(v)) {
            has_plainTime = 1;
            JSPlainTimeData *pt = JS_GetOpaque(v, js_plaintime_class_id);
            if (pt) {
                hour = pt->hour;
                minute = pt->minute;
                second = pt->second;
                ms = pt->millisecond;
                us = pt->microsecond;
                ns = pt->nanosecond;
            } else {
                JSValue pt_val = js_plaintime_from(ctx, JS_UNDEFINED, 1, &v);
                if (JS_IsException(pt_val)) {
                    JS_FreeValue(ctx, v);
                    return JS_EXCEPTION;
                }
                pt = JS_GetOpaque(pt_val, js_plaintime_class_id);
                if (pt) {
                    hour = pt->hour;
                    minute = pt->minute;
                    second = pt->second;
                    ms = pt->millisecond;
                    us = pt->microsecond;
                    ns = pt->nanosecond;
                }
                JS_FreeValue(ctx, pt_val);
            }
            JS_FreeValue(ctx, v);
        }
    } else {
        return JS_ThrowTypeError(ctx, "unsupported time zone argument");
    }
    {
        int64_t days = pdt_to_ordinal_days(s->year, s->month, s->day);
        int64_t epoch_days = days - 719162; /* days before 1970 */
        int64_t epoch_sec = epoch_days * 86400LL;
        epoch_sec += hour * 3600LL + minute * 60LL + second;
        ts.secs = epoch_sec;
        ts.nsecs = (uint32_t)(ms * 1000000 + us * 1000 + ns);
    }
    {
        int tz_id = ti_tz_find_id(timeZone);
        if (tz_id >= 0) {
            if (has_plainTime) {
                /* Object with plainTime: use compatible disambiguation */
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, ts.secs, 2, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                ts.secs = utc_sec;
            } else {
                /* String or object without plainTime: use startOfDay semantics */
                int64_t utc_sec;
                int offset_sec;
                if (ti_tz_local_to_utc(tz_id, ts.secs, 0, &utc_sec, &offset_sec) < 0)
                    return JS_ThrowRangeError(ctx, "multiple instants found");
                if (utc_sec + ti_tz_offset_at_sec(tz_id, utc_sec) == ts.secs) {
                    ts.secs = utc_sec;
                } else {
                    /* Gap at midnight: return next transition after (midnight - 1 day) */
                    int64_t next_trans = ti_tz_next_transition_sec(tz_id, ts.secs - 86400LL);
                    if (next_trans != INT64_MIN) {
                        ts.secs = next_trans;
                    } else {
                        ts.secs = utc_sec;
                    }
                }
            }
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

static JSValue js_plaindate_toPlainDateTime(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int hour = 0, minute = 0, second = 0, ms = 0, us = 0, ns = 0;
    if (!s)
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSPlainTimeData *pt = JS_GetOpaque(argv[0], js_plaintime_class_id);
        if (pt) {
            hour = pt->hour;
            minute = pt->minute;
            second = pt->second;
            ms = pt->millisecond;
            us = pt->microsecond;
            ns = pt->nanosecond;
        } else if (JS_GetOpaque(argv[0], js_plaindatetime_class_id)) {
            JSPlainDateTimeData *pdt = JS_GetOpaque(argv[0], js_plaindatetime_class_id);
            hour = pdt->hour;
            minute = pdt->minute;
            second = pdt->second;
            ms = pdt->millisecond;
            us = pdt->microsecond;
            ns = pdt->nanosecond;
        } else if (JS_GetOpaque(argv[0], js_zoneddatetime_class_id)) {
            JSZonedDateTimeData *zdt = JS_GetOpaque(argv[0], js_zoneddatetime_class_id);
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
                int mo, d;
                ti_epoch_sec_to_date(epoch_sec, &y, &mo, &d, &hour, &minute, &second);
                ms = epoch_nsec / 1000000;
                us = (epoch_nsec % 1000000) / 1000;
                ns = epoch_nsec % 1000;
            }
        } else {
            JSValue pt_val = js_plaintime_from(ctx, JS_UNDEFINED, 1, argv);
            if (JS_IsException(pt_val))
                return pt_val;
            {
                JSPlainTimeData *pt2 = JS_GetOpaque(pt_val, js_plaintime_class_id);
                if (pt2) {
                    hour = pt2->hour;
                    minute = pt2->minute;
                    second = pt2->second;
                    ms = pt2->millisecond;
                    us = pt2->microsecond;
                    ns = pt2->nanosecond;
                }
            }
            JS_FreeValue(ctx, pt_val);
        }
    }
    return js_create_plaindatetime_with_calendar(ctx, s->year, s->month, s->day,
                                   hour, minute, second, ms, us, ns, s->calendar);
}

static JSValue js_plaindate_toPlainYearMonth(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int ref_day;
    if (!s)
        return JS_EXCEPTION;
    ref_day = (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) ? 1 : s->day;
    return js_create_plainyearmonth_with_calendar(ctx, s->year, s->month, ref_day, s->calendar);
}

static JSValue js_plaindate_toPlainMonthDay(JSContext *ctx, JSValueConst this_val,
                                            int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    int ref_year;
    if (!s)
        return JS_EXCEPTION;
    ref_year = (s->calendar == CAL_ISO8601 || s->calendar == CAL_GREGORY) ? 1972 : s->year;
    return js_create_plainmonthday_with_calendar(ctx, ref_year, s->month, s->day, s->calendar);
}

static JSValue js_plaindate_withCalendar(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    (void)argc;
    JSPlainDateData *s = JS_GetOpaque2(ctx, this_val, js_plaindate_class_id);
    JSPlainDateData *t;
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
    obj = JS_NewObjectClass(ctx, (int)js_plaindate_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, t);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, t);
    return obj;
}

static JSValue js_plaindate_from(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    JSValue val;
    int overflow_constrain = 1;
    val = argv[0];
    if (JS_IsObject(val)) {
        JSPlainDateData *s = JS_GetOpaque(val, js_plaindate_class_id);
        if (s) {
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return js_create_plaindate_with_calendar(ctx, s->year, s->month, s->day, s->calendar);
        }
        {
            JSPlainDateTimeData *pdt = JS_GetOpaque(val, js_plaindatetime_class_id);
            if (pdt) {
                int year = pdt->year, month = pdt->month, day = pdt->day;
                if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                    return JS_EXCEPTION;
                if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                    return JS_EXCEPTION;
                if (pdt->calendar != CAL_ISO8601 && pdt->calendar != CAL_GREGORY)
                    ti_calendar_date_to_iso(pdt->calendar, pdt->year, pdt->month, pdt->day, &year, &month, &day);
                return js_create_plaindate_with_calendar(ctx, year, month, day, pdt->calendar);
            }
        }
        {
            JSZonedDateTimeData *zdt = JS_GetOpaque(val, js_zoneddatetime_class_id);
            if (zdt) {
                int64_t year;
                int month, day, hour, minute, second;
                if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                    return JS_EXCEPTION;
                if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                    return JS_EXCEPTION;
                ti_epoch_sec_to_date(zdt->ts.secs, &year, &month, &day, &hour, &minute, &second);
                return js_create_plaindate_with_calendar(ctx, (int)year, month, day, zdt->calendar);
            }
        }
        {
            TIPreparedFields fields;
            uint32_t field_mask = TI_FIELD_YEAR | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                  TI_FIELD_DAY;
            CalendarType cal = CAL_ISO8601;
            if (ti_resolve_calendar_from_object(ctx, val, &cal) < 0)
                return JS_EXCEPTION;
            if (ti_prepare_calendar_fields(ctx, val, field_mask, 0, 0, &fields, cal) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return ti_create_plaindate_from_fields(ctx, &fields, overflow_constrain);
        }
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        TISODateTimeParseResult r;
        if (!str)
            return JS_EXCEPTION;
        if (ti_parse_iso_datetime_string(ctx, str, &r, TI_PARSE_DEFAULT) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainDate string");
        }
        JS_FreeCString(ctx, str);
        if (!r.has_date)
            return JS_ThrowRangeError(ctx, "invalid PlainDate string");
        if (r.has_utc)
            return JS_ThrowRangeError(ctx, "invalid PlainDate string");
        if (r.has_tz_offset && !r.has_time)
            return JS_ThrowRangeError(ctx, "invalid PlainDate string");
        {
            CalendarType cal = r.has_calendar ? r.calendar : CAL_ISO8601;
            if (cal == CAL_UNKNOWN)
                return JS_ThrowRangeError(ctx, "invalid calendar ID");
            /* String contains ISO fields; store them directly */
            if (!pd_is_valid_date(r.year, r.month, r.day))
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
            /* Validate options AFTER successful string parse */
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            if (!overflow_constrain && !pd_is_valid_date(r.year, r.month, r.day))
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
            return js_create_plaindate_with_calendar(ctx, r.year, r.month, r.day, cal);
        }
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.PlainDate.from");
}

static const JSCFunctionListEntry js_plaindate_proto_funcs[] = {
    JS_CGETSET_MAGIC_DEF("year", js_plaindate_get_field, NULL, PLAINDATE_YEAR),
    JS_CGETSET_MAGIC_DEF("month", js_plaindate_get_field, NULL, PLAINDATE_MONTH),
    JS_CGETSET_DEF("monthCode", js_plaindate_get_monthCode, NULL),
    JS_CGETSET_MAGIC_DEF("day", js_plaindate_get_field, NULL, PLAINDATE_DAY),
    JS_CGETSET_DEF("calendarId", js_plaindate_get_calendarId, NULL),
    JS_CGETSET_DEF("era", js_plaindate_get_era, NULL),
    JS_CGETSET_DEF("eraYear", js_plaindate_get_eraYear, NULL),
    JS_CGETSET_DEF("inLeapYear", js_plaindate_get_inLeapYear, NULL),
    JS_CGETSET_DEF("daysInMonth", js_plaindate_get_daysInMonth, NULL),
    JS_CGETSET_DEF("daysInYear", js_plaindate_get_daysInYear, NULL),
    JS_CGETSET_DEF("daysInWeek", js_plaindate_get_daysInWeek, NULL),
    JS_CGETSET_DEF("monthsInYear", js_plaindate_get_monthsInYear, NULL),
    JS_CGETSET_DEF("dayOfYear", js_plaindate_get_dayOfYear, NULL),
    JS_CGETSET_DEF("dayOfWeek", js_plaindate_get_dayOfWeek, NULL),
    JS_CGETSET_DEF("weekOfYear", js_plaindate_get_weekOfYear, NULL),
    JS_CGETSET_DEF("yearOfWeek", js_plaindate_get_yearOfWeek, NULL),
    JS_CFUNC_DEF("equals", 1, js_plaindate_equals),
    JS_CFUNC_DEF("toString", 0, js_plaindate_toString),
    JS_CFUNC_DEF("toJSON", 0, js_plaindate_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_plaindate_toLocaleString),
    JS_CFUNC_MAGIC_DEF("add", 1, js_plaindate_add_subtract, 1),
    JS_CFUNC_MAGIC_DEF("subtract", 1, js_plaindate_add_subtract, 0),
    JS_CFUNC_MAGIC_DEF("until", 1, js_plaindate_difference, 1),
    JS_CFUNC_MAGIC_DEF("since", 1, js_plaindate_difference, 0),
    JS_CFUNC_DEF("toZonedDateTime", 1, js_plaindate_toZonedDateTime),
    JS_CFUNC_DEF("toPlainDateTime", 0, js_plaindate_toPlainDateTime),

    JS_CFUNC_DEF("toPlainYearMonth", 0, js_plaindate_toPlainYearMonth),
    JS_CFUNC_DEF("toPlainMonthDay", 0, js_plaindate_toPlainMonthDay),
    JS_CFUNC_DEF("with", 1, js_plaindate_with),
    JS_CFUNC_DEF("withCalendar", 1, js_plaindate_withCalendar),
    JS_CFUNC_DEF("valueOf", 0, js_plaindate_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainDate", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_plaindate_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_plaindate_from),
    JS_CFUNC_DEF("compare", 2, js_plaindate_compare),
};

int js_add_plaindate(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_plaindate_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_plaindate_class_id, &js_plaindate_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_plaindate_class_id, "PlainDate",
                               (JSCFunction *)js_plaindate_constructor, 3,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_plaindate_static_funcs, countof(js_plaindate_static_funcs),
                               js_plaindate_proto_funcs, countof(js_plaindate_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "PlainDate", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
