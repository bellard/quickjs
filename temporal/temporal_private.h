/*
 * QuickJS Temporal implementation - private header
 */

#ifndef TEMPORAL_PRIVATE_H
#define TEMPORAL_PRIVATE_H

#include "../quickjs.h"
#include "timezone.h"

/* 128-bit integer type: __int128 on 64-bit, _BitInt(128) on 32-bit */
#ifdef __SIZEOF_INT128__
typedef __int128 js_int128_t;
#else
typedef _BitInt(128) js_int128_t;
#endif

/* Temporal limits */
#define INSTANT_MAX_SECS 8640000000000LL

/* JSTimeSpec is used by Intl.DurationFormat and Temporal */
typedef struct JSTimeSpec {
    int64_t secs;
    uint32_t nsecs;
} JSTimeSpec;

JSValue ti_timespec_to_bigint(JSContext *ctx, const JSTimeSpec *ts);
int ti_bigint_to_timespec(JSContext *ctx, JSValueConst val, JSTimeSpec *ts);

int ti_is_partial_temporal_object(JSContext *ctx, JSValueConst obj);



static inline int ti_validate_options_object(JSContext *ctx, JSValueConst v)
{
    if (!JS_IsUndefined(v) && !JS_IsObject(v)) {
        JS_ThrowTypeError(ctx, "options must be an object");
        return -1;
    }
    return 0;
}

#include "calendar.h"

/* PlainTime data */
typedef struct JSPlainTimeData {
    int hour;
    int minute;
    int second;
    int millisecond;
    int microsecond;
    int nanosecond;
} JSPlainTimeData;

/* PlainDateTime data */
typedef struct JSPlainDateTimeData {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int millisecond;
    int microsecond;
    int nanosecond;
    CalendarType calendar;
} JSPlainDateTimeData;

/* Packed js_int128_t wrapper for use in structs that must be 8-byte aligned */
typedef struct __attribute__((packed)) JSPackedInt128 {
    js_int128_t int128_value;
} JSPackedInt128;

/* Duration data (shared with Instant arithmetic) */
typedef struct JSDurationData {
    int64_t years;
    int64_t months;
    int64_t weeks;
    int64_t days;
    int64_t hours;
    int64_t minutes;
    int64_t seconds;
    int64_t milliseconds;
    JSPackedInt128 microseconds;
    JSPackedInt128 nanoseconds;
} JSDurationData;

typedef enum {
    TI_UNIT_AUTO = -102,
    TI_UNIT_INVALID = -100,
    TI_UNIT_YEAR = -4,
    TI_UNIT_MONTH = -3,
    TI_UNIT_WEEK = -2,
    TI_UNIT_DAY = -1,
    TI_UNIT_HOUR = 0,
    TI_UNIT_MINUTE = 1,
    TI_UNIT_SECOND = 2,
    TI_UNIT_MILLISECOND = 3,
    TI_UNIT_MICROSECOND = 4,
    TI_UNIT_NANOSECOND = 5,
} TIUnit;

#define TI_UNIT_OPT_ALLOW_UNDEFINED 1
#define TI_UNIT_OPT_ALLOW_AUTO      2

/* Instant data */
typedef struct {
    JSTimeSpec ts;
} JSInstantData;

/* ZonedDateTime data */
typedef struct {
    JSTimeSpec ts;
    char timeZone[TI_TIMEZONE_BUF_SIZE];
    CalendarType calendar;
} JSZonedDateTimeData;

/* PlainDate data */
typedef struct {
    int year;
    int month;
    int day;
    CalendarType calendar;
} JSPlainDateData;

/* PlainMonthDay data (stores ISO date) */
typedef struct {
    int iso_year;
    int iso_month;
    int iso_day;
    CalendarType calendar;
} JSPlainMonthDayData;

static inline void pmd_get_calendar_date(JSPlainMonthDayData *s, int *cal_year, int *cal_month, int *cal_day)
{
    ti_iso_to_calendar_date(s->calendar, s->iso_year, s->iso_month, s->iso_day, cal_year, cal_month, cal_day);
}

/* PlainYearMonth data (stores ISO date) */
typedef struct {
    int iso_year;
    int iso_month;
    int iso_day;
    CalendarType calendar;
} JSPlainYearMonthData;

static inline void pym_get_calendar_date(JSPlainYearMonthData *s, int *cal_year, int *cal_month, int *cal_day)
{
    ti_iso_to_calendar_date(s->calendar, s->iso_year, s->iso_month, s->iso_day, cal_year, cal_month, cal_day);
}

