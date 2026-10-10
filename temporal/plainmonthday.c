/*
 * QuickJS Temporal.PlainMonthDay implementation (minimal)
 */

#include <string.h>
#include <math.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

JSClassID js_plainmonthday_class_id;


static void js_plainmonthday_finalizer(JSRuntime *rt, JSValue val)
{
    JSPlainMonthDayData *s = JS_GetOpaque(val, js_plainmonthday_class_id);
    if (s)
        js_free_rt(rt, s);
}

static const JSClassDef js_plainmonthday_class = {
    "Temporal.PlainMonthDay",
    .finalizer = js_plainmonthday_finalizer,
};

JSValue js_create_plainmonthday_with_calendar(JSContext *ctx, int iso_year, int iso_month, int iso_day, CalendarType calendar)
{
    JSPlainMonthDayData *s;
    JSValue obj;
    /* Validate ISO year range (ISO date limits: -271821 to 275760) */
    if (iso_year < -271821 || iso_year > 275760)
        return JS_ThrowRangeError(ctx, "PlainMonthDay out of range");
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;
    s->iso_year = iso_year;
    s->iso_month = iso_month;
    s->iso_day = iso_day;
    s->calendar = calendar;
    obj = JS_NewObjectClass(ctx, (int)js_plainmonthday_class_id);
    if (JS_IsException(obj)) {
        js_free(ctx, s);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, s);
    return obj;
}

static int pmd_to_integer_with_truncation(JSContext *ctx, JSValueConst val, int *pres)
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

/* Temporal.PlainMonthDay constructor */
static JSValue js_plainmonthday_constructor(JSContext *ctx, JSValueConst new_target,
                                             int argc, JSValueConst *argv)
{
    int month, day, year = 1972;
    if (JS_IsUndefined(new_target)) {
        return JS_ThrowTypeError(ctx, "Temporal.PlainMonthDay constructor cannot be called as a function");
    }
    if (pmd_to_integer_with_truncation(ctx, argv[0], &month) < 0)
        return JS_EXCEPTION;
    if (pmd_to_integer_with_truncation(ctx, argv[1], &day) < 0)
        return JS_EXCEPTION;
    {
        CalendarType calendar = CAL_ISO8601;
        if (argc > 2) {
            int cal_res = ti_validate_constructor_calendar(ctx, argv[2], &calendar);
            if (cal_res < 0)
                return JS_EXCEPTION;
        }
        if (argc > 3 && !JS_IsUndefined(argv[3])) {
            if (pmd_to_integer_with_truncation(ctx, argv[3], &year) < 0)
                return JS_EXCEPTION;
            if (ti_is_minus_zero(argv[3]))
                return JS_ThrowRangeError(ctx, "minus zero year is not allowed");
        }
        if (month < 1 || month > 12 || day < 1 || day > pd_days_in_month(year, month))
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
        /* ISODateWithinLimits check for CreateTemporalMonthDay */
        if (year < -271821 || (year == -271821 && (month < 4 || (month == 4 && day < 19))))
            return JS_ThrowRangeError(ctx, "date outside supported range");
        if (year > 275760 || (year == 275760 && (month > 9 || (month == 9 && day > 13))))
            return JS_ThrowRangeError(ctx, "date outside supported range");
        {
            JSPlainMonthDayData *s = js_mallocz(ctx, sizeof(*s));
            JSValue obj;
            if (!s)
                return JS_EXCEPTION;
            s->iso_year = year;
            s->iso_month = month;
            s->iso_day = day;
            s->calendar = calendar;
            obj = js_create_temporal_object(ctx, new_target, js_plainmonthday_class_id, s);
            if (JS_IsException(obj)) {
                js_free(ctx, s);
                return JS_EXCEPTION;
            }
            return obj;
        }
    }
}

/* Getters */
static JSValue js_plainmonthday_get_monthCode(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return ti_get_month_code(ctx, s->calendar, cal_y, cal_m);
}

static JSValue js_plainmonthday_get_day(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, cal_d);
}

static JSValue js_plainmonthday_get_referenceISOYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, s->iso_year);
}

static JSValue js_plainmonthday_get_calendarId(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar));
}

