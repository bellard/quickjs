/*
 * QuickJS Temporal calendar implementation - private header
 */

#ifndef TEMPORAL_CALENDAR_H
#define TEMPORAL_CALENDAR_H

#include "../cutils.h"

#define TI_TIMEZONE_BUF_SIZE 128

/* Calendar type enum */
typedef enum {
    CAL_UNKNOWN = -1,
    CAL_BUDDHIST = 0,
    CAL_CHINESE,
    CAL_COPTIC,
    CAL_DANGI,
    CAL_ETHIOAA,
    CAL_ETHIOPIC,
    CAL_GREGORY,
    CAL_HEBREW,
    CAL_INDIAN,
    CAL_ISLAMIC_CIVIL,
    CAL_ISLAMIC_TBLA,
    CAL_ISLAMIC_UMALQURA,
    CAL_ISO8601,
    CAL_JAPANESE,
    CAL_PERSIAN,
    CAL_ROC,
    CAL_COUNT,
} CalendarType;

/* Era enum */
typedef enum {
    TI_ERA_UNKNOWN = 0,
    TI_ERA_CE,
    TI_ERA_BCE,
    TI_ERA_BE,
    TI_ERA_AM,
    TI_ERA_AA,
    TI_ERA_SHAKA,
    TI_ERA_AP,
    TI_ERA_AH,
    TI_ERA_BH,
    TI_ERA_ROC,
    TI_ERA_BROC,
    TI_ERA_MEIJI,
    TI_ERA_TAISHO,
    TI_ERA_SHOWA,
    TI_ERA_HEISEI,
    TI_ERA_REIWA,
} TIEra;

/* Calendar names array (order matches CalendarType enum) */
extern const char *ti_calendar_names[];

/* Calendar string <-> enum conversions */
CalendarType ti_calendar_string_to_enum(const char *str);
const char *ti_calendar_enum_to_string(CalendarType cal);

/* ISO month days table */
extern const int ti_iso_month_days[];

/* Related Gregorian year for Chinese/Dangi dates */
int ti_chinese_related_year(CalendarType cal, int iso_year, int iso_month, int iso_day);

/* Calendar date conversion */
int ti_calendar_date_to_iso(CalendarType cal, int cal_year, int cal_month, int cal_day,
                             int *iso_year, int *iso_month, int *iso_day);
int ti_iso_to_calendar_date(CalendarType cal, int iso_year, int iso_month, int iso_day,
                             int *cal_year, int *cal_month, int *cal_day);

/* Era functions */
int ti_calendar_requires_era(CalendarType cal);
void ti_calendar_era_and_era_year(CalendarType cal, int iso_year, int iso_month, int iso_day,
                                   int cal_year, TIEra *era, int *era_year);
TIEra ti_string_to_era(const char *era);
const char *ti_era_to_string(TIEra era);
int ti_calendar_validate_month_code(CalendarType cal, int month_from_code, int has_l);
int ti_calendar_era_to_iso_year(CalendarType cal, TIEra era, int eraYear, int *iso_year);
int ti_calendar_era_to_calendar_year(CalendarType cal, TIEra era, int eraYear, int *cal_year);
int ti_calendar_monthday_iso_reference_date(CalendarType cal, int month, int has_l, int day,
                                             int overflow_constrain, int overflow_year,
                                             int *iso_year, int *iso_month, int *iso_day);

/* Calendar property functions */
int ti_calendar_is_leap_year(CalendarType cal, int year);
int ti_calendar_days_in_year(CalendarType cal, int year);
int ti_calendar_months_in_year(CalendarType cal, int year);
int ti_calendar_days_in_month(CalendarType cal, int year, int month);
int ti_calendar_day_of_year(CalendarType cal, int year, int month, int day);
int ti_calendar_month_code_to_month(CalendarType cal, int year, int month_code, int has_l);
void ti_calendar_month_to_month_code(CalendarType cal, int year, int month,
                                      int *out_month_code, int *out_has_l);

/* Calendar date arithmetic */
void ti_calendar_date_until(CalendarType cal, int one_year, int one_month, int one_day,
                             int two_year, int two_month, int two_day,
                             int largestUnit,
                             int64_t *out_years, int64_t *out_months,
                             int64_t *out_weeks, int64_t *out_days);
void ti_calendar_date_until_with_monthcode(CalendarType cal,
    int one_year, int one_month, int one_day,
    int one_mc, int one_has_mc, int one_has_l,
    int two_year, int two_month, int two_day,
    int two_mc, int two_has_mc, int two_has_l,
    int largestUnit,
    int64_t *out_years, int64_t *out_months,
    int64_t *out_weeks, int64_t *out_days);
/* Calendar balancing helpers */
void pdt_balance_month_calendar(int *year, int *month, CalendarType cal);
void pdt_balance_date_calendar(int *year, int *month, int *day, CalendarType cal);

#endif /* TEMPORAL_CALENDAR_H */