/* Forward declarations */
extern JSClassID js_plaindate_class_id;
extern JSClassID js_plaintime_class_id;
extern JSClassID js_plaindatetime_class_id;
extern JSClassID js_zoneddatetime_class_id;
extern JSClassID js_plainmonthday_class_id;
extern JSClassID js_plainyearmonth_class_id;
extern JSClassID js_instant_class_id;
JSValue js_create_zoneddatetime(JSContext *ctx, const JSTimeSpec *ts,
                                const char *timeZone, CalendarType calendar);
JSValue js_zoneddatetime_from(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv);
JSValue js_create_instant(JSContext *ctx, const JSTimeSpec *ts);
JSValue js_create_plaintime(JSContext *ctx, const JSPlainTimeData *t);
JSValue js_create_plaindate(JSContext *ctx, int year, int month, int day);
JSValue js_create_plaindate_with_calendar(JSContext *ctx, int year, int month, int day, CalendarType calendar);
JSValue js_create_plaindatetime(JSContext *ctx, int year, int month, int day,
                                 int hour, int minute, int second,
                                 int millisecond, int microsecond, int nanosecond);
JSValue js_create_plaindatetime_with_calendar(JSContext *ctx, int year, int month, int day,
                                                int hour, int minute, int second,
                                                int millisecond, int microsecond, int nanosecond,
                                                CalendarType calendar);
int pdt_is_within_limits(int year, int month, int day,
                                 int hour, int minute, int second,
                                 int millisecond, int microsecond, int nanosecond);
JSValue js_create_plainyearmonth_with_calendar(JSContext *ctx, int year, int month, int referenceISODay, CalendarType calendar);
JSValue js_create_plainmonthday_with_calendar(JSContext *ctx, int iso_year, int iso_month, int iso_day, CalendarType calendar);
int js_to_temporal_duration(JSContext *ctx, JSValueConst item, JSDurationData *out);
int duration_has_mixed_signs(const JSDurationData *d);
int duration_is_out_of_range(const JSDurationData *d);
typedef enum {
    ROUND_CEIL,
    ROUND_FLOOR,
    ROUND_EXPAND,
    ROUND_TRUNC,
    ROUND_HALF_CEIL,
    ROUND_HALF_FLOOR,
    ROUND_HALF_EXPAND,
    ROUND_HALF_TRUNC,
    ROUND_HALF_EVEN,
} TIRoundingMode;

TIUnit ti_string_to_unit(const char *str, size_t len);
int64_t ti_round_to_increment_as_if_positive(int64_t x, int64_t increment, TIRoundingMode mode);
int64_t ti_round_to_increment(int64_t x, int64_t increment, TIRoundingMode mode);
js_int128_t ti_round_to_increment128(js_int128_t x, js_int128_t increment, TIRoundingMode mode);
js_int128_t ti_round_to_increment_as_if_positive128(js_int128_t x, js_int128_t increment, TIRoundingMode mode);
int ti_parse_rounding_mode(JSContext *ctx, JSValueConst val, TIRoundingMode *out);

/* Moved from temporal_private.h inline functions */
int ti_is_minus_zero_year(const char *str);
int ti_is_minus_zero(JSValueConst v);
void ti_format_canonical_timezone(int64_t offset_ns, int is_utc, char *out, size_t out_size);
void ti_append_calendar_annotation(char *buf, int buf_size, CalendarType cal, int calendarName);
int ti_canonicalize_timezone_identifier(const char *str, int64_t offset_ns, char *out, size_t out_size);
int ti_validate_constructor_calendar(JSContext *ctx, JSValueConst v, CalendarType *out);
int ti_validate_timezone_identifier(JSContext *ctx, JSValueConst v, const char **out_str);
JSValue js_create_temporal_object(JSContext *ctx, JSValueConst new_target, JSClassID class_id, void *opaque);
int is_valid_epoch_timespec(const JSTimeSpec *ts);

static inline js_int128_t ti_sec_nsec_to_ns(int64_t secs, uint32_t nsecs)
{
    return (js_int128_t)secs * 1000000000LL + nsecs;
}

static inline js_int128_t ti_timespec_to_ns(const JSTimeSpec *ts)
{
    return ti_sec_nsec_to_ns(ts->secs, ts->nsecs);
}

static inline js_int128_t ti_duration_time_fields_to_ns(const JSDurationData *d)
{
    return (js_int128_t)d->hours * 3600000000000LL
         + (js_int128_t)d->minutes * 60000000000LL
         + (js_int128_t)d->seconds * 1000000000LL
         + (js_int128_t)d->milliseconds * 1000000LL
         + d->microseconds.int128_value * 1000LL
         + d->nanoseconds.int128_value;
}