static int pmd_day_of_week(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year - a;
    int m = month + 12 * a - 2;
    int d = (day + y + y / 4 - y / 100 + y / 400 + (31 * m) / 12) % 7;
    return (d == 0) ? 7 : d;
}

static void pmd_iso_week(int year, int month, int day, int *weekYear, int *weekNumber)
{
    int dow = pmd_day_of_week(year, month, day);
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

static JSValue js_plainmonthday_get_era(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    TIEra era;
    int dummy_era_year;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->iso_year, s->iso_month, s->iso_day,
                                  cal_y, &era, &dummy_era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewString(ctx, ti_era_to_string(era));
    return JS_UNDEFINED;
}

static JSValue js_plainmonthday_get_eraYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    TIEra era;
    int era_year;
    if (!s)
        return JS_EXCEPTION;
    if (s->calendar == CAL_ISO8601)
        return JS_UNDEFINED;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    ti_calendar_era_and_era_year(s->calendar, s->iso_year, s->iso_month, s->iso_day,
                                  cal_y, &era, &era_year);
    if (era != TI_ERA_UNKNOWN)
        return JS_NewInt32(ctx, era_year);
    return JS_UNDEFINED;
}

static JSValue js_plainmonthday_get_inLeapYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewBool(ctx, ti_calendar_is_leap_year(s->calendar, cal_y));
}

static JSValue js_plainmonthday_get_daysInMonth(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_month(s->calendar, cal_y, cal_m));
}

static JSValue js_plainmonthday_get_daysInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_days_in_year(s->calendar, cal_y));
}

static JSValue js_plainmonthday_get_daysInWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, 7);
}

static JSValue js_plainmonthday_get_monthsInYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_months_in_year(s->calendar, cal_y));
}

static JSValue js_plainmonthday_get_dayOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int cal_y, cal_m, cal_d;
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
    return JS_NewInt32(ctx, ti_calendar_day_of_year(s->calendar, cal_y, cal_m, cal_d));
}

static JSValue js_plainmonthday_get_dayOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, pmd_day_of_week(s->iso_year, s->iso_month, s->iso_day));
}

static JSValue js_plainmonthday_get_weekOfYear(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    pmd_iso_week(s->iso_year, s->iso_month, s->iso_day, &weekYear, &weekNumber);
    return JS_NewInt32(ctx, weekNumber);
}

static JSValue js_plainmonthday_get_yearOfWeek(JSContext *ctx, JSValueConst this_val)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    int weekYear, weekNumber;
    if (!s)
        return JS_EXCEPTION;
    pmd_iso_week(s->iso_year, s->iso_month, s->iso_day, &weekYear, &weekNumber);
    return JS_NewInt64(ctx, weekYear);
}

static JSValue js_plainmonthday_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv);

/* Temporal.PlainMonthDay.prototype.equals */
static JSValue js_plainmonthday_equals(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    JSPlainMonthDayData *s, *other;
    JSValue other_val;
    s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    other_val = js_plainmonthday_from(ctx, JS_UNDEFINED, 1, &argv[0]);
    if (JS_IsException(other_val))
        return JS_EXCEPTION;
    other = JS_GetOpaque(other_val, js_plainmonthday_class_id);
    if (!other) {
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, 0);
    }
    {
        int res = s->iso_year == other->iso_year && s->iso_month == other->iso_month &&
                  s->iso_day == other->iso_day &&
                  s->calendar == other->calendar;
        JS_FreeValue(ctx, other_val);
        return JS_NewBool(ctx, res);
    }
}

/* Temporal.PlainMonthDay.prototype.with */
static JSValue js_plainmonthday_with(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    JSPlainMonthDayData *s;
    int month, day;
    int year_for_overflow = 0;
    int has_year = 0;
    JSValue val;
    int has_month_code = 0;
    int month_code_val = 0;
    int has_month_code_l = 0;
    int has_month_prop = 0;
    int month_prop_val = 0;
    TIPreparedFields fields;
    int orig_cal_y, orig_cal_m, orig_cal_d;
    int iso_y, iso_m, iso_d;
    s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    pmd_get_calendar_date(s, &orig_cal_y, &orig_cal_m, &orig_cal_d);
    month = orig_cal_m;
    day = orig_cal_d;
    val = argv[0];
    if (ti_is_partial_temporal_object(ctx, val) < 0)
        return JS_EXCEPTION;
    if (ti_prepare_calendar_fields(ctx, val,
                                    TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                    TI_FIELD_YEAR,
                                    0, 1, &fields, CAL_ISO8601) < 0)
        return JS_EXCEPTION;
    /* Merge fields with existing object */
    if (fields.present_mask & TI_FIELD_DAY) {
        day = fields.day;
    }
    if (fields.present_mask & TI_FIELD_MONTH) {
        has_month_prop = 1;
        month_prop_val = fields.month;
        month = fields.month;
    }
    if (fields.present_mask & TI_FIELD_MONTH_CODE) {
        has_month_code = 1;
        month_code_val = fields.month_from_code;
        has_month_code_l = fields.month_code_has_l;
    }
    if (fields.present_mask & TI_FIELD_YEAR) {
        has_year = 1;
        year_for_overflow = (int)fields.year;
    }
    /* Validate options type */
    if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
        return JS_EXCEPTION;
    {
        int overflow_constrain = 1;
        if (argc >= 2 && !JS_IsUndefined(argv[1])) {
            if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
        }
        if (s->calendar != CAL_ISO8601 && has_month_prop && !has_month_code)
            return JS_ThrowTypeError(ctx, "monthCode is required");
        if (has_month_code) {
            if (!ti_calendar_validate_month_code(s->calendar, month_code_val, has_month_code_l))
                return JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
            month = month_code_val;
        } else if (has_month_prop) {
            month = month_prop_val;
        }
        if (has_month_code && has_month_prop && month_code_val != month_prop_val &&
            ti_calendar_enforces_month_monthcode_agreement(s->calendar))
            return JS_ThrowRangeError(ctx, "month and monthCode must agree");
        {
            int ref_year = has_year ? year_for_overflow : orig_cal_y;
            int dim = ti_calendar_days_in_month(s->calendar, ref_year, month);
            if (day > dim || day < 1) {
                if (!overflow_constrain)
                    return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
                if (day > dim)
                    day = dim;
                else if (day < 1)
                    day = 1;
            }
        }
        if (ti_calendar_monthday_iso_reference_date(s->calendar, month,
                                                       has_month_code ? has_month_code_l : 0,
                                                       day, overflow_constrain, 0,
                                                       &iso_y, &iso_m, &iso_d) < 0)
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
        return js_create_plainmonthday_with_calendar(ctx, iso_y, iso_m, iso_d, s->calendar);
    }
}

/* Format PlainMonthDay as ISO string */
static JSValue js_plainmonthday_toString(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    JSPlainMonthDayData *s;
    char buf[64];
    int calendar_name = 0; /* 0 = auto/never, 1 = always, 2 = critical */
    if (argc >= 1 && ti_validate_options_object(ctx, argv[0]) < 0)
        return JS_EXCEPTION;
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        if (ti_get_calendar_name_option(ctx, argv[0], &calendar_name) < 0)
            return JS_EXCEPTION;
    }
    s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    if (calendar_name == 1 || calendar_name == 2 || (calendar_name == 0 && s->calendar != CAL_ISO8601) || (calendar_name == 3 && s->calendar != CAL_ISO8601)) {
        if (s->iso_year < 0)
            snprintf(buf, sizeof(buf), "-%06d-%02d-%02d", -s->iso_year, s->iso_month, s->iso_day);
        else if (s->iso_year >= 10000)
            snprintf(buf, sizeof(buf), "+%06d-%02d-%02d", s->iso_year, s->iso_month, s->iso_day);
        else
            snprintf(buf, sizeof(buf), "%04d-%02d-%02d", s->iso_year, s->iso_month, s->iso_day);
    } else {
        snprintf(buf, sizeof(buf), "%02d-%02d", s->iso_month, s->iso_day);
    }
    ti_append_calendar_annotation(buf, sizeof(buf), s->calendar, calendar_name);
    return JS_NewString(ctx, buf);
}

static JSValue js_plainmonthday_toJSON(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    return js_plainmonthday_toString(ctx, this_val, 0, NULL);
}