static inline void ti_ns_to_timespec(js_int128_t ns, JSTimeSpec *ts)
{
    ts->secs = (int64_t)(ns / 1000000000LL);
    ts->nsecs = (uint32_t)(ns % 1000000000LL);
    if (ts->nsecs != 0 && ns < 0) {
        ts->nsecs += 1000000000;
        ts->secs -= 1;
    }
}



JSValue ti_get_month_code(JSContext *ctx, CalendarType cal, int year, int month);
static inline CalendarType ti_calendar_string_to_enum_with_aliases(const char *str)
{
    return ti_calendar_string_to_enum(str);
}
static inline int ti_calendar_enforces_month_monthcode_agreement(CalendarType cal)
{
    /* Hebrew, Chinese, and Dangi calendars allow month and monthCode to disagree
       because month numbers shift in leap years */
    return cal != CAL_HEBREW && cal != CAL_CHINESE && cal != CAL_DANGI;
}

int64_t ti_unit_to_ns(TIUnit unit);
/* Parse a unit option (smallestUnit, largestUnit, unit) from an options object.
   Returns 0 on success, -1 on error (exception thrown).
   flags: bitwise OR of TI_UNIT_OPT_ALLOW_UNDEFINED and/or TI_UNIT_OPT_ALLOW_AUTO.
   If ALLOW_UNDEFINED is set and the property is undefined, *out = default_unit.
   If ALLOW_AUTO is set and the value is "auto", *out = TI_UNIT_AUTO.
   Otherwise throws RangeError for undefined or invalid strings. */
int ti_parse_unit_option(JSContext *ctx, JSValueConst options, const char *prop,
                          TIUnit *out, int flags, TIUnit default_unit);
int ti_get_rounding_increment_option(JSContext *ctx, JSValueConst options, int64_t *out);
int ti_get_rounding_mode_option_default(JSContext *ctx, JSValueConst options, TIRoundingMode *out, int default_mode);
int ti_get_rounding_mode_option(JSContext *ctx, JSValueConst options, TIRoundingMode *out);
int ti_get_calendar_name_option(JSContext *ctx, JSValueConst options, int *out);
int ti_get_fractional_second_digits_option(JSContext *ctx, JSValueConst options, int *out);
TIRoundingMode ti_negate_rounding_mode(TIRoundingMode mode);

/* Map smallestUnit to precision_type used by toString methods.
   Returns: 10=minute, 11=second, 12=ms, 13=us, 14=ns, -1 for invalid. */
static inline int ti_precision_from_smallest_unit(TIUnit unit)
{
    switch (unit) {
    case TI_UNIT_MINUTE: return 10;
    case TI_UNIT_SECOND: return 11;
    case TI_UNIT_MILLISECOND: return 12;
    case TI_UNIT_MICROSECOND: return 13;
    case TI_UNIT_NANOSECOND: return 14;
    default: return -1;
    }
}

/* Create a Temporal object respecting newTarget's prototype per spec */

/* JSTimeSpec helpers for Temporal */


static inline double timespec_to_ms(const JSTimeSpec *ts)
{
    return (double)ts->secs * 1000.0 + (double)ts->nsecs / 1000000.0;
}


void ti_epoch_sec_to_date(int64_t epoch_sec, int64_t *year, int *month, int *day,
                           int *hour, int *minute, int *second);
void ti_zoneddatetime_to_local(const char *timeZone, int64_t epoch_sec, int epoch_nsec,
                                int64_t *out_year, int *out_month, int *out_day,
                                int *out_hour, int *out_minute, int *out_second,
                                int *out_ms, int *out_us, int *out_ns);

static inline int64_t floor_div64(int64_t a, uint64_t b)
{
    int64_t bi = (int64_t)b;
    if (a >= 0)
        return a / bi;
    else
        return (a - bi + 1) / bi;
}

static inline int64_t floor_divrem64(int64_t *pr, int64_t a, uint64_t b)
{
    int64_t bi = (int64_t)b;
    int64_t q = a / bi;
    int64_t r = a % bi;
    if (r < 0) {
        q--;
        r += bi;
    }
    *pr = r;
    return q;
}

static inline int32_t floor_div32(int32_t a, uint32_t b)
{
    int32_t bi = (int32_t)b;
    if (a >= 0)
        return a / bi;
    else
        return (a - bi + 1) / bi;
}



int64_t pdt_to_ordinal_days(int year, int month, int day);
int pd_days_in_month(int year, int month);
int pd_is_valid_date(int year, int month, int day);
int pd_is_leap_year(int year);
int pd_day_of_year(int year, int month, int day);
void pdt_from_ordinal_days(int64_t ordinal, int *year, int *month, int *day);
int ti_is_date_within_iso_limits(int year, int month, int day);
void ti_iso_calendar_date_until(int one_year, int one_month, int one_day,
                                 int two_year, int two_month, int two_day,
                                 TIUnit largestUnit,
                                 int64_t *out_years, int64_t *out_months,
                                 int64_t *out_weeks, int64_t *out_days);
int ti_iso_calendar_date_add(int year, int month, int day,
                              int64_t years, int64_t months, int64_t weeks, int64_t days,
                              int *out_year, int *out_month, int *out_day);
int ti_iso_datetime_add_duration(
    int year, int month, int day,
    int hour, int minute, int second, int ms, int us, int ns,
    int64_t dyears, int64_t dmonths, int64_t dweeks, int64_t ddays,
    int64_t dhours, int64_t dminutes, int64_t dseconds,
    int64_t dms, js_int128_t dus, js_int128_t dns,
    int *out_year, int *out_month, int *out_day,
    int *out_hour, int *out_minute, int *out_second,
    int *out_ms, int *out_us, int *out_ns);
int ti_calendar_datetime_difference(
    CalendarType cal,
    int y1, int m1, int d1, int h1, int min1, int s1, int ms1, int us1, int ns1,
    int y2, int m2, int d2, int h2, int min2, int s2, int ms2, int us2, int ns2,
    TIUnit largestUnit,
    JSDurationData *out);

int ti_iso_datetime_difference(
    int y1, int m1, int d1, int h1, int min1, int s1, int ms1, int us1, int ns1,
    int y2, int m2, int d2, int h2, int min2, int s2, int ms2, int us2, int ns2,
    TIUnit largestUnit,
    JSDurationData *out);
int ti_round_relative_duration_iso(
    JSContext *ctx,
    int rel_year, int rel_month, int rel_day,
    int rel_hour, int rel_minute, int rel_second,
    int rel_ms, int rel_us, int rel_ns,
    const JSDurationData *duration,
    TIUnit largestUnit, TIUnit smallestUnit,
    int64_t increment, TIRoundingMode roundingMode,
    BOOL has_timeZone,
    JSDurationData *out);

void ti_balance_time_duration(JSDurationData *d, TIUnit largestUnit);
int ti_round_iso_date_duration(JSContext *ctx,
                                int origin_year, int origin_month, int origin_day,
                                int dest_year, int dest_month, int dest_day,
                                int64_t rel_time_ns, int64_t dest_time_ns,
                                int64_t years, int64_t months, int64_t weeks, int64_t days,
                                TIUnit largestUnit, TIUnit smallestUnit,
                                int64_t increment, TIRoundingMode rounding_mode,
                                int do_bubble,
                                BOOL has_timeZone,
                                int64_t *out_years, int64_t *out_months,
                                int64_t *out_weeks, int64_t *out_days);

/* ZonedDateTime functions */
JSValue js_zoneddatetime_add(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv);
int ti_zoneddatetime_add_duration_iso(const char *timeZone, CalendarType calendar,
                                       JSTimeSpec origin, JSDurationData *duration,
                                       int overflow_constrain, JSTimeSpec *out);
int ti_zoneddatetime_difference_iso(const char *timeZone, CalendarType calendar,
                                     JSTimeSpec start, JSTimeSpec end,
                                     TIUnit largestUnit, JSDurationData *out);

/* Instant functions */
int js_add_instant(JSContext *ctx, JSValue temporal_obj);

/* Duration functions */
int js_add_duration(JSContext *ctx, JSValue temporal_obj);
int js_parse_duration_string(JSContext *ctx, const char *str, JSDurationData *out);
JSValue js_create_duration(JSContext *ctx, const JSDurationData *d);

/* PlainTime functions */
int js_add_plaintime(JSContext *ctx, JSValue temporal_obj);
JSValue js_plaintime_from(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv);

/* Now functions */
int js_add_now(JSContext *ctx, JSValue temporal_obj);

/* PlainDate functions */
int js_add_plaindate(JSContext *ctx, JSValue temporal_obj);

/* PlainDateTime functions */
int js_add_plaindatetime(JSContext *ctx, JSValue temporal_obj);

/* PlainYearMonth functions */
int js_add_plainyearmonth(JSContext *ctx, JSValue temporal_obj);

/* PlainMonthDay functions */
int js_add_plainmonthday(JSContext *ctx, JSValue temporal_obj);