static JSValue js_plainmonthday_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                                int argc, JSValueConst *argv)
{
    JSPlainMonthDayData *s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    return ti_temporal_toLocaleString(ctx, this_val, argc, argv, 1, js_plainmonthday_toString);
}

static JSValue js_plainmonthday_valueOf(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    return JS_ThrowTypeError(ctx, "cannot compare Temporal.PlainMonthDay with < or >");
}

/* Temporal.PlainMonthDay.from */
static JSValue js_plainmonthday_from(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    (void)this_val;
    int overflow_constrain = 1;
    int ref_year = 1972;
    int month = 0;
    int day = 0;
    int has_year = 0;
    JSValue val;
    CalendarType calendar = CAL_ISO8601;
    int month_code_has_l = 0;
    int parsed_month = 0;
    int has_month_code = 0;
    val = argv[0];
    if (JS_IsObject(val)) {
        JSPlainMonthDayData *s = JS_GetOpaque(val, js_plainmonthday_class_id);
        if (s) {
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            return js_create_plainmonthday_with_calendar(ctx, s->iso_year, s->iso_month, s->iso_day, s->calendar);
        }
        {
            TIPreparedFields fields;
            int has_month = 0;
            int has_numeric_month = 0;
            int has_day = 0;
            int has_any = 0;
            JSValue cal_v = JS_GetPropertyStr(ctx, val, "calendar");
            if (JS_IsException(cal_v))
                return JS_EXCEPTION;
            if (!JS_IsUndefined(cal_v)) {
                has_any = 1;
                if (ti_resolve_calendar_value(ctx, cal_v, &calendar) < 0) {
                    JS_FreeValue(ctx, cal_v);
                    return JS_EXCEPTION;
                }
            }
            JS_FreeValue(ctx, cal_v);
            if (ti_prepare_calendar_fields(ctx, val,
                                            TI_FIELD_DAY | TI_FIELD_MONTH | TI_FIELD_MONTH_CODE |
                                            TI_FIELD_YEAR,
                                            0, 0, &fields, calendar) < 0)
                return JS_EXCEPTION;
            if (fields.present_mask)
                has_any = 1;
            /* Extract fields from prepared structure */
            if (fields.present_mask & TI_FIELD_DAY) {
                has_day = 1;
                day = fields.day;
            }
            if (fields.present_mask & TI_FIELD_MONTH) {
                has_month = 1;
                has_numeric_month = 1;
                month = fields.month;
            }
            if (fields.present_mask & TI_FIELD_MONTH_CODE) {
                has_month = 1;
                has_month_code = 1;
                parsed_month = fields.month_from_code;
                month_code_has_l = fields.month_code_has_l;
            }
            if (fields.present_mask & TI_FIELD_YEAR) {
                has_year = 1;
                ref_year = (int)fields.year;
            }
            /* era/eraYear processing */
            if (ti_calendar_requires_era(calendar) &&
                (fields.present_mask & (TI_FIELD_ERA | TI_FIELD_ERA_YEAR))) {
                if (!(fields.present_mask & TI_FIELD_ERA) || !(fields.present_mask & TI_FIELD_ERA_YEAR))
                    return JS_ThrowTypeError(ctx, "era and eraYear must both be present");
                {
                    int iso_year;
                    if (fields.era == TI_ERA_UNKNOWN || ti_calendar_era_to_iso_year(calendar, fields.era, (int)fields.era_year, &iso_year) < 0)
                        return JS_ThrowRangeError(ctx, "invalid era");
                    if (has_year && ref_year != iso_year)
                        return JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
                    ref_year = iso_year;
                    has_year = 1;
                }
            }
            if (!has_any)
                return JS_ThrowTypeError(ctx, "plain object is not a valid property bag and does not convert to a string");
            /* Validate options AFTER reading fields */
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                return JS_EXCEPTION;
            /* Missing property check before algorithmic validation */
            if (!has_month || !has_day)
                return JS_ThrowTypeError(ctx, "invalid PlainMonthDay");
            /* Non-ISO calendars require monthCode or year */
            if (calendar != CAL_ISO8601 && !has_month_code && !has_year) {
                return JS_ThrowTypeError(ctx, "invalid PlainMonthDay");
            }
            /* Non-ISO calendars require year when numeric month is present */
            if (calendar != CAL_ISO8601 && has_numeric_month && !has_year) {
                return JS_ThrowTypeError(ctx, "invalid PlainMonthDay");
            }
            /* Validate monthCode */
            if (fields.present_mask & TI_FIELD_MONTH_CODE) {
                if (!ti_calendar_validate_month_code(calendar, parsed_month, fields.month_code_has_l))
                    return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
                month = parsed_month;
            }
            /* Check month/monthCode agreement */
            if (has_numeric_month && (fields.present_mask & TI_FIELD_MONTH_CODE)) {
                int resolved = ti_calendar_month_code_to_month(calendar, ref_year, parsed_month, fields.month_code_has_l);
                if (resolved >= 0 && fields.month != resolved)
                    return JS_ThrowRangeError(ctx, "month and monthCode must agree");
            }
            /* Bail out early if year is out of supported range for non-ISO calendars */
            if (has_year && calendar != CAL_ISO8601) {
                if (ref_year < -271821 || ref_year > 275760)
                    return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
            }
            /* When only numeric month is provided, convert to monthCode */
            if (has_month && !has_month_code && calendar != CAL_ISO8601 && calendar != CAL_GREGORY) {
                int user_year = has_year ? ref_year : 1972;
                int computed_mc, computed_has_l;
                int max_m = ti_calendar_months_in_year(calendar, user_year);
                if (month < 1 || month > max_m) {
                    if (!overflow_constrain)
                        return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
                    if (month < 1) month = 1;
                    else month = max_m;
                }
                ti_calendar_month_to_month_code(calendar, user_year, month, &computed_mc, &computed_has_l);
                month = computed_mc;
                month_code_has_l = computed_has_l;
                has_month_code = 1;
            }
        }
        if (month < 1)
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
        {
            int iso_y, iso_m, iso_d;
            {
                int overflow_year = has_year ? ref_year : 0;
                if (ti_calendar_monthday_iso_reference_date(calendar, month,
                                                              has_month_code ? month_code_has_l : 0,
                                                              day, overflow_constrain, overflow_year,
                                                              &iso_y, &iso_m, &iso_d) < 0)
                    return JS_ThrowRangeError(ctx, "invalid PlainMonthDay");
                return js_create_plainmonthday_with_calendar(ctx, iso_y, iso_m, iso_d, calendar);
            }
        }
    }
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        TISODateTimeParseResult result;
        int pm_month, pm_day, pm_year;
        CalendarType str_calendar;
        if (!str)
            return JS_EXCEPTION;
        if (ti_is_minus_zero_year(str)) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay string");
        }
        memset(&result, 0, sizeof(result));
        if (ti_parse_iso_datetime_string(ctx, str, &result, TI_PARSE_PLAIN_MONTHDAY) < 0) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay string");
        }
        JS_FreeCString(ctx, str);
        if (result.has_date) {
            pm_month = result.month;
            pm_day = result.day;
            pm_year = result.year;
        } else {
            pm_month = result.month;
            pm_day = result.day;
            pm_year = 0;
        }
        str_calendar = result.has_calendar ? result.calendar : CAL_ISO8601;
        if (str_calendar == CAL_UNKNOWN)
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay string");
        /* For month-day strings without a year, only iso8601 calendar is allowed */
        if (!result.has_date && str_calendar != CAL_ISO8601)
            return JS_ThrowRangeError(ctx, "invalid PlainMonthDay string");
        /* Validate options AFTER string parsing */
        if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
            return JS_EXCEPTION;
        if (argc >= 2 && ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
            return JS_EXCEPTION;
        {
            int str_ref_year = 1972;
            if (str_calendar != CAL_ISO8601) {
                str_ref_year = pm_year;
                if (!ti_is_date_within_iso_limits(str_ref_year, pm_month, pm_day))
                    return JS_ThrowRangeError(ctx, "invalid PlainMonthDay string");
            }
            return js_create_plainmonthday_with_calendar(ctx, str_ref_year, pm_month, pm_day, str_calendar);
        }
    }
    return JS_ThrowTypeError(ctx, "unsupported argument type for Temporal.PlainMonthDay.from");
}