/* ZonedDateTime functions */
int js_add_zoneddatetime(JSContext *ctx, JSValue temporal_obj);
int js_zoneddatetime_get_timespec(JSContext *ctx, JSValueConst val, JSTimeSpec *ts);

/* TimeZone string parsing */
int ti_parse_temporal_timezone_string(const char *str, int64_t *offset_ns);
void ti_get_default_timezone(char *buf, size_t buf_size);
int ti_get_string_option(JSContext *ctx, JSValueConst options, const char *prop,
                          const char **valid_values, int num_values, int default_index);
int ti_get_overflow_option(JSContext *ctx, JSValueConst options, int *overflow_constrain);

/* Calendar Fields Record - like PrepareCalendarFields from the spec */
#define TI_FIELD_YEAR        (1U << 0)
#define TI_FIELD_MONTH       (1U << 1)
#define TI_FIELD_MONTH_CODE  (1U << 2)
#define TI_FIELD_DAY         (1U << 3)
#define TI_FIELD_HOUR        (1U << 4)
#define TI_FIELD_MINUTE      (1U << 5)
#define TI_FIELD_SECOND      (1U << 6)
#define TI_FIELD_MILLISECOND (1U << 7)
#define TI_FIELD_MICROSECOND (1U << 8)
#define TI_FIELD_NANOSECOND  (1U << 9)
#define TI_FIELD_OFFSET      (1U << 10)
#define TI_FIELD_TIMEZONE    (1U << 11)
#define TI_FIELD_CALENDAR    (1U << 12)
#define TI_FIELD_ERA         (1U << 13)
#define TI_FIELD_ERA_YEAR    (1U << 14)

typedef struct {
    /* Integer fields */
    int64_t year;
    int month, day;
    int hour, minute, second;
    int millisecond, microsecond, nanosecond;
    int64_t era_year;
    /* Derived from monthCode */
    int month_from_code;
    int month_code_has_l;
    /* Parsed offset in nanoseconds */
    int64_t offset_ns;
    /* Time zone identifier */
    char time_zone[TI_TIMEZONE_BUF_SIZE];
    /* Era enum (for non-ISO calendars) */
    TIEra era;
    /* Presence and calendar info */
    uint32_t present_mask;
    CalendarType calendar;
} TIPreparedFields;

int ti_resolve_date_from_fields(JSContext *ctx, TIPreparedFields *fields, int overflow_constrain,
                                 int *year, int *month, int *day, BOOL require_day);
int ti_resolve_calendar_value(JSContext *ctx, JSValue cal_v, CalendarType *out);
int ti_resolve_calendar_from_object(JSContext *ctx, JSValueConst obj, CalendarType *out);
int ti_prepare_calendar_fields(JSContext *ctx, JSValueConst obj,
                                uint32_t field_mask, uint32_t required_mask,
                                int partial, TIPreparedFields *out,
                                CalendarType calendar);

/* Helpers to create Temporal objects from prepared fields without JS calls */
JSValue ti_create_plaindate_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                         int overflow_constrain);
JSValue ti_create_zoneddatetime_from_fields(JSContext *ctx, TIPreparedFields *fields,
                                             int overflow_constrain, JSValueConst options);



typedef struct TISODateTimeParseResult {
    int has_date;
    int has_yearmonth;
    int has_monthday;
    int year, month, day;
    int has_time;
    int hour, minute, second;
    int millisecond, microsecond, nanosecond;
    int has_utc;
    int has_tz_offset;
    int tz_offset_seconds;
    int tz_offset_nanoseconds;
    int tz_offset_has_seconds;
    int has_calendar;
    CalendarType calendar;
    int calendar_critical;
    int has_unknown_critical;
    int has_tz_annotation;
    char tz_annotation[64];
} TISODateTimeParseResult;

#define TI_PARSE_DEFAULT        0
#define TI_PARSE_PLAIN_TIME     1  /* Reject Z, require time, reject ambiguous date-like strings without T prefix */
#define TI_PARSE_PLAIN_MONTHDAY 2  /* Accept --MM-DD, MM-DD, MMDD; reject time/Z/offset without full date */

int ti_parse_iso_datetime_string(JSContext *ctx, const char *str, TISODateTimeParseResult *out, int flags);

/* Helper for Temporal.prototype.toLocaleString using Intl.DateTimeFormat.
   Falls back to toString_func if Intl is not available. */
JSValue ti_temporal_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv,
                                    int default_mode,
                                    JSValue (*toString_func)(JSContext *, JSValueConst, int, JSValueConst *));

#endif /* TEMPORAL_PRIVATE_H */