static JSValue js_plainmonthday_toPlainDate(JSContext *ctx, JSValueConst this_val,
                                             int argc, JSValueConst *argv)
{
    JSPlainMonthDayData *s;
    JSValue val;
    int year;
    s = JS_GetOpaque2(ctx, this_val, js_plainmonthday_class_id);
    if (!s)
        return JS_EXCEPTION;
    val = argv[0];
    if (!JS_IsObject(val))
        return JS_ThrowTypeError(ctx, "toPlainDate argument must be an object");
    {
        JSValue v = JS_GetPropertyStr(ctx, val, "year");
        double d;
        int has_year = 0;
        if (!JS_IsUndefined(v)) {
            if (JS_ToFloat64(ctx, &d, v) < 0) {
                JS_FreeValue(ctx, v);
                return JS_EXCEPTION;
            }
            if (ti_is_minus_zero(v)) {
                JS_FreeValue(ctx, v);
                return JS_ThrowRangeError(ctx, "minus zero year is not allowed");
            }
            JS_FreeValue(ctx, v);
            if (!isfinite(d)) {
                JS_ThrowRangeError(ctx, "year must be an integer");
                return JS_EXCEPTION;
            }
            if (d < INT32_MIN || d > INT32_MAX) {
                JS_ThrowRangeError(ctx, "year out of range");
                return JS_EXCEPTION;
            }
            year = (int)(d < 0 ? ceil(d) : floor(d));
            has_year = 1;
        } else {
            JS_FreeValue(ctx, v);
        }
        /* era/eraYear fallback for calendars that support eras */
        if (!has_year && ti_calendar_requires_era(s->calendar)) {
            JSValue era_v = JS_GetPropertyStr(ctx, val, "era");
            JSValue eraYear_v = JS_GetPropertyStr(ctx, val, "eraYear");
            if (!JS_IsUndefined(era_v) || !JS_IsUndefined(eraYear_v)) {
                if (JS_IsUndefined(era_v) || JS_IsUndefined(eraYear_v)) {
                    JS_FreeValue(ctx, era_v);
                    JS_FreeValue(ctx, eraYear_v);
                    return JS_ThrowTypeError(ctx, "era and eraYear must both be present");
                }
                const char *era_str = JS_ToCString(ctx, era_v);
                if (!era_str) {
                    JS_FreeValue(ctx, era_v);
                    JS_FreeValue(ctx, eraYear_v);
                    return JS_EXCEPTION;
                }
                TIEra era = ti_string_to_era(era_str);
                JS_FreeCString(ctx, era_str);
                JS_FreeValue(ctx, era_v);
                if (era == TI_ERA_UNKNOWN) {
                    JS_FreeValue(ctx, eraYear_v);
                    return JS_ThrowRangeError(ctx, "invalid era");
                }
                if (JS_ToFloat64(ctx, &d, eraYear_v) < 0) {
                    JS_FreeValue(ctx, eraYear_v);
                    return JS_EXCEPTION;
                }
                JS_FreeValue(ctx, eraYear_v);
                if (!isfinite(d)) {
                    JS_ThrowRangeError(ctx, "eraYear must be an integer");
                    return JS_EXCEPTION;
                }
                int iso_year;
                if (ti_calendar_era_to_iso_year(s->calendar, era, (int)d, &iso_year) < 0)
                    return JS_ThrowRangeError(ctx, "invalid era");
                year = iso_year;
                has_year = 1;
            } else {
                JS_FreeValue(ctx, era_v);
                JS_FreeValue(ctx, eraYear_v);
            }
        }
        if (!has_year)
            return JS_ThrowTypeError(ctx, "missing year property");
    }
    {
        int cal_y, cal_m, cal_d;
        int iso_y, iso_m, iso_d;
        int day;
        int dim;
        pmd_get_calendar_date(s, &cal_y, &cal_m, &cal_d);
        day = cal_d;
        dim = ti_calendar_days_in_month(s->calendar, year, cal_m);
        if (day > dim || day < 1) {
            int overflow_constrain = 1;
            if (argc >= 2 && ti_validate_options_object(ctx, argv[1]) < 0)
                return JS_EXCEPTION;
            if (argc >= 2 && !JS_IsUndefined(argv[1])) {
                if (ti_get_overflow_option(ctx, argv[1], &overflow_constrain) < 0)
                    return JS_EXCEPTION;
            }
            if (!overflow_constrain)
                return JS_ThrowRangeError(ctx, "invalid PlainDate");
            if (day > dim)
                day = dim;
            else if (day < 1)
                day = 1;
        }
        if (ti_calendar_date_to_iso(s->calendar, year, cal_m, day, &iso_y, &iso_m, &iso_d) < 0)
            return JS_ThrowRangeError(ctx, "invalid PlainDate");
        return js_create_plaindate_with_calendar(ctx, iso_y, iso_m, iso_d, s->calendar);
    }
}

static const JSCFunctionListEntry js_plainmonthday_proto_funcs[] = {
    JS_CGETSET_DEF("monthCode", js_plainmonthday_get_monthCode, NULL),
    JS_CGETSET_DEF("day", js_plainmonthday_get_day, NULL),
    JS_CGETSET_DEF("referenceISOYear", js_plainmonthday_get_referenceISOYear, NULL),
    JS_CGETSET_DEF("calendarId", js_plainmonthday_get_calendarId, NULL),
    JS_CGETSET_DEF("era", js_plainmonthday_get_era, NULL),
    JS_CGETSET_DEF("eraYear", js_plainmonthday_get_eraYear, NULL),
    JS_CGETSET_DEF("inLeapYear", js_plainmonthday_get_inLeapYear, NULL),
    JS_CGETSET_DEF("daysInMonth", js_plainmonthday_get_daysInMonth, NULL),
    JS_CGETSET_DEF("daysInYear", js_plainmonthday_get_daysInYear, NULL),
    JS_CGETSET_DEF("daysInWeek", js_plainmonthday_get_daysInWeek, NULL),
    JS_CGETSET_DEF("monthsInYear", js_plainmonthday_get_monthsInYear, NULL),
    JS_CGETSET_DEF("dayOfYear", js_plainmonthday_get_dayOfYear, NULL),
    JS_CGETSET_DEF("dayOfWeek", js_plainmonthday_get_dayOfWeek, NULL),
    JS_CGETSET_DEF("weekOfYear", js_plainmonthday_get_weekOfYear, NULL),
    JS_CGETSET_DEF("yearOfWeek", js_plainmonthday_get_yearOfWeek, NULL),
    JS_CFUNC_DEF("equals", 1, js_plainmonthday_equals),
    JS_CFUNC_DEF("toString", 0, js_plainmonthday_toString),
    JS_CFUNC_DEF("toJSON", 0, js_plainmonthday_toJSON),
    JS_CFUNC_DEF("toLocaleString", 0, js_plainmonthday_toLocaleString),
    JS_CFUNC_DEF("with", 1, js_plainmonthday_with),
    JS_CFUNC_DEF("toPlainDate", 1, js_plainmonthday_toPlainDate),
    JS_CFUNC_DEF("valueOf", 0, js_plainmonthday_valueOf),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.PlainMonthDay", JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_plainmonthday_static_funcs[] = {
    JS_CFUNC_DEF("from", 1, js_plainmonthday_from),
};

int js_add_plainmonthday(JSContext *ctx, JSValue temporal_obj)
{
    JSValue ctor;
    
    JS_NewClassID(&js_plainmonthday_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_plainmonthday_class_id, &js_plainmonthday_class);
    
    ctor = JS_NewCConstructor(ctx, (int)js_plainmonthday_class_id, "PlainMonthDay",
                               (JSCFunction *)js_plainmonthday_constructor, 2,
                               JS_CFUNC_constructor, 0,
                               JS_UNDEFINED,
                               js_plainmonthday_static_funcs, countof(js_plainmonthday_static_funcs),
                               js_plainmonthday_proto_funcs, countof(js_plainmonthday_proto_funcs),
                               JS_NEW_CTOR_NO_GLOBAL);
    if (JS_IsException(ctor))
        return -1;
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "PlainMonthDay", ctor,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0)
        return -1;
    return 0;
}
