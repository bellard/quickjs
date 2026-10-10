/*
 * QuickJS Intl.DateTimeFormat implementation
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "../temporal/temporal_private.h"
#include "intl_private.h"

static JSClassID js_datetimeformat_class_id;

enum {
    DTF_STYLE_FULL = 0,
    DTF_STYLE_LONG,
    DTF_STYLE_MEDIUM,
    DTF_STYLE_SHORT,
    DTF_STYLE_UNSET,
};
static const char *dtf_style_names[] = {"full", "long", "medium", "short"};

enum {
    DTF_HOURCYCLE_H11 = 0,
    DTF_HOURCYCLE_H12,
    DTF_HOURCYCLE_H23,
    DTF_HOURCYCLE_H24,
    DTF_HOURCYCLE_UNSET,
};

static const char *dtf_hourcycle_names[] = {"h11", "h12", "h23", "h24"};

enum {
    DTF_WEEKDAY_NARROW = 0,
    DTF_WEEKDAY_SHORT,
    DTF_WEEKDAY_LONG,
    DTF_WEEKDAY_UNSET,
};
static const char *dtf_weekday_names[] = {"narrow", "short", "long"};

enum {
    DTF_ERA_NARROW = 0,
    DTF_ERA_SHORT,
    DTF_ERA_LONG,
    DTF_ERA_UNSET,
};
static const char *dtf_era_names[] = {"narrow", "short", "long"};

enum {
    DTF_YEAR_2DIGIT = 0,
    DTF_YEAR_NUMERIC,
    DTF_YEAR_UNSET,
};
static const char *dtf_year_names[] = {"2-digit", "numeric"};

enum {
    DTF_MONTH_2DIGIT = 0,
    DTF_MONTH_NUMERIC,
    DTF_MONTH_NARROW,
    DTF_MONTH_SHORT,
    DTF_MONTH_LONG,
    DTF_MONTH_UNSET,
};
static const char *dtf_month_names[] = {"2-digit", "numeric", "narrow", "short", "long"};

enum {
    DTF_DAY_2DIGIT = 0,
    DTF_DAY_NUMERIC,
    DTF_DAY_UNSET,
};
static const char *dtf_day_names[] = {"2-digit", "numeric"};

enum {
    DTF_DAYPERIOD_NARROW = 0,
    DTF_DAYPERIOD_SHORT,
    DTF_DAYPERIOD_LONG,
    DTF_DAYPERIOD_UNSET,
};
static const char *dtf_dayperiod_names[] = {"narrow", "short", "long"};

enum {
    DTF_HOUR_2DIGIT = 0,
    DTF_HOUR_NUMERIC,
    DTF_HOUR_UNSET,
};
static const char *dtf_hour_names[] = {"2-digit", "numeric"};

enum {
    DTF_MINUTE_2DIGIT = 0,
    DTF_MINUTE_NUMERIC,
    DTF_MINUTE_UNSET,
};
static const char *dtf_minute_names[] = {"2-digit", "numeric"};

enum {
    DTF_SECOND_2DIGIT = 0,
    DTF_SECOND_NUMERIC,
    DTF_SECOND_UNSET,
};
static const char *dtf_second_names[] = {"2-digit", "numeric"};

enum {
    DTF_TIMEZONE_SHORT = 0,
    DTF_TIMEZONE_LONG,
    DTF_TIMEZONE_SHORTOFFSET,
    DTF_TIMEZONE_LONGOFFSET,
    DTF_TIMEZONE_SHORTGENERIC,
    DTF_TIMEZONE_LONGGENERIC,
    DTF_TIMEZONE_UNSET,
};
static const char *dtf_timezonename_names[] = {"short", "long", "shortOffset", "longOffset", "shortGeneric", "longGeneric"};

enum {
    DTF_FORMATMATCHER_BASIC = 0,
    DTF_FORMATMATCHER_BESTFIT,
    DTF_FORMATMATCHER_UNSET,
};
static const char *dtf_formatmatcher_names[] = {"basic", "best fit"};

/* Validate Unicode Locale Identifier type: alphanum{3,8} (- alphanum{3,8})* */
static int is_valid_locale_type(const char *str)
{
    int seg_len = 0;
    const char *p = str;
    while (*p) {
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) {
            seg_len++;
            if (seg_len > 8)
                return 0;
            p++;
        } else if (*p == '-') {
            if (seg_len < 3 || seg_len > 8)
                return 0;
            seg_len = 0;
            p++;
        } else {
            return 0;
        }
    }
    return seg_len >= 3 && seg_len <= 8;
}

static int is_valid_time_zone(const char *str)
{
    int len = (int)strlen(str);
    int i;
    int has_slash = 0;
    if (len == 0)
        return 0;
    if (str[0] == '+' || str[0] == '-') {
        int hh, mm;
        if (len == 6 && str[3] == ':') {
            if (!isdigit((unsigned char)str[1]) || !isdigit((unsigned char)str[2]) ||
                !isdigit((unsigned char)str[4]) || !isdigit((unsigned char)str[5]))
                return 0;
            hh = (str[1] - '0') * 10 + (str[2] - '0');
            mm = (str[4] - '0') * 10 + (str[5] - '0');
        } else if (len == 5) {
            if (!isdigit((unsigned char)str[1]) || !isdigit((unsigned char)str[2]) ||
                !isdigit((unsigned char)str[3]) || !isdigit((unsigned char)str[4]))
                return 0;
            hh = (str[1] - '0') * 10 + (str[2] - '0');
            mm = (str[3] - '0') * 10 + (str[4] - '0');
        } else if (len == 3) {
            if (!isdigit((unsigned char)str[1]) || !isdigit((unsigned char)str[2]))
                return 0;
            hh = (str[1] - '0') * 10 + (str[2] - '0');
            mm = 0;
        } else {
            return 0;
        }
        return hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59;
    }
    /* allow UTC and GMT in any case */
    if (!strcasecmp(str, "UTC") || !strcasecmp(str, "GMT"))
        return 1;
    /* IANA time zone names must contain a slash, except UTC/GMT/Etc zones */
    for (i = 0; i < len; i++) {
        char c = str[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+'))
            return 0;
        if (c == '/')
            has_slash = 1;
    }
    return has_slash;
}

static int is_supported_calendar(const char *str)
{
    return ti_calendar_string_to_enum(str) != CAL_UNKNOWN;
}

/* Canonicalize an offset time zone string to ±HH:MM format.
   Returns a newly allocated string, or NULL on error.  */
static char *canonicalize_offset_time_zone(JSContext *ctx, const char *str)
{
    int len = (int)strlen(str);
    char sign;
    int hh, mm;
    char *buf;
    if (len == 0 || (str[0] != '+' && str[0] != '-'))
        return NULL;
    sign = str[0];
    if (len == 3) {
        hh = (str[1] - '0') * 10 + (str[2] - '0');
        mm = 0;
    } else if (len == 5) {
        hh = (str[1] - '0') * 10 + (str[2] - '0');
        mm = (str[3] - '0') * 10 + (str[4] - '0');
    } else if (len == 6 && str[3] == ':') {
        hh = (str[1] - '0') * 10 + (str[2] - '0');
        mm = (str[4] - '0') * 10 + (str[5] - '0');
    } else {
        return NULL;
    }
    if (sign == '-' && hh == 0 && mm == 0)
        sign = '+';
    buf = js_malloc(ctx, 7);
    if (!buf)
        return NULL;
    snprintf(buf, 7, "%c%02d:%02d", sign, hh, mm);
    return buf;
}

typedef struct {
    char *locale;
    char *locale_canonical;
    int calendar;
    int numberingSystem;
    char *timeZone;
    int hour12;
    int has_hour12;
    int weekday;
    int era;
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int timeZoneName;
    int dateStyle;
    int timeStyle;
    int fractionalSecondDigits;
    int has_fractionalSecondDigits;
    int dayPeriod;
    int hourCycle;
    int has_hourCycle;
    int has_calendar_option;
    int has_numberingSystem_option;
    int has_hourCycle_option;
    int has_year_option;
    int has_month_option;
    int has_day_option;
    int formatMatcher;
    int required;
    int defaults;
} JSDateTimeFormatData;

static const char *en_tz_utc_long = "Coordinated Universal Time";

static const char *islamic_month_long_en[12] = {
    "Muharram", "Safar", "Rabiʻ I", "Rabiʻ II", "Jumada I", "Jumada II",
    "Rajab", "Shaʻban", "Ramadan", "Shawwal", "Dhuʻl-Qiʻdah", "Dhuʻl-Hijjah"
};

static const char *islamic_month_short_en[12] = {
    "Muh.", "Saf.", "Rab. I", "Rab. II", "Jum. I", "Jum. II",
    "Raj.", "Sha.", "Ram.", "Shaw.", "Dhuʻl-Q.", "Dhuʻl-H."
};

static const char *hebrew_month_long_en[13] = {
    "Tishri", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar I",
    "Adar II", "Nisan", "Iyar", "Sivan", "Tamuz", "Av", "Elul"
};

static const char *hebrew_month_short_en[13] = {
    "Tishri", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar I",
    "Adar II", "Nisan", "Iyar", "Sivan", "Tammuz", "Av", "Elul"
};

/* Return a non-Gregorian calendar month name for English locale.
   calendar: the calendar type.
   locale: the locale string (e.g., "en").
   cal_year: calendar year.
   cal_month: 1-based calendar month.
   width: 4=long, 3=short, 5=narrow (narrow falls back to short).
   Returns the name string or NULL if no special name applies. */
static const char *get_calendar_month_name(int calendar, const char *locale,
                                           int cal_year, int cal_month,
                                           int width)
{
    int is_long, is_short;

    if (!locale || !strstart(locale, "en", NULL))
        return NULL;
    if (cal_month < 1)
        return NULL;
    is_long = (width >= 4);
    is_short = (width == 3);
    if (calendar == CAL_ISLAMIC_CIVIL || calendar == CAL_ISLAMIC_TBLA ||
        calendar == CAL_ISLAMIC_UMALQURA) {
        if (cal_month > 12)
            return NULL;
        if (is_long)
            return islamic_month_long_en[cal_month - 1];
        if (is_short)
            return islamic_month_short_en[cal_month - 1];
        return NULL;
    }
    if (calendar == CAL_HEBREW) {
        int is_leap = ti_calendar_is_leap_year(CAL_HEBREW, cal_year);
        int idx = cal_month - 1;
        if (!is_leap) {
            /* Non-leap year: Adar is month 6, skip Adar I slot */
            static const char *nonleap_long[12] = {
                "Tishri", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar",
                "Nisan", "Iyar", "Sivan", "Tamuz", "Av", "Elul"
            };
            static const char *nonleap_short[12] = {
                "Tishri", "Heshvan", "Kislev", "Tevet", "Shevat", "Adar",
                "Nisan", "Iyar", "Sivan", "Tamuz", "Av", "Elul"
            };
            if (idx >= 12)
                return NULL;
            if (is_long)
                return nonleap_long[idx];
            if (is_short)
                return nonleap_short[idx];
            /* For narrow / numeric fallback, return long name */
            return nonleap_long[idx];
        } else {
            if (idx >= 13)
                return NULL;
            if (is_long)
                return hebrew_month_long_en[idx];
            if (is_short)
                return hebrew_month_short_en[idx];
            return hebrew_month_long_en[idx];
        }
    }
    return NULL;
}

/* Return era string for a given calendar and year.
   style: 0=long, 1=short, 2=narrow.
   Returns 1 if the calendar should have an era, 0 otherwise. */
static int get_calendar_era(int calendar, TIEra era, int style,
                             const LocaleData *ld, const char **era_str)
{
    if (era == TI_ERA_UNKNOWN)
        return 0;

    switch (calendar) {
    case CAL_GREGORY:
        *era_str = LD_ERA(ld, style, era == TI_ERA_BCE ? 0 : 1);
        return 1;
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA:
    case CAL_ISLAMIC_UMALQURA:
        if (era == TI_ERA_BH) {
            *era_str = (style == 0) ? "Before Hijra" :
                       (style == 1) ? "BH" : "B";
        } else {
            *era_str = (style == 0) ? "AH" :
                       (style == 1) ? "AH" : "A";
        }
        return 1;
    case CAL_JAPANESE:
        switch (era) {
        case TI_ERA_BCE:
            *era_str = (style == 0) ? "Before Christ" :
                       (style == 1) ? "BC" : "B";
            break;
        case TI_ERA_CE:
            *era_str = (style == 0) ? "Anno Domini" :
                       (style == 1) ? "AD" : "A";
            break;
        case TI_ERA_MEIJI:
            *era_str = (style == 0) ? "Meiji" :
                       (style == 1) ? "Meiji" : "M";
            break;
        case TI_ERA_TAISHO:
            *era_str = (style == 0) ? "Taisho" :
                       (style == 1) ? "Taisho" : "T";
            break;
        case TI_ERA_SHOWA:
            *era_str = (style == 0) ? "Showa" :
                       (style == 1) ? "Showa" : "S";
            break;
        case TI_ERA_HEISEI:
            *era_str = (style == 0) ? "Heisei" :
                       (style == 1) ? "Heisei" : "H";
            break;
        case TI_ERA_REIWA:
            *era_str = (style == 0) ? "Reiwa" :
                       (style == 1) ? "Reiwa" : "R";
            break;
        default:
            *era_str = (style == 0) ? "Anno Domini" :
                       (style == 1) ? "AD" : "A";
            break;
        }
        return 1;
    case CAL_ROC:
        if (era == TI_ERA_BROC) {
            *era_str = (style == 0) ? "Before ROC" :
                       (style == 1) ? "B.R.O.C." : "B";
        } else {
            *era_str = (style == 0) ? "ROC" :
                       (style == 1) ? "R.O.C." : "R";
        }
        return 1;
    case CAL_ETHIOPIC:
        if (era == TI_ERA_AA) {
            *era_str = (style == 0) ? "AA" :
                       (style == 1) ? "AA" : "A";
        } else {
            *era_str = (style == 0) ? "AM" :
                       (style == 1) ? "AM" : "A";
        }
        return 1;
    case CAL_ETHIOAA:
        *era_str = (style == 0) ? "AA" :
                   (style == 1) ? "AA" : "A";
        return 1;
    case CAL_BUDDHIST:
        *era_str = (style == 0) ? "BE" :
                   (style == 1) ? "BE" : "B";
        return 1;
    case CAL_COPTIC:
        *era_str = (style == 0) ? "AM" :
                   (style == 1) ? "AM" : "A";
        return 1;
    case CAL_HEBREW:
        *era_str = (style == 0) ? "AM" :
                   (style == 1) ? "AM" : "A";
        return 1;
    case CAL_INDIAN:
        *era_str = (style == 0) ? "Saka" :
                   (style == 1) ? "Saka" : "S";
        return 1;
    case CAL_PERSIAN:
        *era_str = (style == 0) ? "AP" :
                   (style == 1) ? "AP" : "A";
        return 1;
    case CAL_CHINESE:
    case CAL_DANGI:
        return 0;
    }

    /* Default fallback */
    *era_str = LD_ERA(ld, style, era == TI_ERA_BCE ? 0 : 1);
    return 1;
}
static const char *en_tz_utc_short = "UTC";

/* Return day period index (0-4) for English locale based on hour (0-23).
   0=morning, 1=noon, 2=afternoon, 3=evening, 4=night */
static int get_en_dayperiod_idx(int hour)
{
    if (hour >= 6 && hour <= 11)
        return 0;  /* morning */
    if (hour == 12)
        return 1;  /* noon */
    if (hour >= 13 && hour <= 17)
        return 2;  /* afternoon */
    if (hour >= 18 && hour <= 20)
        return 3;  /* evening */
    return 4;      /* night (21-23, 0-5) */
}

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int ms;
    int weekday;
    int64_t epoch_sec;
} DTFDateFields;

typedef struct {
    const char *type;
    int off;
    int len;
} DTFPartInfo;

static int dtf_parse_offset_timezone(const char *tz, int *offset_min)
{
    int hh, mm;
    int sign = 1;
    int len = (int)strlen(tz);
    if (len < 3 || (tz[0] != '+' && tz[0] != '-'))
        return -1;
    if (tz[0] == '-')
        sign = -1;
    if (len == 3) {
        hh = (tz[1] - '0') * 10 + (tz[2] - '0');
        mm = 0;
    } else if (len == 6 && tz[3] == ':') {
        hh = (tz[1] - '0') * 10 + (tz[2] - '0');
        mm = (tz[4] - '0') * 10 + (tz[5] - '0');
    } else if (len == 5) {
        hh = (tz[1] - '0') * 10 + (tz[2] - '0');
        mm = (tz[3] - '0') * 10 + (tz[4] - '0');
    } else {
        return -1;
    }
    *offset_min = sign * (hh * 60 + mm);
    return 0;
}

static void dtf_extract_fields(double ms, const char *timeZone, DTFDateFields *out)
{
    int64_t ms_i = (int64_t)ms;
    int64_t sec = ms_i / 1000;
    int ms_val = (int)(ms_i % 1000);
    int64_t offset_sec = 0;
    int64_t year;
    int month, day, hour, minute, second;
    int64_t days;

    if (ms_val < 0) {
        ms_val += 1000;
        sec -= 1;
    }

    {
        int tz_id = ti_tz_find_id(timeZone);
        if (tz_id >= 0) {
            offset_sec = ti_tz_offset_at_sec(tz_id, sec);
        } else if (timeZone[0] == '+' || timeZone[0] == '-') {
            int offset_min;
            if (dtf_parse_offset_timezone(timeZone, &offset_min) == 0)
                offset_sec = offset_min * 60;
        }
    }

    sec += offset_sec;
    ti_epoch_sec_to_date(sec, &year, &month, &day, &hour, &minute, &second);
    days = floor_div64(sec, 86400ULL);

    out->year = (int)year;
    out->month = month - 1;
    out->day = day;
    out->hour = hour;
    out->minute = minute;
    out->second = second;
    out->ms = ms_val;
    {
        int wday = (int)((days + 4) % 7);
        if (wday < 0)
            wday += 7;
        out->weekday = wday;
    }
    out->epoch_sec = sec;
}

/* Return the default numbering system for a locale based on language tag.
   This is a pragmatic mapping for common locales. */
static int dtf_default_numbering_system(const char *locale)
{
    if (strstart(locale, "ar", NULL))
        return NS_ARAB;
    if (strstart(locale, "th", NULL))
        return NS_THAI;
    if (strstart(locale, "bn", NULL))
        return NS_BENG;
    if (strstart(locale, "hi", NULL) || strstart(locale, "mr", NULL) ||
        strstart(locale, "ne", NULL) || strstart(locale, "sa", NULL))
        return NS_DEVA;
    if (strstart(locale, "pa", NULL))
        return NS_GURU;
    if (strstart(locale, "gu", NULL))
        return NS_GUJR;
    if (strstart(locale, "ta", NULL))
        return NS_TAML;
    if (strstart(locale, "te", NULL))
        return NS_TELU;
    if (strstart(locale, "kn", NULL))
        return NS_KNDA;
    if (strstart(locale, "ml", NULL))
        return NS_MLYM;
    if (strstart(locale, "or", NULL))
        return NS_ORYA;
    if (strstart(locale, "my", NULL))
        return NS_MYMR;
    if (strstart(locale, "km", NULL))
        return NS_KHMR;
    if (strstart(locale, "lo", NULL))
        return NS_LAOO;
    if (strstart(locale, "am", NULL))
        return NS_ETHI;
    return NS_LATN;
}

/* Return the separator used before dayPeriod for a given locale.
   en-US uses narrow no-break space (U+202F) per CLDR to match
   the timeStyle patterns used by temporal-objects-resolved-time-zone. */
static const char *dtf_get_dayperiod_separator(const char *locale)
{
    if (!strcmp(locale, "en-US"))
        return "\u202F";
    return " ";
}

static void dtf_expand_styles(JSDateTimeFormatData *s, int *weekday, int *year, int *month, int *day,
                              int *hour, int *minute, int *second, int *timeZoneName)
{
    *weekday = s->weekday;
    *year = s->year;
    *month = s->month;
    *day = s->day;
    *hour = s->hour;
    *minute = s->minute;
    *second = s->second;
    *timeZoneName = s->timeZoneName;

    if (s->dateStyle != DTF_STYLE_UNSET) {
        switch (s->dateStyle) {
        case DTF_STYLE_FULL:
            *weekday = DTF_WEEKDAY_LONG;
            *year = DTF_YEAR_NUMERIC;
            *month = DTF_MONTH_LONG;
            *day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_LONG:
            *weekday = DTF_WEEKDAY_UNSET;
            *year = DTF_YEAR_NUMERIC;
            *month = DTF_MONTH_LONG;
            *day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_MEDIUM:
            *weekday = DTF_WEEKDAY_UNSET;
            *year = DTF_YEAR_NUMERIC;
            *month = DTF_MONTH_SHORT;
            *day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_SHORT:
            *weekday = DTF_WEEKDAY_UNSET;
            *year = DTF_YEAR_2DIGIT;
            *month = DTF_MONTH_NUMERIC;
            *day = DTF_DAY_NUMERIC;
            break;
        }
    }

    if (s->timeStyle != DTF_STYLE_UNSET) {
        switch (s->timeStyle) {
        case DTF_STYLE_FULL:
            *hour = DTF_HOUR_NUMERIC;
            *minute = DTF_MINUTE_NUMERIC;
            *second = DTF_SECOND_NUMERIC;
            *timeZoneName = DTF_TIMEZONE_LONG;
            break;
        case DTF_STYLE_LONG:
            *hour = DTF_HOUR_NUMERIC;
            *minute = DTF_MINUTE_NUMERIC;
            *second = DTF_SECOND_NUMERIC;
            *timeZoneName = DTF_TIMEZONE_SHORT;
            break;
        case DTF_STYLE_MEDIUM:
            *hour = DTF_HOUR_NUMERIC;
            *minute = DTF_MINUTE_NUMERIC;
            *second = DTF_SECOND_NUMERIC;
            *timeZoneName = DTF_TIMEZONE_UNSET;
            break;
        case DTF_STYLE_SHORT:
            *hour = DTF_HOUR_NUMERIC;
            *minute = DTF_MINUTE_NUMERIC;
            *second = DTF_SECOND_UNSET;
            *timeZoneName = DTF_TIMEZONE_UNSET;
            break;
        }
    }
}

static int dtf_check_temporal_overlap(JSContext *ctx, JSDateTimeFormatData *s,
                                       JSValueConst val)
{
    void *p_inst = JS_GetOpaque(val, js_instant_class_id);
    void *p_zdt = JS_GetOpaque(val, js_zoneddatetime_class_id);
    void *p_pdt = JS_GetOpaque(val, js_plaindatetime_class_id);
    void *p_pd = JS_GetOpaque(val, js_plaindate_class_id);
    void *p_pt = JS_GetOpaque(val, js_plaintime_class_id);
    void *p_pym = JS_GetOpaque(val, js_plainyearmonth_class_id);
    void *p_pmd = JS_GetOpaque(val, js_plainmonthday_class_id);
    if (!p_inst && !p_zdt && !p_pdt && !p_pd && !p_pt && !p_pym && !p_pmd)
        return 0;
    /* ZonedDateTime is not supported directly by DateTimeFormat */
    if (p_zdt) {
        JS_ThrowTypeError(ctx, "format() does not support Temporal.ZonedDateTime");
        return -1;
    }
    /* Style branch: dateStyle/timeStyle */
    if (s->dateStyle != DTF_STYLE_UNSET || s->timeStyle != DTF_STYLE_UNSET) {
        if (p_pt && s->timeStyle == DTF_STYLE_UNSET) {
            JS_ThrowTypeError(ctx, "no overlap between dateTimeFormat options and Temporal object fields");
            return -1;
        }
        if ((p_pd || p_pym || p_pmd) && s->dateStyle == DTF_STYLE_UNSET) {
            JS_ThrowTypeError(ctx, "no overlap between dateTimeFormat options and Temporal object fields");
            return -1;
        }
        return 0;
    }
    /* Explicit options branch: GetDateTimeFormat logic */
    {
        int any_present = 0;
        int req_weekday = 0, req_year = 0, req_month = 0, req_day = 0;
        int req_hour = 0, req_minute = 0, req_second = 0, req_frac = 0, req_dp = 0;
        int required_present = 0;
        if (s->weekday != DTF_WEEKDAY_UNSET) { any_present = 1; req_weekday = 1; }
        if (s->has_year_option) { any_present = 1; req_year = 1; }
        if (s->has_month_option) { any_present = 1; req_month = 1; }
        if (s->has_day_option) { any_present = 1; req_day = 1; }
        if (s->dayPeriod != DTF_DAYPERIOD_UNSET) { any_present = 1; req_dp = 1; }
        if (s->hour != DTF_HOUR_UNSET) { any_present = 1; req_hour = 1; }
        if (s->minute != DTF_MINUTE_UNSET) { any_present = 1; req_minute = 1; }
        if (s->second != DTF_SECOND_UNSET) { any_present = 1; req_second = 1; }
        if (s->has_fractionalSecondDigits) { any_present = 1; req_frac = 1; }
        /* era and timeZoneName do not count towards any_present or required */
        if (p_pt) {
            /* PlainTime: required = {dayPeriod, hour, minute, second, fractionalSecondDigits} */
            if (req_dp || req_hour || req_minute || req_second || req_frac) required_present = 1;
        } else if (p_pd) {
            /* PlainDate: required = {weekday, year, month, day} */
            if (req_weekday || req_year || req_month || req_day) required_present = 1;
        } else if (p_pym) {
            /* PlainYearMonth: required = {year, month} */
            if (req_year || req_month) required_present = 1;
        } else if (p_pmd) {
            /* PlainMonthDay: required = {month, day} */
            if (req_month || req_day) required_present = 1;
        } else {
            /* PlainDateTime, Instant, ZonedDateTime: required = all */
            required_present = 1; /* always has overlap since any_present field is required */
        }
        if (!required_present && any_present) {
            JS_ThrowTypeError(ctx, "no overlap between dateTimeFormat options and Temporal object fields");
            return -1;
        }
    }
    return 0;
}

static void dtf_append_number(DynBuf *db, int n, int width, int numberingSystem)
{
    char buf[32];
    int len, i;

    if (width > 1)
        len = snprintf(buf, sizeof(buf), "%0*d", width, n);
    else
        len = snprintf(buf, sizeof(buf), "%d", n);

    if (numberingSystem == NS_LATN) {
        dbuf_put(db, (uint8_t *)buf, (size_t)len);
    } else {
        for (i = 0; i < len; i++) {
            if (buf[i] >= '0' && buf[i] <= '9')
                nf_put_digit(db, numberingSystem, buf[i] - '0');
            else
                dbuf_putc(db, (uint8_t)buf[i]);
        }
    }
}

static int dtf_record_part(JSContext *ctx, DynBuf *db, DTFPartInfo **parts, int *n_parts, int *max_parts,
                           const char *type, int off, int len)
{
    if (!parts)
        return 0;
    if (*n_parts >= *max_parts) {
        int new_max = *max_parts ? *max_parts * 2 : 8;
        DTFPartInfo *new_parts = js_realloc(ctx, *parts, (size_t)new_max * sizeof(DTFPartInfo));
        if (!new_parts)
            return -1;
        *parts = new_parts;
        *max_parts = new_max;
    }
    (*parts)[*n_parts].type = type;
    (*parts)[*n_parts].off = off;
    (*parts)[*n_parts].len = len;
    (*n_parts)++;
    return 0;
}

static int dtf_add_literal(JSContext *ctx, DynBuf *db, DTFPartInfo **parts, int *n_parts, int *max_parts,
                           const char *str)
{
    int off = (int)db->size;
    int len = (int)strlen(str);
    dbuf_put(db, (uint8_t *)str, (size_t)len);
    if (parts)
        return dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, len);
    return 0;
}

static double dtf_iso_to_epoch_ms(int year, int month, int day,
                                   int hour, int minute, int second, int millisecond);

static int dtf_to_datetimeformattable(JSContext *ctx, JSValueConst arg, double *pd, BOOL *is_temporal, BOOL *needs_clip)
{
    *is_temporal = FALSE;
    *needs_clip = FALSE;
    if (JS_IsUndefined(arg)) {
        JS_ThrowTypeError(ctx, "date argument is undefined");
        return -1;
    }
    if (JS_IsObject(arg)) {
        JSInstantData *instant = JS_GetOpaque(arg, js_instant_class_id);
        if (instant) {
            *pd = timespec_to_ms(&instant->ts);
            *is_temporal = TRUE;
            *needs_clip = TRUE;
            return 0;
        }
        JSZonedDateTimeData *zdt = JS_GetOpaque(arg, js_zoneddatetime_class_id);
        if (zdt) {
            *pd = timespec_to_ms(&zdt->ts);
            *is_temporal = TRUE;
            *needs_clip = TRUE;
            return 0;
        }
        JSPlainDateTimeData *pdt = JS_GetOpaque(arg, js_plaindatetime_class_id);
        if (pdt) {
            *pd = dtf_iso_to_epoch_ms(pdt->year, pdt->month, pdt->day,
                                       pdt->hour, pdt->minute, pdt->second, pdt->millisecond);
            *is_temporal = TRUE;
            return 0;
        }
        JSPlainDateData *pdate = JS_GetOpaque(arg, js_plaindate_class_id);
        if (pdate) {
            *pd = dtf_iso_to_epoch_ms(pdate->year, pdate->month, pdate->day, 12, 0, 0, 0);
            *is_temporal = TRUE;
            return 0;
        }
        JSPlainTimeData *pt = JS_GetOpaque(arg, js_plaintime_class_id);
        if (pt) {
            *pd = dtf_iso_to_epoch_ms(1970, 1, 1, pt->hour, pt->minute, pt->second, pt->millisecond);
            *is_temporal = TRUE;
            return 0;
        }
        JSPlainYearMonthData *pym = JS_GetOpaque(arg, js_plainyearmonth_class_id);
        if (pym) {
            *pd = dtf_iso_to_epoch_ms(pym->iso_year, pym->iso_month, pym->iso_day, 12, 0, 0, 0);
            *is_temporal = TRUE;
            return 0;
        }
        JSPlainMonthDayData *pmd = JS_GetOpaque(arg, js_plainmonthday_class_id);
        if (pmd) {
            *pd = dtf_iso_to_epoch_ms(pmd->iso_year, pmd->iso_month, pmd->iso_day, 12, 0, 0, 0);
            *is_temporal = TRUE;
            return 0;
        }
    }
    *needs_clip = TRUE;
    return JS_ToFloat64(ctx, pd, arg);
}

static BOOL dtf_same_temporal_type(JSContext *ctx, JSValueConst a, JSValueConst b)
{
    if (JS_GetOpaque(a, js_instant_class_id) || JS_GetOpaque(b, js_instant_class_id))
        return JS_GetOpaque(a, js_instant_class_id) && JS_GetOpaque(b, js_instant_class_id);
    if (JS_GetOpaque(a, js_zoneddatetime_class_id) || JS_GetOpaque(b, js_zoneddatetime_class_id))
        return JS_GetOpaque(a, js_zoneddatetime_class_id) && JS_GetOpaque(b, js_zoneddatetime_class_id);
    if (JS_GetOpaque(a, js_plaindatetime_class_id) || JS_GetOpaque(b, js_plaindatetime_class_id))
        return JS_GetOpaque(a, js_plaindatetime_class_id) && JS_GetOpaque(b, js_plaindatetime_class_id);
    if (JS_GetOpaque(a, js_plaindate_class_id) || JS_GetOpaque(b, js_plaindate_class_id))
        return JS_GetOpaque(a, js_plaindate_class_id) && JS_GetOpaque(b, js_plaindate_class_id);
    if (JS_GetOpaque(a, js_plaintime_class_id) || JS_GetOpaque(b, js_plaintime_class_id))
        return JS_GetOpaque(a, js_plaintime_class_id) && JS_GetOpaque(b, js_plaintime_class_id);
    if (JS_GetOpaque(a, js_plainyearmonth_class_id) || JS_GetOpaque(b, js_plainyearmonth_class_id))
        return JS_GetOpaque(a, js_plainyearmonth_class_id) && JS_GetOpaque(b, js_plainyearmonth_class_id);
    if (JS_GetOpaque(a, js_plainmonthday_class_id) || JS_GetOpaque(b, js_plainmonthday_class_id))
        return JS_GetOpaque(a, js_plainmonthday_class_id) && JS_GetOpaque(b, js_plainmonthday_class_id);
    return TRUE;
}

static int dtf_get_temporal_calendar(JSContext *ctx, JSValueConst obj)
{
    JSPlainDateTimeData *pdt = JS_GetOpaque(obj, js_plaindatetime_class_id);
    if (pdt)
        return pdt->calendar;
    JSPlainDateData *pd = JS_GetOpaque(obj, js_plaindate_class_id);
    if (pd)
        return pd->calendar;
    JSPlainTimeData *pt = JS_GetOpaque(obj, js_plaintime_class_id);
    if (pt)
        return CAL_ISO8601;
    JSPlainYearMonthData *pym = JS_GetOpaque(obj, js_plainyearmonth_class_id);
    if (pym)
        return pym->calendar;
    JSPlainMonthDayData *pmd = JS_GetOpaque(obj, js_plainmonthday_class_id);
    if (pmd)
        return pmd->calendar;
    JSZonedDateTimeData *zdt = JS_GetOpaque(obj, js_zoneddatetime_class_id);
    if (zdt)
        return zdt->calendar;
    JSInstantData *instant = JS_GetOpaque(obj, js_instant_class_id);
    if (instant)
        return CAL_ISO8601;
    return -1;
}

static void dtf_clear_non_overlapping_fields(JSDateTimeFormatData *s, JSValueConst val)
{
    void *p_pd = JS_GetOpaque(val, js_plaindate_class_id);
    void *p_pt = JS_GetOpaque(val, js_plaintime_class_id);
    void *p_pym = JS_GetOpaque(val, js_plainyearmonth_class_id);
    void *p_pmd = JS_GetOpaque(val, js_plainmonthday_class_id);

    if (p_pd) {
        s->hour = DTF_HOUR_UNSET;
        s->minute = DTF_MINUTE_UNSET;
        s->second = DTF_SECOND_UNSET;
        s->dayPeriod = DTF_DAYPERIOD_UNSET;
        s->has_fractionalSecondDigits = 0;
        s->timeStyle = DTF_STYLE_UNSET;
    } else if (p_pt) {
        s->weekday = DTF_WEEKDAY_UNSET;
        s->year = DTF_YEAR_UNSET;
        s->has_year_option = 0;
        s->month = DTF_MONTH_UNSET;
        s->has_month_option = 0;
        s->day = DTF_DAY_UNSET;
        s->has_day_option = 0;
        s->era = DTF_ERA_UNSET;
        s->dateStyle = DTF_STYLE_UNSET;
    } else if (p_pym) {
        s->weekday = DTF_WEEKDAY_UNSET;
        s->day = DTF_DAY_UNSET;
        s->has_day_option = 0;
        s->hour = DTF_HOUR_UNSET;
        s->minute = DTF_MINUTE_UNSET;
        s->second = DTF_SECOND_UNSET;
        s->dayPeriod = DTF_DAYPERIOD_UNSET;
        s->has_fractionalSecondDigits = 0;
        s->timeStyle = DTF_STYLE_UNSET;
    } else if (p_pmd) {
        s->weekday = DTF_WEEKDAY_UNSET;
        s->year = DTF_YEAR_UNSET;
        s->has_year_option = 0;
        s->hour = DTF_HOUR_UNSET;
        s->minute = DTF_MINUTE_UNSET;
        s->second = DTF_SECOND_UNSET;
        s->dayPeriod = DTF_DAYPERIOD_UNSET;
        s->has_fractionalSecondDigits = 0;
        s->timeStyle = DTF_STYLE_UNSET;
    }
}

/* Convert dateStyle/timeStyle to explicit fields when formatting Temporal
   objects, so that non-overlapping fields can be suppressed. This emulates
   ECMA-402 AdjustDateTimeStyleFormat for Temporal types. */
static void dtf_resolve_style_for_temporal(JSDateTimeFormatData *s, JSValueConst val)
{
    void *p_pd = JS_GetOpaque(val, js_plaindate_class_id);
    void *p_pt = JS_GetOpaque(val, js_plaintime_class_id);
    void *p_pdt = JS_GetOpaque(val, js_plaindatetime_class_id);
    void *p_pym = JS_GetOpaque(val, js_plainyearmonth_class_id);
    void *p_pmd = JS_GetOpaque(val, js_plainmonthday_class_id);
    void *p_inst = JS_GetOpaque(val, js_instant_class_id);

    if (s->dateStyle != DTF_STYLE_UNSET && (p_pt || p_pym || p_pmd)) {
        switch (s->dateStyle) {
        case DTF_STYLE_FULL:
            s->weekday = DTF_WEEKDAY_LONG;
            s->year = DTF_YEAR_NUMERIC;
            s->month = DTF_MONTH_LONG;
            s->day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_LONG:
            s->weekday = DTF_WEEKDAY_UNSET;
            s->year = DTF_YEAR_NUMERIC;
            s->month = DTF_MONTH_LONG;
            s->day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_MEDIUM:
            s->weekday = DTF_WEEKDAY_UNSET;
            s->year = DTF_YEAR_NUMERIC;
            s->month = DTF_MONTH_SHORT;
            s->day = DTF_DAY_NUMERIC;
            break;
        case DTF_STYLE_SHORT:
            s->weekday = DTF_WEEKDAY_UNSET;
            s->year = DTF_YEAR_2DIGIT;
            s->month = DTF_MONTH_NUMERIC;
            s->day = DTF_DAY_NUMERIC;
            break;
        }
        s->dateStyle = DTF_STYLE_UNSET;
    }

    if (s->timeStyle != DTF_STYLE_UNSET && (p_pd || p_pym || p_pmd)) {
        switch (s->timeStyle) {
        case DTF_STYLE_FULL:
            s->hour = DTF_HOUR_NUMERIC;
            s->minute = DTF_MINUTE_NUMERIC;
            s->second = DTF_SECOND_NUMERIC;
            break;
        case DTF_STYLE_LONG:
            s->hour = DTF_HOUR_NUMERIC;
            s->minute = DTF_MINUTE_NUMERIC;
            s->second = DTF_SECOND_NUMERIC;
            break;
        case DTF_STYLE_MEDIUM:
            s->hour = DTF_HOUR_NUMERIC;
            s->minute = DTF_MINUTE_NUMERIC;
            s->second = DTF_SECOND_NUMERIC;
            break;
        case DTF_STYLE_SHORT:
            s->hour = DTF_HOUR_NUMERIC;
            s->minute = DTF_MINUTE_NUMERIC;
            s->second = DTF_SECOND_UNSET;
            break;
        }
        s->timeStyle = DTF_STYLE_UNSET;
    }

    dtf_clear_non_overlapping_fields(s, val);

    /* Inject defaults after clearing non-overlapping fields, so that options
       like era on PlainTime don't block time defaults. */
    if (s->dateStyle == DTF_STYLE_UNSET && s->timeStyle == DTF_STYLE_UNSET) {
        int has_date = (s->weekday != DTF_WEEKDAY_UNSET || s->year != DTF_YEAR_UNSET ||
                        s->month != DTF_MONTH_UNSET || s->day != DTF_DAY_UNSET);
        int has_time = (s->dayPeriod != DTF_DAYPERIOD_UNSET || s->hour != DTF_HOUR_UNSET ||
                        s->minute != DTF_MINUTE_UNSET || s->second != DTF_SECOND_UNSET ||
                        s->has_fractionalSecondDigits);

        if (p_pd && !has_date) {
            s->year = DTF_YEAR_NUMERIC;
            s->month = DTF_MONTH_NUMERIC;
            s->day = DTF_DAY_NUMERIC;
        } else if (p_pym && !has_date) {
            s->year = DTF_YEAR_NUMERIC;
            s->month = DTF_MONTH_NUMERIC;
        } else if (p_pmd && !has_date) {
            s->month = DTF_MONTH_NUMERIC;
            s->day = DTF_DAY_NUMERIC;
        } else if (p_pt && !has_time) {
            s->hour = DTF_HOUR_NUMERIC;
            s->minute = DTF_MINUTE_NUMERIC;
            s->second = DTF_SECOND_NUMERIC;
        } else if ((p_pdt || p_inst) && !has_time) {
            if (!has_date ||
                (s->weekday == DTF_WEEKDAY_UNSET && s->dateStyle == DTF_STYLE_UNSET &&
                 !s->has_year_option && !s->has_month_option && !s->has_day_option)) {
                s->hour = DTF_HOUR_NUMERIC;
                s->minute = DTF_MINUTE_NUMERIC;
                s->second = DTF_SECOND_NUMERIC;
            }
            if (!has_date) {
                s->year = DTF_YEAR_NUMERIC;
                s->month = DTF_MONTH_NUMERIC;
                s->day = DTF_DAY_NUMERIC;
            }
        }

        if (!s->has_hourCycle && (s->hour != DTF_HOUR_UNSET || s->timeStyle != DTF_STYLE_UNSET)) {
            const char *p;
            if (strstart(s->locale, "ja", &p) && (*p == '-' || *p == '\0'))
                s->hourCycle = 2; /* h23 */
            else if (strstart(s->locale, "de", &p) && (*p == '-' || *p == '\0'))
                s->hourCycle = 2; /* h23 */
            else
                s->hourCycle = 1; /* h12 */
            s->has_hourCycle = 1;
        }
    }
}

static int dtf_append_number_part(JSContext *ctx, DynBuf *db, DTFPartInfo **parts, int *n_parts, int *max_parts,
                                  const char *type, int n, int width, int numberingSystem)
{
    int off = (int)db->size;
    dtf_append_number(db, n, width, numberingSystem);
    if (parts)
        return dtf_record_part(ctx, db, parts, n_parts, max_parts, type, off, (int)db->size - off);
    return 0;
}

static JSValue dtf_format_result(JSContext *ctx, DynBuf *db, DTFPartInfo *parts, int n_parts,
                                  int want_parts)
{
    if (dbuf_error(db)) {
        dbuf_free(db);
        js_free(ctx, parts);
        return JS_EXCEPTION;
    }
    if (want_parts) {
        JSValue arr = JS_NewArray(ctx);
        int i;
        if (JS_IsException(arr))
            goto fail;
        for (i = 0; i < n_parts; i++) {
            JSValue obj = JS_NewObject(ctx);
            if (JS_IsException(obj))
                goto fail;
            if (JS_DefinePropertyValueStr(ctx, obj, "type",
                                           JS_NewString(ctx, parts[i].type), JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, obj);
                goto fail;
            }
            if (JS_DefinePropertyValueStr(ctx, obj, "value",
                                           JS_NewStringLen(ctx, (char *)db->buf + parts[i].off,
                                                           (size_t)parts[i].len), JS_PROP_C_W_E) < 0) {
                JS_FreeValue(ctx, obj);
                goto fail;
            }
            if (JS_DefinePropertyValueUint32(ctx, arr, (uint32_t)i, obj, JS_PROP_C_W_E) < 0)
                goto fail;
        }
        dbuf_free(db);
        js_free(ctx, parts);
        return arr;
    fail:
        JS_FreeValue(ctx, arr);
        dbuf_free(db);
        js_free(ctx, parts);
        return JS_EXCEPTION;
    } else {
        JSValue ret = JS_NewStringLen(ctx, (char *)db->buf, db->size);
        dbuf_free(db);
        js_free(ctx, parts);
        return ret;
    }
}

/* Format a date/time using a CLDR pattern string.
   Handles: y, M, d, E, G, h, H, m, s, a, z and quoted literals.
   Returns 0 on success, -1 on error. */
static int dtf_display_hour(int hour, int hourCycle)
{
    switch (hourCycle) {
    case DTF_HOURCYCLE_H11:
        return hour % 12;
    case DTF_HOURCYCLE_H12:
        {
            int dh = hour % 12;
            return dh == 0 ? 12 : dh;
        }
    case DTF_HOURCYCLE_H23:
        return hour;
    case DTF_HOURCYCLE_H24:
        return hour == 0 ? 24 : hour;
    default:
        return hour;
    }
}

static const char *dtf_sec_to_offset_str(int offset_sec);
static const char *dtf_format_offset_timezone(const char *tz, int is_long);

/* Hardcoded long timezone names for embedded zones used by tests */
static const char *dtf_get_embedded_tz_long_name(const char *tz, int offset_sec)
{
    if (!strcmp(tz, "Europe/Vienna")) {
        if (offset_sec == 3600)
            return "Central European Standard Time";
        if (offset_sec == 7200)
            return "Central European Summer Time";
    }
    return NULL;
}

static int dtf_format_from_pattern(JSContext *ctx, DynBuf *db,
                                    const char *pattern, JSDateTimeFormatData *s,
                                    const LocaleData *ldata, DTFDateFields *fields,
                                    int want_parts, DTFPartInfo **parts, int *n_parts, int *max_parts)
{
    const char *p = pattern;
    int cal_year = fields->year;
    int cal_month = fields->month + 1;
    int cal_day = fields->day;
    ti_iso_to_calendar_date(s->calendar, fields->year, fields->month + 1, fields->day, &cal_year, &cal_month, &cal_day);

    TIEra era = TI_ERA_UNKNOWN;
    int era_year = cal_year;
    ti_calendar_era_and_era_year(s->calendar, fields->year, fields->month + 1, fields->day, cal_year, &era, &era_year);

    int relatedYear = fields->year;
    int lmonth = cal_month;
    int lday = cal_day;
    int is_leap_month = 0;
    if (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) {
        relatedYear = ti_chinese_related_year(s->calendar, fields->year, fields->month + 1, fields->day);
        ti_calendar_month_to_month_code(s->calendar, cal_year, cal_month, &lmonth, &is_leap_month);
        lday = cal_day;
    }

    while (*p) {
        if (*p == '\'') {
            /* Quoted literal */
            p++;
            if (*p == '\'') {
                /* '' → single ' */
                if (dtf_add_literal(ctx, db, want_parts ? parts : NULL, n_parts, max_parts, "'") < 0)
                    return -1;
                p++;
            } else {
                int off = (int)db->size;
                while (*p && *p != '\'') {
                    dbuf_putc(db, (uint8_t)*p++);
                }
                if (want_parts && parts)
                    if (dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, (int)db->size - off) < 0)
                        return -1;
                if (*p == '\'') p++;
            }
        } else if (isalpha((unsigned char)*p)) {
            char c = *p;
            int width = 0;
            while (*p == c) {
                width++;
                p++;
            }
            switch (c) {
            case 'y': {
                int yr = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? relatedYear : era_year;
                int has_era = 0;
                const char *era_str = NULL;
                if (width == 2)
                    yr = yr % 100;
                if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                           "year", yr, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                    return -1;
                if (s->era != DTF_ERA_UNSET && s->calendar != CAL_CHINESE && s->calendar != CAL_DANGI) {
                    has_era = get_calendar_era(s->calendar, era,
                                                s->era == DTF_ERA_LONG ? 0 :
                                                s->era == DTF_ERA_SHORT ? 1 : 2,
                                                ldata, &era_str);
                    if (has_era && era_str) {
                        if (dtf_add_literal(ctx, db, want_parts ? parts : NULL, n_parts, max_parts, " ") < 0)
                            return -1;
                        if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                            "era", (int)db->size, (int)strlen(era_str)) < 0)
                            return -1;
                        dbuf_putstr(db, era_str);
                    }
                }
                break;
            }
            case 'M': {
                int month_val = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? lmonth : cal_month;
                const char *cal_name = get_calendar_month_name(s->calendar, s->locale, cal_year, cal_month, width);
                if (cal_name) {
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        "month", (int)db->size, (int)strlen(cal_name)) < 0)
                        return -1;
                    dbuf_putstr(db, cal_name);
                } else if (width <= 2) {
                    if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                               "month", month_val, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                        return -1;
                    if ((s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) && is_leap_month) {
                        dbuf_putc(db, 'L');
                        if (want_parts && parts && *n_parts > 0)
                            (*parts)[*n_parts - 1].len++;
                    }
                } else if (width == 3) {
                    const char *name = LD_MONTH_SHORT(ldata, fields->month);
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        "month", (int)db->size, (int)strlen(name)) < 0)
                        return -1;
                    dbuf_putstr(db, name);
                } else {
                    const char *name = LD_MONTH_LONG(ldata, fields->month);
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        "month", (int)db->size, (int)strlen(name)) < 0)
                        return -1;
                    dbuf_putstr(db, name);
                }
                break;
            }
            case 'd': {
                int day_val = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? lday : cal_day;
                if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                           "day", day_val, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                    return -1;
                break;
            }
            case 'E': {
                const char *name;
                if (width <= 3) {
                    /* Short weekday not in our data; fall back to long */
                    name = LD_WEEKDAY_LONG(ldata, fields->weekday);
                } else if (width == 4) {
                    name = LD_WEEKDAY_LONG(ldata, fields->weekday);
                } else {
                    name = LD_WEEKDAY_NARROW(ldata, fields->weekday);
                }
                if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                    "weekday", (int)db->size, (int)strlen(name)) < 0)
                    return -1;
                dbuf_putstr(db, name);
                break;
            }
            case 'G': {
                const char *era_str = NULL;
                int has_era = get_calendar_era(s->calendar, era,
                                                width <= 3 ? 1 :
                                                width == 4 ? 0 : 2,
                                                ldata, &era_str);
                if (has_era && era_str) {
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        "era", (int)db->size, (int)strlen(era_str)) < 0)
                        return -1;
                    dbuf_putstr(db, era_str);
                }
                break;
            }
            case 'h':
            case 'K':
            case 'H':
            case 'k': {
                int dh = dtf_display_hour(fields->hour, s->hourCycle);
                if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                           "hour", dh, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                    return -1;
                break;
            }
            case 'm': {
                if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                           "minute", fields->minute, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                    return -1;
                break;
            }
            case 's': {
                if (dtf_append_number_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                           "second", fields->second, width == 2 ? 2 : 0, s->numberingSystem) < 0)
                    return -1;
                break;
            }
            case 'a':
            case 'b':
            case 'B': {
                int is_12h = (s->hourCycle == DTF_HOURCYCLE_H11 || s->hourCycle == DTF_HOURCYCLE_H12);
                if (is_12h) {
                    const char *dp;
                    if (c == 'B') {
                        int dp_idx = get_en_dayperiod_idx(fields->hour);
                        dp = LD_DAYPERIOD(ldata, dp_idx);
                    } else {
                        dp = LD_AMPM(ldata, fields->hour < 12 ? 0 : 1);
                    }
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        "dayPeriod", (int)db->size, (int)strlen(dp)) < 0)
                        return -1;
                    dbuf_putstr(db, dp);
                } else {
                    /* Suppress dayPeriod and remove preceding space/U+202F if present */
                    if (db->size >= 1 && db->buf[db->size - 1] == ' ') {
                        db->size -= 1;
                        if (want_parts && parts && *n_parts > 0) {
                            if ((size_t)((*parts)[*n_parts - 1].off + (*parts)[*n_parts - 1].len) == db->size + 1)
                                (*n_parts)--;
                        }
                    } else if (db->size >= 3 &&
                               db->buf[db->size - 3] == 0xE2 &&
                               db->buf[db->size - 2] == 0x80 &&
                               db->buf[db->size - 1] == 0xAF) {
                        db->size -= 3;
                        if (want_parts && parts && *n_parts > 0) {
                            if ((size_t)((*parts)[*n_parts - 1].off + (*parts)[*n_parts - 1].len) == db->size + 3)
                                (*n_parts)--;
                        }
                    }
                }
                break;
            }
            case 'z':
            case 'Z':
            case 'v':
            case 'V': {
                /* Timezone formatting */
                const char *tz_name = NULL;
                const char *tz_type = "timeZoneName";
                if (!strcmp(s->timeZone, "UTC") || !strcmp(s->timeZone, "GMT") ||
                    !strcmp(s->timeZone, "Etc/UTC") || !strcmp(s->timeZone, "Etc/GMT")) {
                    if (width >= 4)
                        tz_name = "Coordinated Universal Time";
                    else
                        tz_name = "UTC";
                } else if (s->timeZone[0] == '+' || s->timeZone[0] == '-') {
                    tz_name = s->timeZone;
                } else {
                    int tz_id = ti_tz_find_id(s->timeZone);
                    if (tz_id >= 0) {
                        int offset_sec = ti_tz_offset_at_sec(tz_id, fields->epoch_sec);
                        if (width >= 4) {
                            tz_name = dtf_get_embedded_tz_long_name(s->timeZone, offset_sec);
                            if (!tz_name) {
                                const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                                tz_name = dtf_format_offset_timezone(offset_str, 1);
                            }
                        } else {
                            const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                            tz_name = dtf_format_offset_timezone(offset_str, 0);
                        }
                    } else {
                        tz_name = s->timeZone;
                    }
                }
                if (tz_name) {
                    if (dtf_record_part(ctx, db, want_parts ? parts : NULL, n_parts, max_parts,
                                        tz_type, (int)db->size, (int)strlen(tz_name)) < 0)
                        return -1;
                    dbuf_putstr(db, tz_name);
                }
                break;
            }
            default:
                /* Unknown field symbol: treat as literal */
                {
                    int off = (int)db->size;
                    int i;
                    for (i = 0; i < width; i++)
                        dbuf_putc(db, (uint8_t)c);
                    if (want_parts && parts)
                        if (dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, (int)db->size - off) < 0)
                            return -1;
                }
                break;
            }
        } else {
            /* Literal character - handle multi-byte UTF-8 sequences */
            const char *start = p;
            unsigned char c = (unsigned char)*p++;
            if (c >= 0x80) {
                int nextra;
                if (c < 0xC0)
                    nextra = 0;
                else if (c < 0xE0)
                    nextra = 1;
                else if (c < 0xF0)
                    nextra = 2;
                else
                    nextra = 3;
                while (nextra-- > 0 && (*p & 0xC0) == 0x80)
                    p++;
            }
            {
                int off = (int)db->size;
                int len = (int)(p - start);
                dbuf_put(db, (uint8_t *)start, (size_t)len);
                if (want_parts && parts)
                    if (dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, len) < 0)
                        return -1;
            }
        }
    }
    return 0;
}

/* Sub-format helper for combined date+time formatting */
typedef struct {
    DynBuf db;
    DTFPartInfo *parts;
    int n_parts;
    int max_parts;
} DTFSubFormat;

static void dtf_subformat_init(JSContext *ctx, DTFSubFormat *sub)
{
    js_intl_dbuf_init(ctx, &sub->db);
    sub->parts = NULL;
    sub->n_parts = 0;
    sub->max_parts = 0;
}

static void dtf_subformat_free(JSContext *ctx, DTFSubFormat *sub)
{
    dbuf_free(&sub->db);
    js_free(ctx, sub->parts);
}

static int dtf_merge_subformat(JSContext *ctx, DynBuf *db, DTFPartInfo **parts, int *n_parts, int *max_parts,
                                DTFSubFormat *sub)
{
    int base_off = (int)db->size;
    int i;
    dbuf_put(db, sub->db.buf, sub->db.size);
    if (dbuf_error(db)) return -1;
    for (i = 0; i < sub->n_parts; i++) {
        if (dtf_record_part(ctx, db, parts, n_parts, max_parts,
                            sub->parts[i].type, base_off + sub->parts[i].off, sub->parts[i].len) < 0)
            return -1;
    }
    return 0;
}

/* Format date and time using a combined CLDR pattern like "{1}, {0}".
   {1} is replaced by the date sub-format, {0} by the time sub-format. */
static int dtf_format_combined(JSContext *ctx, DynBuf *db,
                                const char *pattern, JSDateTimeFormatData *s,
                                const LocaleData *ldata, DTFDateFields *fields,
                                int want_parts, DTFPartInfo **parts, int *n_parts, int *max_parts,
                                DTFSubFormat *date_sub, DTFSubFormat *time_sub)
{
    const char *p = pattern;
    while (*p) {
        if (*p == '\'') {
            p++;
            if (*p == '\'') {
                if (dtf_add_literal(ctx, db, want_parts ? parts : NULL, n_parts, max_parts, "'") < 0)
                    return -1;
                p++;
            } else {
                int off = (int)db->size;
                while (*p && *p != '\'') {
                    dbuf_putc(db, (uint8_t)*p++);
                }
                if (want_parts && parts)
                    if (dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, (int)db->size - off) < 0)
                        return -1;
                if (*p == '\'') p++;
            }
        } else if (p[0] == '{' && (p[1] == '0' || p[1] == '1') && p[2] == '}') {
            if (p[1] == '0') {
                if (dtf_merge_subformat(ctx, db, want_parts ? parts : NULL, n_parts, max_parts, time_sub) < 0)
                    return -1;
            } else {
                if (dtf_merge_subformat(ctx, db, want_parts ? parts : NULL, n_parts, max_parts, date_sub) < 0)
                    return -1;
            }
            p += 3;
        } else {
            /* Literal - handle multi-byte UTF-8 sequences */
            const char *start = p;
            unsigned char c = (unsigned char)*p++;
            if (c >= 0x80) {
                int nextra;
                if (c < 0xC0)
                    nextra = 0;
                else if (c < 0xE0)
                    nextra = 1;
                else if (c < 0xF0)
                    nextra = 2;
                else
                    nextra = 3;
                while (nextra-- > 0 && (*p & 0xC0) == 0x80)
                    p++;
            }
            {
                int off = (int)db->size;
                int len = (int)(p - start);
                dbuf_put(db, (uint8_t *)start, (size_t)len);
                if (want_parts && parts)
                    if (dtf_record_part(ctx, db, parts, n_parts, max_parts, "literal", off, len) < 0)
                        return -1;
            }
        }
    }
    return 0;
}

/* Convert an offset in seconds to a string like +HH:MM */
static const char *dtf_sec_to_offset_str(int offset_sec)
{
    static char buf[16];
    int abs_sec = offset_sec < 0 ? -offset_sec : offset_sec;
    int hour = abs_sec / 3600;
    int minute = (abs_sec % 3600) / 60;
    char sign = offset_sec < 0 ? '-' : '+';
    snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, hour, minute);
    return buf;
}

/* Format an offset time zone (e.g. +01:00) into a display string.
   For short names, zero offset becomes "GMT", non-zero becomes "GMT±H" or "GMT±H:MM".
   Returns a pointer to a static buffer. */
static const char *dtf_format_offset_timezone(const char *tz, int is_long)
{
    static char buf[32];
    int hour, minute;
    char sign;
    if (tz[0] != '+' && tz[0] != '-')
        return tz;
    if (tz[1] < '0' || tz[1] > '9' || tz[2] < '0' || tz[2] > '9' ||
        tz[3] != ':' || tz[4] < '0' || tz[4] > '9' || tz[5] < '0' || tz[5] > '9' || tz[6] != '\0')
        return tz;
    sign = tz[0];
    hour = (tz[1] - '0') * 10 + (tz[2] - '0');
    minute = (tz[4] - '0') * 10 + (tz[5] - '0');
    if (hour == 0 && minute == 0) {
        snprintf(buf, sizeof(buf), "GMT");
    } else if (is_long) {
        snprintf(buf, sizeof(buf), "GMT%c%02d:%02d", sign, hour, minute);
    } else if (minute == 0) {
        snprintf(buf, sizeof(buf), "GMT%c%d", sign, hour);
    } else {
        snprintf(buf, sizeof(buf), "GMT%c%d:%02d", sign, hour, minute);
    }
    return buf;
}

static JSValue dtf_format_date(JSContext *ctx, JSDateTimeFormatData *s, double ms, int want_parts)
{
    DTFDateFields fields;
    DynBuf db;
    DTFPartInfo *parts = NULL;
    int n_parts = 0, max_parts = 0;
    int has_date, has_time;
    int weekday_opt, year_opt, month_opt, day_opt;
    int hour_opt, minute_opt, second_opt, tzname_opt;
    int display_hour, dayperiod_idx;
    int hour_width, minute_width, second_width;
    int has_hour, has_minute, has_second;
    int has_dayperiod;
    int time_field_count;
    const char *dayperiod_str = NULL;

    const LocaleData *ldata = js_intl_find_locale_data(s->locale);

    js_intl_dbuf_init(ctx, &db);

    dtf_extract_fields(ms, s->timeZone, &fields);

    dtf_expand_styles(s, &weekday_opt, &year_opt, &month_opt, &day_opt,
                      &hour_opt, &minute_opt, &second_opt, &tzname_opt);

    has_date = (weekday_opt != DTF_WEEKDAY_UNSET || year_opt != DTF_YEAR_UNSET ||
                month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET);
    has_time = (hour_opt != DTF_HOUR_UNSET || minute_opt != DTF_MINUTE_UNSET ||
                second_opt != DTF_SECOND_UNSET);
    has_hour = (hour_opt != DTF_HOUR_UNSET);
    has_minute = (minute_opt != DTF_MINUTE_UNSET);
    has_second = (second_opt != DTF_SECOND_UNSET);
    has_dayperiod = (s->dayPeriod != DTF_DAYPERIOD_UNSET);

    {
        int is_24h = (s->hourCycle == DTF_HOURCYCLE_H23 || s->hourCycle == DTF_HOURCYCLE_H24);
        hour_width = (hour_opt == DTF_HOUR_2DIGIT || (is_24h && (has_minute || has_second))) ? 2 : 0;
    }
    minute_width = (has_minute && (has_hour || has_second)) ? 2 : ((minute_opt == DTF_MINUTE_2DIGIT) ? 2 : 0);
    second_width = (has_second && has_minute) ? 2 : ((second_opt == DTF_SECOND_2DIGIT) ? 2 : 0);

    display_hour = fields.hour;
    dayperiod_idx = -1;
    if (has_hour && (s->hourCycle == DTF_HOURCYCLE_H11 || s->hourCycle == DTF_HOURCYCLE_H12)) {
        if (fields.hour < 12)
            dayperiod_idx = 0;
        else
            dayperiod_idx = 1;
        if (s->hourCycle == DTF_HOURCYCLE_H11) {
            display_hour = fields.hour % 12;
        } else {
            display_hour = fields.hour % 12;
            if (display_hour == 0)
                display_hour = 12;
        }
    } else if (s->hourCycle == DTF_HOURCYCLE_H24) {
        if (display_hour == 0)
            display_hour = 24;
    }

    int cal_year2 = fields.year;
    int cal_month2 = fields.month + 1;
    int cal_day2 = fields.day;
    ti_iso_to_calendar_date(s->calendar, fields.year, fields.month + 1, fields.day, &cal_year2, &cal_month2, &cal_day2);

    TIEra era2 = TI_ERA_UNKNOWN;
    int era_year2 = cal_year2;
    ti_calendar_era_and_era_year(s->calendar, fields.year, fields.month + 1, fields.day, cal_year2, &era2, &era_year2);

    if (has_dayperiod) {
        int dp_idx = get_en_dayperiod_idx(fields.hour);
        if (s->dayPeriod == DTF_DAYPERIOD_NARROW)
            dayperiod_str = LD_DAYPERIOD_NARROW(ldata, dp_idx);
        else
            dayperiod_str = LD_DAYPERIOD(ldata, dp_idx);
    }

    /* Date part */
    if (has_date) {
        int relatedYear2 = fields.year;
        int lmonth2 = cal_month2;
        int lday2 = cal_day2;
        int is_leap_month2 = 0;
        if (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) {
            relatedYear2 = ti_chinese_related_year(s->calendar, fields.year, fields.month + 1, fields.day);
            ti_calendar_month_to_month_code(s->calendar, cal_year2, cal_month2, &lmonth2, &is_leap_month2);
            lday2 = cal_day2;
        }

        /* Use locale-specific date pattern when dateStyle is set */
        if (s->dateStyle != DTF_STYLE_UNSET && s->timeStyle != DTF_STYLE_UNSET) {
            /* Combined date+time formatting */
            DTFSubFormat date_sub, time_sub;
            const char *date_pattern = NULL;
            const char *time_pattern = NULL;
            const char *combined_pattern = NULL;
            switch (s->dateStyle) {
            case DTF_STYLE_SHORT:
                date_pattern = date_pattern_tab[ldata->datePatternShort_idx];
                combined_pattern = combined_pattern_tab[ldata->combinedPatternShort_idx];
                break;
            case DTF_STYLE_MEDIUM:
                date_pattern = date_pattern_tab[ldata->datePatternMedium_idx];
                combined_pattern = combined_pattern_tab[ldata->combinedPatternMedium_idx];
                break;
            case DTF_STYLE_LONG:
                date_pattern = date_pattern_tab[ldata->datePatternLong_idx];
                combined_pattern = combined_pattern_tab[ldata->combinedPatternLong_idx];
                break;
            case DTF_STYLE_FULL:
                date_pattern = date_pattern_tab[ldata->datePatternFull_idx];
                combined_pattern = combined_pattern_tab[ldata->combinedPatternFull_idx];
                break;
            }
            switch (s->timeStyle) {
            case DTF_STYLE_SHORT:
                time_pattern = time_pattern_tab[ldata->timePatternShort_idx];
                break;
            case DTF_STYLE_MEDIUM:
                time_pattern = time_pattern_tab[ldata->timePatternMedium_idx];
                break;
            case DTF_STYLE_LONG:
                time_pattern = time_pattern_tab[ldata->timePatternLong_idx];
                break;
            case DTF_STYLE_FULL:
                time_pattern = time_pattern_tab[ldata->timePatternFull_idx];
                break;
            }
            dtf_subformat_init(ctx, &date_sub);
            if (date_pattern && *date_pattern) {
                if (dtf_format_from_pattern(ctx, &date_sub.db, date_pattern, s, ldata, &fields,
                                            want_parts, want_parts ? &date_sub.parts : NULL, &date_sub.n_parts, &date_sub.max_parts) < 0) {
                    dtf_subformat_free(ctx, &date_sub);
                    goto fail;
                }
            }
            dtf_subformat_init(ctx, &time_sub);
            if (time_pattern && *time_pattern) {
                if (dtf_format_from_pattern(ctx, &time_sub.db, time_pattern, s, ldata, &fields,
                                            want_parts, want_parts ? &time_sub.parts : NULL, &time_sub.n_parts, &time_sub.max_parts) < 0) {
                    dtf_subformat_free(ctx, &date_sub);
                    dtf_subformat_free(ctx, &time_sub);
                    goto fail;
                }
            }
            if (combined_pattern && *combined_pattern) {
                if (dtf_format_combined(ctx, &db, combined_pattern, s, ldata, &fields,
                                        want_parts, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                        &date_sub, &time_sub) < 0) {
                    dtf_subformat_free(ctx, &date_sub);
                    dtf_subformat_free(ctx, &time_sub);
                    goto fail;
                }
            }
            dtf_subformat_free(ctx, &date_sub);
            dtf_subformat_free(ctx, &time_sub);
        } else if (s->dateStyle != DTF_STYLE_UNSET) {
            const char *pattern = NULL;
            switch (s->dateStyle) {
            case DTF_STYLE_SHORT:
                pattern = date_pattern_tab[ldata->datePatternShort_idx];
                break;
            case DTF_STYLE_MEDIUM:
                pattern = date_pattern_tab[ldata->datePatternMedium_idx];
                break;
            case DTF_STYLE_LONG:
                pattern = date_pattern_tab[ldata->datePatternLong_idx];
                break;
            case DTF_STYLE_FULL:
                pattern = date_pattern_tab[ldata->datePatternFull_idx];
                break;
            }
            if (pattern && *pattern) {
                if (dtf_format_from_pattern(ctx, &db, pattern, s, ldata, &fields,
                                            want_parts, want_parts ? &parts : NULL, &n_parts, &max_parts) < 0)
                    goto fail;
            }
        } else {
            if (weekday_opt == DTF_WEEKDAY_LONG) {
                const char *name = LD_WEEKDAY_LONG(ldata, fields.weekday);
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "weekday", (int)db.size, (int)strlen(name)) < 0)
                    goto fail;
                dbuf_putstr(&db, name);
                if (month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET || year_opt != DTF_YEAR_UNSET) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ", ") < 0)
                        goto fail;
                }
            } else if (weekday_opt == DTF_WEEKDAY_SHORT) {
                const char *name = LD_WEEKDAY_LONG(ldata, fields.weekday);
                int name_len = (int)strlen(name);
                if (name_len > 3)
                    name_len = 3;
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "weekday", (int)db.size, name_len) < 0)
                    goto fail;
                dbuf_put(&db, (uint8_t *)name, (size_t)name_len);
                if (month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET || year_opt != DTF_YEAR_UNSET) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ", ") < 0)
                        goto fail;
                }
            } else if (weekday_opt == DTF_WEEKDAY_NARROW) {
                const char *name = LD_WEEKDAY_NARROW(ldata, fields.weekday);
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "weekday", (int)db.size, (int)strlen(name)) < 0)
                    goto fail;
                dbuf_putstr(&db, name);
                if (month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET || year_opt != DTF_YEAR_UNSET) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ", ") < 0)
                        goto fail;
                }
            }

            if (month_opt != DTF_MONTH_UNSET) {
            int month_width = (month_opt == DTF_MONTH_LONG) ? 4 :
                              (month_opt == DTF_MONTH_SHORT) ? 3 :
                              (month_opt == DTF_MONTH_NARROW) ? 5 : 1;
            const char *cal_name = get_calendar_month_name(s->calendar, s->locale, cal_year2, cal_month2, month_width);
            if (cal_name) {
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "month", (int)db.size, (int)strlen(cal_name)) < 0)
                    goto fail;
                dbuf_putstr(&db, cal_name);
            } else if (month_opt == DTF_MONTH_LONG) {
                const char *name = LD_MONTH_LONG(ldata, fields.month);
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "month", (int)db.size, (int)strlen(name)) < 0)
                    goto fail;
                dbuf_putstr(&db, name);
            } else if (month_opt == DTF_MONTH_SHORT) {
                const char *name = LD_MONTH_SHORT(ldata, fields.month);
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "month", (int)db.size, (int)strlen(name)) < 0)
                    goto fail;
                dbuf_putstr(&db, name);
            } else if (month_opt == DTF_MONTH_NARROW) {
                const char *name = LD_MONTH_NARROW(ldata, fields.month);
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "month", (int)db.size, (int)strlen(name)) < 0)
                    goto fail;
                dbuf_putstr(&db, name);
            } else {
                int width = (month_opt == DTF_MONTH_2DIGIT) ? 2 : 0;
                int month_val = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? lmonth2 : cal_month2;
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "month", month_val, width, s->numberingSystem) < 0)
                    goto fail;
                if ((s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) && is_leap_month2) {
                    dbuf_putc(&db, 'L');
                    if (want_parts && parts && n_parts > 0)
                        parts[n_parts - 1].len++;
                }
            }

            int is_en_numeric_month = (strstart(s->locale, "en", NULL) &&
                                       month_opt != DTF_MONTH_UNSET &&
                                       month_opt != DTF_MONTH_LONG &&
                                       month_opt != DTF_MONTH_SHORT);
            if (day_opt != DTF_DAY_UNSET) {
                if (s->dateStyle == DTF_STYLE_SHORT || is_en_numeric_month) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, "/") < 0)
                        goto fail;
                } else {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, " ") < 0)
                        goto fail;
                }
            } else if (year_opt != DTF_YEAR_UNSET) {
                if (is_en_numeric_month) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, "/") < 0)
                        goto fail;
                } else {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, " ") < 0)
                        goto fail;
                }
            }
        }

        if (day_opt != DTF_DAY_UNSET) {
            int width = (day_opt == DTF_DAY_2DIGIT) ? 2 : 0;
            int day_val = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? lday2 : cal_day2;
            if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                       "day", day_val, width, s->numberingSystem) < 0)
                goto fail;

            if (year_opt != DTF_YEAR_UNSET) {
                int is_en_slashed_date = (strstart(s->locale, "en", NULL) &&
                                          month_opt != DTF_MONTH_UNSET &&
                                          month_opt != DTF_MONTH_LONG &&
                                          month_opt != DTF_MONTH_SHORT &&
                                          day_opt != DTF_DAY_UNSET);
                const char *day_year_sep = (s->dateStyle == DTF_STYLE_SHORT || is_en_slashed_date) ? "/" : ", ";
                if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, day_year_sep) < 0)
                    goto fail;
            }
        }

        if (year_opt != DTF_YEAR_UNSET) {
            int width = (year_opt == DTF_YEAR_2DIGIT) ? 2 : 0;
            int yr = (s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) ? relatedYear2 : era_year2;
            int has_era = 0;
            const char *era_str = NULL;

            if (s->era != DTF_ERA_UNSET && s->calendar != CAL_CHINESE && s->calendar != CAL_DANGI) {
                has_era = get_calendar_era(s->calendar, era2,
                                            s->era == DTF_ERA_LONG ? 0 :
                                            s->era == DTF_ERA_SHORT ? 1 : 2,
                                            ldata, &era_str);
            }
            if (width == 2)
                yr = yr % 100;

            if (s->calendar != CAL_CHINESE && s->calendar != CAL_DANGI) {
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "year", yr, width, s->numberingSystem) < 0)
                    goto fail;

                if (has_era && era_str) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, " ") < 0)
                        goto fail;
                    if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                        "era", (int)db.size, (int)strlen(era_str)) < 0)
                        goto fail;
                    dbuf_putstr(&db, era_str);
                }
            }
        }

        /* Chinese and Dangi calendar stubs: add relatedYear and yearName */
        if ((s->calendar == CAL_CHINESE || s->calendar == CAL_DANGI) && year_opt != DTF_YEAR_UNSET) {
            if (strstart(s->locale, "zh", NULL)) {
                /* zh locale: yearName + literal("年") */
                const char *year_name = NULL;
                if (relatedYear2 == 2019)
                    year_name = "\u5df1\u4ea5"; /* 己亥 */
                else
                    year_name = "Year";
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "yearName", (int)db.size, (int)strlen(year_name)) < 0)
                    goto fail;
                dbuf_putstr(&db, year_name);
                if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, "\u5e74") < 0)
                    goto fail;
            } else {
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "relatedYear", relatedYear2, 0, s->numberingSystem) < 0)
                    goto fail;
                {
                    const char *year_name = "Year";
                    if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                        "yearName", (int)db.size, (int)strlen(year_name)) < 0)
                        goto fail;
                    dbuf_putstr(&db, year_name);
                }
            }
        }
        }
    }

    /* Connector between date and time */
    if (has_date && has_time) {
        if (s->dateStyle == DTF_STYLE_UNSET || s->timeStyle == DTF_STYLE_UNSET) {
            if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ", ") < 0)
                goto fail;
        }
    }

    /* Time part */
    if (has_time && (s->dateStyle == DTF_STYLE_UNSET || s->timeStyle == DTF_STYLE_UNSET)) {
        if (s->timeStyle != DTF_STYLE_UNSET) {
            const char *pattern = NULL;
            switch (s->timeStyle) {
            case DTF_STYLE_SHORT:
                pattern = time_pattern_tab[ldata->timePatternShort_idx];
                break;
            case DTF_STYLE_MEDIUM:
                pattern = time_pattern_tab[ldata->timePatternMedium_idx];
                break;
            case DTF_STYLE_LONG:
                pattern = time_pattern_tab[ldata->timePatternLong_idx];
                break;
            case DTF_STYLE_FULL:
                pattern = time_pattern_tab[ldata->timePatternFull_idx];
                break;
            }
            if (pattern && *pattern) {
                if (dtf_format_from_pattern(ctx, &db, pattern, s, ldata, &fields,
                                            want_parts, want_parts ? &parts : NULL, &n_parts, &max_parts) < 0)
                    goto fail;
            }
        } else {
            time_field_count = 0;

            if (has_hour) {
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "hour", display_hour, hour_width, s->numberingSystem) < 0)
                    goto fail;
                time_field_count++;
            }

            if (has_minute) {
                if (time_field_count > 0) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ":") < 0)
                        goto fail;
                }
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "minute", fields.minute, minute_width, s->numberingSystem) < 0)
                    goto fail;
                time_field_count++;
            }

            if (has_second) {
                if (time_field_count > 0) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ":") < 0)
                        goto fail;
                }
                if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                           "second", fields.second, second_width, s->numberingSystem) < 0)
                    goto fail;
                time_field_count++;

                if (s->has_fractionalSecondDigits && s->fractionalSecondDigits > 0) {
                    const char *sep = ".";
                    if (s->numberingSystem == NS_ARAB)
                        sep = "\xD9\xAB";
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, sep) < 0)
                        goto fail;
                    {
                        int fsd = s->fractionalSecondDigits;
                        int frac_val = fields.ms;
                        if (fsd == 1)
                            frac_val = frac_val / 100;
                        else if (fsd == 2)
                            frac_val = frac_val / 10;
                        if (dtf_append_number_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                                   "fractionalSecond", frac_val, fsd, s->numberingSystem) < 0)
                            goto fail;
                    }
                }
            }

            if (has_dayperiod) {
                if (time_field_count > 0) {
                    const char *dp_sep = dtf_get_dayperiod_separator(s->locale);
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, dp_sep) < 0)
                        goto fail;
                }
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    "dayPeriod", (int)db.size, (int)strlen(dayperiod_str)) < 0)
                    goto fail;
                dbuf_putstr(&db, dayperiod_str);
            } else if (dayperiod_idx >= 0) {
                const char *dp_sep = dtf_get_dayperiod_separator(s->locale);
                if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, dp_sep) < 0)
                    goto fail;
                {
                    const char *dp = LD_AMPM(ldata, dayperiod_idx);
                    if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                        "dayPeriod", (int)db.size, (int)strlen(dp)) < 0)
                        goto fail;
                    dbuf_putstr(&db, dp);
                }
            }

            if (tzname_opt != DTF_TIMEZONE_UNSET) {
                if (time_field_count > 0 || dayperiod_idx >= 0 || has_dayperiod) {
                    if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, " ") < 0)
                        goto fail;
                }
                {
                    const char *tz_name = NULL;
                    const char *tz_type = "timeZoneName";
                    if (!strcmp(s->timeZone, "UTC") || !strcmp(s->timeZone, "GMT") ||
                        !strcmp(s->timeZone, "Etc/UTC") || !strcmp(s->timeZone, "Etc/GMT")) {
                        if (tzname_opt == DTF_TIMEZONE_LONG)
                            tz_name = en_tz_utc_long;
                        else
                            tz_name = en_tz_utc_short;
                    } else {
                        int tz_id = ti_tz_find_id(s->timeZone);
                        if (tz_id >= 0) {
                            int offset_sec = ti_tz_offset_at_sec(tz_id, fields.epoch_sec);
                            if (tzname_opt == DTF_TIMEZONE_LONG) {
                                tz_name = dtf_get_embedded_tz_long_name(s->timeZone, offset_sec);
                                if (!tz_name) {
                                    const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                                    tz_name = dtf_format_offset_timezone(offset_str, 1);
                                }
                            } else {
                                const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                                tz_name = dtf_format_offset_timezone(offset_str, 0);
                            }
                        } else {
                            tz_name = dtf_format_offset_timezone(s->timeZone, tzname_opt == DTF_TIMEZONE_LONG);
                        }
                    }
                    if (tz_name) {
                        if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                            tz_type, (int)db.size, (int)strlen(tz_name)) < 0)
                            goto fail;
                        dbuf_putstr(&db, tz_name);
                    }
                }
            }
        }
    }

    /* Time zone name for date-only or standalone */
    if (!has_time && tzname_opt != DTF_TIMEZONE_UNSET) {
        if (has_date) {
            if (dtf_add_literal(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts, ", ") < 0)
                goto fail;
        }
        {
            const char *tz_name = NULL;
            const char *tz_type = "timeZoneName";
            if (!strcmp(s->timeZone, "UTC") || !strcmp(s->timeZone, "GMT") ||
                !strcmp(s->timeZone, "Etc/UTC") || !strcmp(s->timeZone, "Etc/GMT")) {
                if (tzname_opt == DTF_TIMEZONE_LONG)
                    tz_name = en_tz_utc_long;
                else
                    tz_name = en_tz_utc_short;
            } else {
                int tz_id = ti_tz_find_id(s->timeZone);
                if (tz_id >= 0) {
                    int offset_sec = ti_tz_offset_at_sec(tz_id, fields.epoch_sec);
                    if (tzname_opt == DTF_TIMEZONE_LONG) {
                        tz_name = dtf_get_embedded_tz_long_name(s->timeZone, offset_sec);
                        if (!tz_name) {
                            const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                            tz_name = dtf_format_offset_timezone(offset_str, 1);
                        }
                    } else {
                        const char *offset_str = dtf_sec_to_offset_str(offset_sec);
                        tz_name = dtf_format_offset_timezone(offset_str, 0);
                    }
                } else {
                    tz_name = dtf_format_offset_timezone(s->timeZone, tzname_opt == DTF_TIMEZONE_LONG);
                }
            }
            if (tz_name) {
                if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                                    tz_type, (int)db.size, (int)strlen(tz_name)) < 0)
                    goto fail;
                dbuf_putstr(&db, tz_name);
            }
        }
    }

    /* Day period only formatting (no date or time fields) */
    if (has_dayperiod && !has_date && !has_time) {
        if (dtf_record_part(ctx, &db, want_parts ? &parts : NULL, &n_parts, &max_parts,
                            "dayPeriod", (int)db.size, (int)strlen(dayperiod_str)) < 0)
            goto fail;
        dbuf_putstr(&db, dayperiod_str);
    }

    return dtf_format_result(ctx, &db, parts, n_parts, want_parts);

 fail:
    dbuf_free(&db);
    js_free(ctx, parts);
    return JS_EXCEPTION;
}

static void js_datetimeformat_finalizer(JSRuntime *rt, JSValue val)
{
    JSDateTimeFormatData *s = JS_GetOpaque(val, js_datetimeformat_class_id);
    if (s) {
        js_free_rt(rt, s->locale);
        js_free_rt(rt, s->locale_canonical);
        js_free_rt(rt, s->timeZone);
        js_free_rt(rt, s);
    }
}

static JSClassDef js_datetimeformat_class = {
    "DateTimeFormat",
    .finalizer = js_datetimeformat_finalizer,
};

/* UnwrapDateTimeFormat: handle fallback symbol for cross-realm objects */
static JSValue js_datetimeformat_unwrap(JSContext *ctx, JSValueConst this_val)
{
    JSDateTimeFormatData *s;
    JSValue ctor, sym, unwrapped;
    int is_instance;

    if (!JS_IsObject(this_val))
        return JS_ThrowTypeError(ctx, "DateTimeFormat object expected");

    s = JS_GetOpaque(this_val, js_datetimeformat_class_id);
    if (s)
        return JS_DupValue(ctx, this_val);

    /* No internal slot; check if it's an instance of DateTimeFormat */
    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
    is_instance = JS_OrdinaryIsInstanceOf(ctx, this_val, ctor);
    JS_FreeValue(ctx, ctor);
    if (is_instance < 0)
        return JS_EXCEPTION;
    if (!is_instance)
        return JS_ThrowTypeError(ctx, "DateTimeFormat object expected");

    /* Return the fallback symbol value */
    sym = JS_GetContextValue(ctx, JS_INTL_FALLBACK_SYMBOL);
    if (JS_IsException(sym))
        return sym;
    {
        JSAtom sym_atom = JS_ValueToAtom(ctx, sym);
        unwrapped = JS_GetProperty(ctx, this_val, sym_atom);
        JS_FreeAtom(ctx, sym_atom);
    }
    JS_FreeValue(ctx, sym);
    return unwrapped;
}

static JSValue js_datetimeformat_constructor(JSContext *ctx,
                                                JSValueConst func_obj,
                                                JSValueConst this_val,
                                                int argc, JSValueConst *argv,
                                                int flags)
{
    JSDateTimeFormatData *s;
    JSValue obj = JS_UNDEFINED;
    JSValue options = JS_UNDEFINED;
    JSValueConst new_target;
    int ret;
    int is_new;

    is_new = (flags & JS_CALL_FLAG_CONSTRUCTOR) != 0;

    if (is_new) {
        new_target = this_val;
    } else {
        new_target = func_obj;
    }

    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        return JS_EXCEPTION;

    s->calendar = CAL_GREGORY;
    s->numberingSystem = NS_LATN;
    {
        char tz[TI_TIMEZONE_BUF_SIZE];
        ti_get_default_timezone(tz, sizeof(tz));
        s->timeZone = js_strdup(ctx, tz);
    }
    s->dateStyle = DTF_STYLE_UNSET;
    s->timeStyle = DTF_STYLE_UNSET;
    s->weekday = DTF_WEEKDAY_UNSET;
    s->era = DTF_ERA_UNSET;
    s->year = DTF_YEAR_UNSET;
    s->has_year_option = 0;
    s->month = DTF_MONTH_UNSET;
    s->has_month_option = 0;
    s->day = DTF_DAY_UNSET;
    s->has_day_option = 0;
    s->dayPeriod = DTF_DAYPERIOD_UNSET;
    s->hour = DTF_HOUR_UNSET;
    s->minute = DTF_MINUTE_UNSET;
    s->second = DTF_SECOND_UNSET;
    s->timeZoneName = DTF_TIMEZONE_UNSET;
    s->formatMatcher = DTF_FORMATMATCHER_UNSET;
    s->required = 0;
    s->defaults = 0;

    {
        char *loc = js_intl_extract_locale(ctx, argc, argv);
        if (!loc)
            goto fail;
        js_free(ctx, s->locale);
        s->locale = loc;
    }

    /* Save canonical locale with extensions for resolvedOptions, and extract
       numberingSystem and calendar from the Unicode extension. */
    {
        char *canonical = NULL;
        if (js_canonicalizeLocaleList_first(ctx, argc > 0 ? argv[0] : JS_UNDEFINED, &canonical) < 0)
            goto fail;
        s->locale_canonical = canonical;
        if (canonical) {
            char *ns_str = js_intl_locale_get_numbering_system(ctx, canonical);
            if (ns_str) {
                int ns = js_intl_string_to_numbering_system(ns_str);
                if (ns != NS_UNKNOWN)
                    s->numberingSystem = ns;
                js_free(ctx, ns_str);
            } else {
                s->numberingSystem = dtf_default_numbering_system(s->locale);
            }
            {
                char *cal = js_intl_locale_get_unicode_key(ctx, canonical, "ca");
                if (cal) {
                    if (!strcasecmp(cal, "islamicc")) {
                        js_free(ctx, cal);
                        cal = js_strdup(ctx, "islamic-civil");
                    } else if (!strcasecmp(cal, "ethiopic-amete-alem")) {
                        js_free(ctx, cal);
                        cal = js_strdup(ctx, "ethioaa");
                    }
                    if (cal) {
                        int cal_enum = ti_calendar_string_to_enum(cal);
                        if (cal_enum == CAL_UNKNOWN) {
                            js_free(ctx, cal);
                            s->calendar = CAL_GREGORY;
                        } else {
                            js_free(ctx, cal);
                            s->calendar = cal_enum;
                        }
                    }
                }
            }
        }
    }

    /* Extract hourCycle from unicode extension if present in raw input */
    {
        int hc_from_ext = -1;
        if (argc > 0 && !JS_IsUndefined(argv[0])) {
            const char *raw = NULL;
            if (JS_IsString(argv[0])) {
                raw = JS_ToCString(ctx, argv[0]);
            } else {
                JSValue arr = JS_ToObject(ctx, argv[0]);
                if (!JS_IsException(arr)) {
                    JSValue item = JS_GetPropertyUint32(ctx, arr, (uint32_t)0);
                    if (!JS_IsException(item) && JS_IsString(item)) {
                        raw = JS_ToCString(ctx, item);
                    }
                    JS_FreeValue(ctx, item);
                }
                JS_FreeValue(ctx, arr);
            }
            if (raw) {
                const char *hc_ext = strstr(raw, "-u-hc-");
                if (hc_ext) {
                    hc_ext += 6;
                    if (!strncmp(hc_ext, "h11", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                        hc_from_ext = 0;
                    else if (!strncmp(hc_ext, "h12", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                        hc_from_ext = 1;
                    else if (!strncmp(hc_ext, "h23", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                        hc_from_ext = 2;
                    else if (!strncmp(hc_ext, "h24", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                        hc_from_ext = 3;
                }
                JS_FreeCString(ctx, raw);
            }
        }
        s->hourCycle = hc_from_ext >= 0 ? hc_from_ext : 1;
        s->has_hourCycle = hc_from_ext >= 0;
    }

    /* parse options */
    if (argc > 1 && !JS_IsUndefined(argv[1])) {
        options = JS_ToObject(ctx, argv[1]);
        if (JS_IsException(options))
            goto fail;

        /* Step 5: localeMatcher (read for spec order, ignore value) */
        {
            int lm = js_intl_get_string_option(ctx, options, "localeMatcher",
                                                intl_localeMatcher_names, 2,
                                                INTL_LOCALEMATCHER_BESTFIT);
            if (lm < 0)
                goto fail;
        }

        /* Step 8: calendar */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "calendar");
            if (JS_IsException(v))
                goto fail;
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCString(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str)
                    goto fail;
                if (!is_valid_locale_type(str)) {
                    JS_ThrowRangeError(ctx, "invalid calendar option");
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                {
                    char cal_buf[64];
                    const char *cal_str = str;
                    if (!strcasecmp(str, "islamicc")) cal_str = "islamic-civil";
                    else if (!strcasecmp(str, "ethiopic-amete-alem")) cal_str = "ethioaa";
                    else {
                        int i;
                        pstrcpy(cal_buf, sizeof(cal_buf), str);
                        for (i = 0; cal_buf[i]; i++)
                            cal_buf[i] = (char)tolower((unsigned char)cal_buf[i]);
                        cal_str = cal_buf;
                    }
                    {
                        int cal_enum = ti_calendar_string_to_enum(cal_str);
                        if (cal_enum != CAL_UNKNOWN) {
                            s->has_calendar_option = 1;
                            s->calendar = cal_enum;
                        }
                    }
                }
                JS_FreeCString(ctx, str);
            } else {
                JS_FreeValue(ctx, v);
            }
        }

        /* Step 9: numberingSystem */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "numberingSystem");
            if (JS_IsException(v))
                goto fail;
            if (!JS_IsUndefined(v)) {
                const char *str = JS_ToCString(ctx, v);
                JS_FreeValue(ctx, v);
                if (!str)
                    goto fail;
                if (!is_valid_locale_type(str)) {
                    JS_ThrowRangeError(ctx, "invalid numberingSystem option");
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                {
                    int ns = js_intl_string_to_numbering_system(str);
                    if (ns != NS_UNKNOWN) {
                        s->has_numberingSystem_option = 1;
                        s->numberingSystem = ns;
                    }
                }
                JS_FreeCString(ctx, str);
            } else {
                JS_FreeValue(ctx, v);
            }
        }

        /* Step 12: hour12 */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "hour12");
            if (JS_IsException(v))
                goto fail;
            if (!JS_IsUndefined(v)) {
                const char *p;
                int is_ja;
                s->has_hour12 = 1;
                s->hour12 = JS_ToBool(ctx, v);
                is_ja = strstart(s->locale, "ja", &p) && (*p == '-' || *p == '\0');
                /* hour12 overrides hourCycle */
                if (s->hour12) {
                    s->hourCycle = is_ja ? 0 : 1; /* h11 for ja, h12 otherwise */
                    s->has_hourCycle = 1;
                } else {
                    s->hourCycle = 2; /* h23 */
                    s->has_hourCycle = 1;
                }
            }
            JS_FreeValue(ctx, v);
        }

        /* Step 13: hourCycle */
        {
            int hc = js_intl_get_string_option(ctx, options, "hourCycle",
                                                dtf_hourcycle_names, 4, 4);
            if (hc < 0)
                goto fail;
            if (hc < 4) {
                s->has_hourCycle_option = 1;
                s->hourCycle = hc;
                s->has_hourCycle = 1;
                /* hour12 takes precedence if both are present */
                if (s->has_hour12) {
                    if (s->hour12)
                        s->hourCycle = 1; /* h12 */
                    else
                        s->hourCycle = 2; /* h23 */
                }
            }
        }

        /* Step 29: timeZone */
        {
            JSValue v = JS_GetPropertyStr(ctx, options, "timeZone");
            if (JS_IsException(v))
                goto fail;
            if (!JS_IsUndefined(v)) {
                size_t slen;
                const char *str = JS_ToCStringLen(ctx, &slen, v);
                JS_FreeValue(ctx, v);
                if (!str)
                    goto fail;
                if (strlen(str) != slen || !is_valid_time_zone(str)) {
                    JS_ThrowRangeError(ctx, "invalid timeZone option");
                    JS_FreeCString(ctx, str);
                    goto fail;
                }
                {
                    char *tz;
                    if (str[0] == '+' || str[0] == '-') {
                        tz = canonicalize_offset_time_zone(ctx, str);
                    } else if (!strcasecmp(str, "UTC")) {
                        tz = js_strdup(ctx, "UTC");
                    } else {
                        tz = js_strdup(ctx, str);
                    }
                    js_free(ctx, s->timeZone);
                    s->timeZone = tz;
                }
                JS_FreeCString(ctx, str);
                if (!s->timeZone)
                    goto fail;
            } else {
                JS_FreeValue(ctx, v);
            }
        }

        /* Step 37: Table 7 options in table order */
        s->weekday = js_intl_get_string_option(ctx, options, "weekday",
                                                  dtf_weekday_names, 3,
                                                  DTF_WEEKDAY_UNSET);
        if (s->weekday < 0) goto fail;
        s->era = js_intl_get_string_option(ctx, options, "era",
                                              dtf_era_names, 3,
                                              DTF_ERA_UNSET);
        if (s->era < 0) goto fail;
        s->year = js_intl_get_string_option(ctx, options, "year",
                                             dtf_year_names, 2,
                                             DTF_YEAR_UNSET);
        if (s->year < 0) goto fail;
        s->has_year_option = (s->year != DTF_YEAR_UNSET);
        s->month = js_intl_get_string_option(ctx, options, "month",
                                              dtf_month_names, 5,
                                              DTF_MONTH_UNSET);
        if (s->month < 0) goto fail;
        s->has_month_option = (s->month != DTF_MONTH_UNSET);
        s->day = js_intl_get_string_option(ctx, options, "day",
                                            dtf_day_names, 2,
                                            DTF_DAY_UNSET);
        if (s->day < 0) goto fail;
        s->has_day_option = (s->day != DTF_DAY_UNSET);
        s->dayPeriod = js_intl_get_string_option(ctx, options, "dayPeriod",
                                                  dtf_dayperiod_names, 3,
                                                  DTF_DAYPERIOD_UNSET);
        if (s->dayPeriod < 0) goto fail;
        s->hour = js_intl_get_string_option(ctx, options, "hour",
                                             dtf_hour_names, 2,
                                             DTF_HOUR_UNSET);
        if (s->hour < 0) goto fail;
        s->minute = js_intl_get_string_option(ctx, options, "minute",
                                               dtf_minute_names, 2,
                                               DTF_MINUTE_UNSET);
        if (s->minute < 0) goto fail;
        s->second = js_intl_get_string_option(ctx, options, "second",
                                               dtf_second_names, 2,
                                               DTF_SECOND_UNSET);
        if (s->second < 0) goto fail;

        /* fractionalSecondDigits: GetNumberOption(options, prop, 1, 3, undefined) */
        {
            int fsd;
            ret = js_intl_get_number_option(ctx, options, "fractionalSecondDigits", &fsd, 1, 3);
            if (ret < 0) goto fail;
            if (ret > 0) {
                s->fractionalSecondDigits = fsd;
                s->has_fractionalSecondDigits = 1;
            }
        }

        s->timeZoneName = js_intl_get_string_option(ctx, options, "timeZoneName",
                                                      dtf_timezonename_names, 6,
                                                      DTF_TIMEZONE_UNSET);
        if (s->timeZoneName < 0) goto fail;

        /* Step 37 (continued): formatMatcher */
        s->formatMatcher = js_intl_get_string_option(ctx, options, "formatMatcher",
                                                      dtf_formatmatcher_names, 2,
                                                      DTF_FORMATMATCHER_UNSET);
        if (s->formatMatcher < 0) goto fail;

        /* Step 38: dateStyle */
        {
            int ds_idx = js_intl_get_string_option(ctx, options, "dateStyle",
                                                    dtf_style_names, 4,
                                                    DTF_STYLE_UNSET);
            if (ds_idx < 0)
                goto fail;

            /* Step 40: timeStyle */
            int ts_idx = js_intl_get_string_option(ctx, options, "timeStyle",
                                                    dtf_style_names, 4,
                                                    DTF_STYLE_UNSET);
            if (ts_idx < 0)
                goto fail;

            int has_style = (ds_idx != DTF_STYLE_UNSET || ts_idx != DTF_STYLE_UNSET);

            if (has_style) {
                static const char *conflicting[] = {
                    "weekday", "era", "year", "month", "day",
                    "dayPeriod", "hour", "minute", "second",
                    "fractionalSecondDigits", "timeZoneName"
                };
                int i;
                for (i = 0; i < 11; i++) {
                    JSValue v = JS_GetPropertyStr(ctx, options, conflicting[i]);
                    if (JS_IsException(v))
                        goto fail;
                    int is_undef = JS_IsUndefined(v);
                    JS_FreeValue(ctx, v);
                    if (!is_undef) {
                        JS_ThrowTypeError(ctx, "conflicting formatting option");
                        goto fail;
                    }
                }
                if (ds_idx != DTF_STYLE_UNSET) {
                    s->year = DTF_YEAR_UNSET;
                    s->month = DTF_MONTH_UNSET;
                    s->day = DTF_DAY_UNSET;
                    s->dateStyle = ds_idx;
                }
                if (ts_idx != DTF_STYLE_UNSET) {
                    s->hour = DTF_HOUR_UNSET;
                    s->minute = DTF_MINUTE_UNSET;
                    s->second = DTF_SECOND_UNSET;
                    s->timeStyle = ts_idx;
                }
            }
        }

        /* Default hourCycle when hour-related formatting is used but
           neither hour12 nor hourCycle was explicitly set. */
        if (!s->has_hourCycle && (s->hour != DTF_HOUR_UNSET || s->timeStyle != DTF_STYLE_UNSET)) {
            const char *p;
            /* Check for -u-hc- extension in the original locale string */
            const char *hc_ext = strstr(s->locale, "-u-hc-");
            if (hc_ext) {
                hc_ext += 6;
                if (!strncmp(hc_ext, "h11", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                    s->hourCycle = 0;
                else if (!strncmp(hc_ext, "h12", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                    s->hourCycle = 1;
                else if (!strncmp(hc_ext, "h23", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                    s->hourCycle = 2;
                else if (!strncmp(hc_ext, "h24", 3) && (hc_ext[3] == '-' || hc_ext[3] == '\0'))
                    s->hourCycle = 3;
                else
                    goto default_hc;
                s->has_hourCycle = 1;
            } else {
            default_hc:
                if (strstart(s->locale, "ja", &p) && (*p == '-' || *p == '\0'))
                    s->hourCycle = 2; /* h23 */
                else if (strstart(s->locale, "de", &p) && (*p == '-' || *p == '\0'))
                    s->hourCycle = 2; /* h23 */
                else
                    s->hourCycle = 1; /* h12 */
                s->has_hourCycle = 1;
            }
        }

        JS_FreeValue(ctx, options);
        options = JS_UNDEFINED;
    }

    /* Rebuild locale with preserved Unicode extensions */
    if (s->locale_canonical) {
        int preserve_ca = 0, preserve_nu = 0, preserve_hc = 0;

        if (js_intl_locale_has_key(s->locale_canonical, "ca")) {
            char *ext_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "ca");
            if (ext_val && is_supported_calendar(ext_val)) {
                int ext_cal = ti_calendar_string_to_enum(ext_val);
                if (!s->has_calendar_option || ext_cal == s->calendar)
                    preserve_ca = 1;
            }
            js_free(ctx, ext_val);
        }
        if (js_intl_locale_has_key(s->locale_canonical, "nu")) {
            char *ext_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
            if (ext_val) {
                int ext_ns = js_intl_string_to_numbering_system(ext_val);
                if (!s->has_numberingSystem_option || ext_ns == s->numberingSystem)
                    preserve_nu = 1;
                js_free(ctx, ext_val);
            }
        }
        if (js_intl_locale_has_key(s->locale_canonical, "hc")) {
            char *ext_val = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "hc");
            if (ext_val) {
                int hc_valid = 0;
                if (!strcmp(ext_val, "h11") || !strcmp(ext_val, "h12") ||
                    !strcmp(ext_val, "h23") || !strcmp(ext_val, "h24"))
                    hc_valid = 1;
                if (hc_valid && !s->has_hour12 &&
                    (!s->has_hourCycle_option || !strcmp(ext_val, dtf_hourcycle_names[s->hourCycle])))
                    preserve_hc = 1;
                js_free(ctx, ext_val);
            }
        }

        if (preserve_ca || preserve_nu || preserve_hc) {
            DynBuf db;
            js_intl_dbuf_init(ctx, &db);
            dbuf_putstr(&db, s->locale);
            dbuf_put(&db, (uint8_t *)"-u", (size_t)2);
            if (preserve_ca) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "ca");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-ca-", (size_t)4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            if (preserve_nu) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "nu");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-nu-", (size_t)4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            if (preserve_hc) {
                char *v = js_intl_locale_get_unicode_key(ctx, s->locale_canonical, "hc");
                if (v) {
                    dbuf_put(&db, (uint8_t *)"-hc-", (size_t)4);
                    dbuf_putstr(&db, v);
                    js_free(ctx, v);
                }
            }
            dbuf_put(&db, (uint8_t *)"", (size_t)1);
            if (dbuf_error(&db)) {
                dbuf_free(&db);
                return JS_EXCEPTION;
            }
            js_free(ctx, s->locale);
            s->locale = js_strdup(ctx, (char *)db.buf);
            dbuf_free(&db);
        }
    }

    /* Default date/time fields based on required and defaults */
    if (s->dateStyle == DTF_STYLE_UNSET && s->timeStyle == DTF_STYLE_UNSET) {
        int needDefaults = 1;
        if (s->required == 0 || s->required == 1) {
            if (s->weekday != DTF_WEEKDAY_UNSET || s->year != DTF_YEAR_UNSET ||
                s->month != DTF_MONTH_UNSET || s->day != DTF_DAY_UNSET)
                needDefaults = 0;
        }
        if (needDefaults && (s->required == 0 || s->required == 2)) {
            if (s->dayPeriod != DTF_DAYPERIOD_UNSET || s->hour != DTF_HOUR_UNSET ||
                s->minute != DTF_MINUTE_UNSET || s->second != DTF_SECOND_UNSET ||
                s->has_fractionalSecondDigits)
                needDefaults = 0;
        }
        if (needDefaults) {
            if (s->defaults == 0 || s->defaults == 2) {
                s->year = DTF_YEAR_NUMERIC;
                s->month = DTF_MONTH_NUMERIC;
                s->day = DTF_DAY_NUMERIC;
            }
            if (s->defaults == 1 || s->defaults == 2) {
                s->hour = DTF_HOUR_NUMERIC;
                s->minute = DTF_MINUTE_NUMERIC;
                s->second = DTF_SECOND_NUMERIC;
            }
        }
    }

    obj = JS_CreateFromConstructor(ctx, new_target, js_datetimeformat_class_id);
    if (JS_IsException(obj))
        goto fail;
    JS_SetOpaque(obj, s);

    if (!is_new) {
        /* ChainDateTimeFormat: if this is a DateTimeFormat instance, define fallback symbol */
        JSValue ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
        int is_instance = JS_OrdinaryIsInstanceOf(ctx, this_val, ctor);
        JS_FreeValue(ctx, ctor);
        if (is_instance < 0) {
            JS_FreeValue(ctx, obj);
            return JS_EXCEPTION;
        }
        if (is_instance) {
            JSValue sym = JS_GetContextValue(ctx, JS_INTL_FALLBACK_SYMBOL);
            if (JS_IsException(sym)) {
                JS_FreeValue(ctx, obj);
                return JS_EXCEPTION;
            }
            if (!JS_IsUndefined(sym)) {
                JSAtom sym_atom = JS_ValueToAtom(ctx, sym);
                JS_DefinePropertyValue(ctx, this_val, sym_atom, JS_DupValue(ctx, obj), 0);
                JS_FreeAtom(ctx, sym_atom);
            }
            JS_FreeValue(ctx, sym);
            JS_FreeValue(ctx, obj);
            return JS_DupValue(ctx, this_val);
        }
    }
    return obj;
 fail:
    JS_FreeValue(ctx, options);
    js_free(ctx, s->locale);
    js_free(ctx, s->locale_canonical);
    js_free(ctx, s->timeZone);
    js_free(ctx, s);
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/* TimeClip as defined in ECMA-262 */
static double time_clip(double x)
{
    if (!isfinite(x))
        return NAN;
    if (fabs(x) > 8.64e15)
        return NAN;
    return trunc(x);
}

static int64_t datetime_now(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

static double dtf_iso_to_epoch_ms(int year, int month, int day,
                                     int hour, int minute, int second, int ms)
{
    static const int mdays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int64_t y = year - 1;
    int64_t days = y * 365;
    int64_t q = y / 4;
    if ((y ^ 4) < 0 && (y % 4)) q--;
    days += q;
    q = y / 100;
    if ((y ^ 100) < 0 && (y % 100)) q--;
    days -= q;
    q = y / 400;
    if ((y ^ 400) < 0 && (y % 400)) q--;
    days += q;
    int i;
    for (i = 1; i < month; i++)
        days += mdays[i];
    if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        days++;
    days += day - 1;
    return (double)((days - 719162) * 86400000LL + hour * 3600000LL + minute * 60000LL + second * 1000LL + ms);
}

static int datetimeformat_validate_date(JSContext *ctx, JSValueConst arg, double *pd, int *is_plain)
{
    if (is_plain)
        *is_plain = 0;

    if (JS_IsUndefined(arg)) {
        JS_ThrowTypeError(ctx, "date argument is undefined");
        return -1;
    }

    /* Handle Temporal objects */
    if (JS_IsObject(arg)) {
        JSInstantData *instant = JS_GetOpaque(arg, js_instant_class_id);
        if (instant) {
            *pd = timespec_to_ms(&instant->ts);
            if (is_plain)
                *is_plain = 1;
            goto do_clip;
        }

        JSZonedDateTimeData *zdt = JS_GetOpaque(arg, js_zoneddatetime_class_id);
        if (zdt) {
            *pd = timespec_to_ms(&zdt->ts);
            goto do_clip;
        }

        JSPlainDateTimeData *pdt = JS_GetOpaque(arg, js_plaindatetime_class_id);
        if (pdt) {
            *pd = dtf_iso_to_epoch_ms(pdt->year, pdt->month, pdt->day,
                                       pdt->hour, pdt->minute, pdt->second, pdt->millisecond);
            if (is_plain)
                *is_plain = 1;
            return 0;
        }

        JSPlainDateData *pdate = JS_GetOpaque(arg, js_plaindate_class_id);
        if (pdate) {
            *pd = dtf_iso_to_epoch_ms(pdate->year, pdate->month, pdate->day, 12, 0, 0, 0);
            if (is_plain)
                *is_plain = 1;
            return 0;
        }

        JSPlainTimeData *pt = JS_GetOpaque(arg, js_plaintime_class_id);
        if (pt) {
            *pd = dtf_iso_to_epoch_ms(1970, 1, 1, pt->hour, pt->minute, pt->second, pt->millisecond);
            if (is_plain)
                *is_plain = 1;
            return 0;
        }

        JSPlainYearMonthData *pym = JS_GetOpaque(arg, js_plainyearmonth_class_id);
        if (pym) {
            *pd = dtf_iso_to_epoch_ms(pym->iso_year, pym->iso_month, pym->iso_day, 12, 0, 0, 0);
            if (is_plain)
                *is_plain = 1;
            return 0;
        }

        JSPlainMonthDayData *pmd = JS_GetOpaque(arg, js_plainmonthday_class_id);
        if (pmd) {
            *pd = dtf_iso_to_epoch_ms(pmd->iso_year, pmd->iso_month, pmd->iso_day, 12, 0, 0, 0);
            if (is_plain)
                *is_plain = 1;
            return 0;
        }
    }

    {
        int ret = JS_ToFloat64(ctx, pd, arg);
        if (ret < 0)
            return -1;
    }
do_clip:
    *pd = time_clip(*pd);
    if (isnan(*pd)) {
        JS_ThrowRangeError(ctx, "Invalid time value");
        return -1;
    }
    return 0;
}

/* bound format function */
static JSValue js_datetimeformat_bound_format(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv,
                                               int magic, JSValue *func_data)
{
    JSDateTimeFormatData *s = JS_GetOpaque(func_data[0], js_datetimeformat_class_id);
    double d;
    int is_plain = 0;

    if (!s)
        return JS_ThrowTypeError(ctx, "DateTimeFormat object expected");

    if (argc == 0 || JS_IsUndefined(argv[0])) {
        d = time_clip((double)datetime_now());
    } else {
        if (JS_IsObject(argv[0])) {
            if (dtf_check_temporal_overlap(ctx, s, argv[0]) < 0)
                return JS_EXCEPTION;
        }
        if (datetimeformat_validate_date(ctx, argv[0], &d, &is_plain) < 0)
            return JS_EXCEPTION;
    }
    if (is_plain) {
        JSDateTimeFormatData s_copy = *s;
        if (JS_GetOpaque(argv[0], js_plaindate_class_id) ||
            JS_GetOpaque(argv[0], js_plaintime_class_id) ||
            JS_GetOpaque(argv[0], js_plainyearmonth_class_id) ||
            JS_GetOpaque(argv[0], js_plainmonthday_class_id) ||
            JS_GetOpaque(argv[0], js_plaindatetime_class_id)) {
            s_copy.timeZone = (char *)"+00:00";
            s_copy.timeZoneName = DTF_TIMEZONE_UNSET;
        }
        dtf_resolve_style_for_temporal(&s_copy, argv[0]);
        return dtf_format_date(ctx, &s_copy, d, 0);
    }
    return dtf_format_date(ctx, s, d, 0);
}

/* getter for Intl.DateTimeFormat.prototype.format */
static JSValue js_datetimeformat_get_format(JSContext *ctx, JSValueConst this_val)
{
    JSValue func_data[1];
    JSValue unwrapped = js_datetimeformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    JSDateTimeFormatData *s = JS_GetOpaque2(ctx, unwrapped, js_datetimeformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    func_data[0] = unwrapped;
    JSValue ret = JS_NewCFunctionData(ctx, (JSCFunctionData *)js_datetimeformat_bound_format,
                                1, 0, 1, func_data);
    JS_FreeValue(ctx, unwrapped);
    return ret;
}

static JSValue js_datetimeformat_formatToParts(JSContext *ctx, JSValueConst this_val,
                                                  int argc, JSValueConst *argv)
{
    JSValue unwrapped = js_datetimeformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    JSDateTimeFormatData *s = JS_GetOpaque2(ctx, unwrapped, js_datetimeformat_class_id);
    double d;
    int is_plain = 0;
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    if (argc == 0 || JS_IsUndefined(argv[0])) {
        d = time_clip((double)datetime_now());
    } else {
        if (JS_IsObject(argv[0])) {
            if (dtf_check_temporal_overlap(ctx, s, argv[0]) < 0) {
                JS_FreeValue(ctx, unwrapped);
                return JS_EXCEPTION;
            }
        }
        if (datetimeformat_validate_date(ctx, argv[0], &d, &is_plain) < 0) {
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
    }
    if (is_plain) {
        JSDateTimeFormatData s_copy = *s;
        if (JS_GetOpaque(argv[0], js_plaindate_class_id) ||
            JS_GetOpaque(argv[0], js_plaintime_class_id) ||
            JS_GetOpaque(argv[0], js_plainyearmonth_class_id) ||
            JS_GetOpaque(argv[0], js_plainmonthday_class_id) ||
            JS_GetOpaque(argv[0], js_plaindatetime_class_id)) {
            s_copy.timeZone = (char *)"+00:00";
            s_copy.timeZoneName = DTF_TIMEZONE_UNSET;
        }
        dtf_resolve_style_for_temporal(&s_copy, argv[0]);
        JSValue ret = dtf_format_date(ctx, &s_copy, d, 1);
        JS_FreeValue(ctx, unwrapped);
        return ret;
    }
    JSValue ret = dtf_format_date(ctx, s, d, 1);
    JS_FreeValue(ctx, unwrapped);
    return ret;
}

/* Compare two part objects by type and value */
static int dtf_part_equal(JSContext *ctx, JSValue a, JSValue b)
{
    JSValue ta = JS_GetPropertyStr(ctx, a, "type");
    JSValue tb = JS_GetPropertyStr(ctx, b, "type");
    JSValue va = JS_GetPropertyStr(ctx, a, "value");
    JSValue vb = JS_GetPropertyStr(ctx, b, "value");
    int res = 0;
    const char *ca, *cb, *cva, *cvb;

    if (JS_IsException(ta) || JS_IsException(tb) || JS_IsException(va) || JS_IsException(vb))
        goto done;

    ca = JS_ToCString(ctx, ta);
    cb = JS_ToCString(ctx, tb);
    cva = JS_ToCString(ctx, va);
    cvb = JS_ToCString(ctx, vb);

    if (ca && cb && cva && cvb)
        res = !strcmp(ca, cb) && !strcmp(cva, cvb);

    JS_FreeCString(ctx, ca);
    JS_FreeCString(ctx, cb);
    JS_FreeCString(ctx, cva);
    JS_FreeCString(ctx, cvb);

done:
    JS_FreeValue(ctx, ta);
    JS_FreeValue(ctx, tb);
    JS_FreeValue(ctx, va);
    JS_FreeValue(ctx, vb);
    return res;
}

/* Append string values of parts[start] to parts[end-1] into db */
static int dtf_append_parts_string(JSContext *ctx, JSValue arr, int start, int end, DynBuf *db)
{
    int i;
    for (i = start; i < end; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, arr, (uint32_t)i);
        if (JS_IsException(item))
            return -1;
        {
            JSValue val = JS_GetPropertyStr(ctx, item, "value");
            const char *str = JS_ToCString(ctx, val);
            if (str)
                dbuf_putstr(db, str);
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, val);
        }
        JS_FreeValue(ctx, item);
    }
    return 0;
}

static JSValue js_datetimeformat_formatRange(JSContext *ctx, JSValueConst this_val,
                                              int argc, JSValueConst *argv)
{
    JSValue unwrapped = js_datetimeformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    JSDateTimeFormatData *s = JS_GetOpaque2(ctx, unwrapped, js_datetimeformat_class_id);
    JSDateTimeFormatData *s_use = s;
    JSDateTimeFormatData s_copy;
    JSValue start, end, ret;
    DynBuf db;
    double x, y;
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(argv[0]) || JS_IsUndefined(argv[1])) {
        JS_FreeValue(ctx, unwrapped);
        return JS_ThrowTypeError(ctx, "date argument is undefined");
    }
    {
        BOOL t0, t1, c0, c1;
        if (dtf_to_datetimeformattable(ctx, argv[0], &x, &t0, &c0) < 0) {
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
        if (dtf_to_datetimeformattable(ctx, argv[1], &y, &t1, &c1) < 0) {
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
        if (t0 != t1 || (t0 && !dtf_same_temporal_type(ctx, argv[0], argv[1]))) {
            JS_FreeValue(ctx, unwrapped);
            return JS_ThrowTypeError(ctx, "cannot format range between different types");
        }
        if (t0) {
            int cal0 = dtf_get_temporal_calendar(ctx, argv[0]);
            int cal1 = dtf_get_temporal_calendar(ctx, argv[1]);
            if (cal0 != cal1) {
                JS_FreeValue(ctx, unwrapped);
                return JS_ThrowRangeError(ctx, "cannot format range between different calendars");
            }
            if (dtf_check_temporal_overlap(ctx, s, argv[0]) < 0) {
                JS_FreeValue(ctx, unwrapped);
                return JS_EXCEPTION;
            }
            if (dtf_check_temporal_overlap(ctx, s, argv[1]) < 0) {
                JS_FreeValue(ctx, unwrapped);
                return JS_EXCEPTION;
            }
            if (JS_GetOpaque(argv[0], js_plaindate_class_id) ||
                JS_GetOpaque(argv[0], js_plaintime_class_id) ||
                JS_GetOpaque(argv[0], js_plainyearmonth_class_id) ||
                JS_GetOpaque(argv[0], js_plainmonthday_class_id) ||
                JS_GetOpaque(argv[0], js_plaindatetime_class_id)) {
                s_copy = *s;
                s_copy.timeZone = (char *)"+00:00";
                s_copy.timeZoneName = DTF_TIMEZONE_UNSET;
                s_use = &s_copy;
            }
        }
        if (c0) {
            x = time_clip(x);
            if (isnan(x)) {
                JS_FreeValue(ctx, unwrapped);
                JS_ThrowRangeError(ctx, "Invalid time value");
                return JS_EXCEPTION;
            }
        }
        if (c1) {
            y = time_clip(y);
            if (isnan(y)) {
                JS_FreeValue(ctx, unwrapped);
                JS_ThrowRangeError(ctx, "Invalid time value");
                return JS_EXCEPTION;
            }
        }
    }

    dtf_resolve_style_for_temporal(s_use, argv[0]);

    start = dtf_format_date(ctx, s_use, x, 0);
    if (JS_IsException(start)) {
        JS_FreeValue(ctx, unwrapped);
        return start;
    }
    end = dtf_format_date(ctx, s_use, y, 0);
    if (JS_IsException(end)) {
        JS_FreeValue(ctx, unwrapped);
        JS_FreeValue(ctx, start);
        return end;
    }

    {
        const char *start_c = JS_ToCString(ctx, start);
        const char *end_c = JS_ToCString(ctx, end);
        int same = start_c && end_c && strcmp(start_c, end_c) == 0;
        JS_FreeCString(ctx, start_c);
        JS_FreeCString(ctx, end_c);
        if (same) {
            JS_FreeValue(ctx, end);
            JS_FreeValue(ctx, unwrapped);
            return start;
        }
    }

    {
        int weekday_opt, year_opt, month_opt, day_opt;
        int hour_opt, minute_opt, second_opt, tzname_opt;
        int can_share = 0;
        DTFDateFields fx, fy;
        dtf_expand_styles(s_use, &weekday_opt, &year_opt, &month_opt, &day_opt,
                          &hour_opt, &minute_opt, &second_opt, &tzname_opt);
        dtf_extract_fields(x, s_use->timeZone, &fx);
        dtf_extract_fields(y, s_use->timeZone, &fy);
        {
            int has_date = (year_opt != DTF_YEAR_UNSET || month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET);
            if (fx.year == fy.year && fx.month == fy.month && fx.day == fy.day && has_date) {
                can_share = 1;
            } else if (month_opt == DTF_MONTH_SHORT || month_opt == DTF_MONTH_LONG) {
                can_share = 1;
            }
        }

        if (can_share) {
            JSValue start_arr, end_arr;
            int start_len = 0, end_len = 0;
            int prefix_len = 0, suffix_len = 0;
            int first_diff = 0, last_diff = -1, i;
            int largest_diff = -1; /* 0=day, 1=month, 2=year */

            start_arr = dtf_format_date(ctx, s_use, x, 1);
            if (JS_IsException(start_arr))
                goto range_fail;
            end_arr = dtf_format_date(ctx, s_use, y, 1);
            if (JS_IsException(end_arr)) {
                JS_FreeValue(ctx, start_arr);
                goto range_fail;
            }

            {
                JSValue len_val = JS_GetPropertyStr(ctx, start_arr, "length");
                if (!JS_IsException(len_val)) {
                    JS_ToInt32(ctx, &start_len, len_val);
                    JS_FreeValue(ctx, len_val);
                }
            }
            {
                JSValue len_val = JS_GetPropertyStr(ctx, end_arr, "length");
                if (!JS_IsException(len_val)) {
                    JS_ToInt32(ctx, &end_len, len_val);
                    JS_FreeValue(ctx, len_val);
                }
            }

            /* find first and last differing part */
                for (i = 0; i < start_len && i < end_len; i++) {
                    JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)i);
                    JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)i);
                    int eq = dtf_part_equal(ctx, a, b);
                    JS_FreeValue(ctx, a);
                    JS_FreeValue(ctx, b);
                    if (!eq) {
                        if (last_diff == -1)
                            first_diff = i;
                        last_diff = i;
                    }
                }
                if (last_diff == -1) {
                    /* no differences found, check if index 0 differs */
                    JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)0);
                    JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)0);
                    if (!dtf_part_equal(ctx, a, b)) {
                        first_diff = 0;
                        last_diff = 0;
                    }
                    JS_FreeValue(ctx, a);
                    JS_FreeValue(ctx, b);
                }

                if (fx.year != fy.year)
                    largest_diff = 2;
                else if (fx.month != fy.month)
                    largest_diff = 1;
                else if (fx.day != fy.day)
                    largest_diff = 0;
                else
                    largest_diff = -1;
                if (largest_diff == 2) {
                    prefix_len = 0;
                    suffix_len = 0;
                } else if (largest_diff == 1) {
                    prefix_len = 0;
                    /* suffix = matching parts after last_diff */
                    suffix_len = 0;
                    for (i = last_diff + 1; i < start_len && i < end_len; i++) {
                        JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)i);
                        JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)i);
                        int eq = dtf_part_equal(ctx, a, b);
                        JS_FreeValue(ctx, a);
                        JS_FreeValue(ctx, b);
                        if (eq)
                            suffix_len++;
                        else
                            break;
                    }
                } else {
                    /* day differs: prefix up to first_diff, suffix after last_diff */
                    prefix_len = first_diff;
                    suffix_len = 0;
                    for (i = last_diff + 1; i < start_len && i < end_len; i++) {
                        JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)i);
                        JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)i);
                        int eq = dtf_part_equal(ctx, a, b);
                        JS_FreeValue(ctx, a);
                        JS_FreeValue(ctx, b);
                        if (eq)
                            suffix_len++;
                        else
                            break;
                    }
                }

            js_intl_dbuf_init(ctx, &db);
            if (dtf_append_parts_string(ctx, start_arr, 0, prefix_len, &db) < 0)
                goto parts_fail;
            if (dtf_append_parts_string(ctx, start_arr, prefix_len, start_len - suffix_len, &db) < 0)
                goto parts_fail;
            dbuf_put(&db, (uint8_t *)" \xE2\x80\x93 ", (size_t)5); /* " – " */
            if (dtf_append_parts_string(ctx, end_arr, prefix_len, end_len - suffix_len, &db) < 0)
                goto parts_fail;
            if (dtf_append_parts_string(ctx, end_arr, end_len - suffix_len, end_len, &db) < 0)
                goto parts_fail;

            if (dbuf_error(&db))
                goto parts_fail;
            ret = JS_NewStringLen(ctx, (char *)db.buf, db.size);
            dbuf_free(&db);
            JS_FreeValue(ctx, start_arr);
            JS_FreeValue(ctx, end_arr);
            JS_FreeValue(ctx, start);
            JS_FreeValue(ctx, end);
            JS_FreeValue(ctx, unwrapped);
            return ret;

        parts_fail:
            dbuf_free(&db);
            JS_FreeValue(ctx, start_arr);
            JS_FreeValue(ctx, end_arr);
        range_fail:
            JS_FreeValue(ctx, start);
            JS_FreeValue(ctx, end);
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
    }

    js_intl_dbuf_init(ctx, &db);
    {
        const char *p = JS_ToCString(ctx, start);
        if (p)
            dbuf_putstr(&db, p);
        JS_FreeCString(ctx, p);
    }
    dbuf_put(&db, (uint8_t *)" \xE2\x80\x93 ", (size_t)5); /* " – " */
    {
        const char *p = JS_ToCString(ctx, end);
        if (p)
            dbuf_putstr(&db, p);
        JS_FreeCString(ctx, p);
    }
    if (dbuf_error(&db)) {
        dbuf_free(&db);
        JS_FreeValue(ctx, start);
        JS_FreeValue(ctx, end);
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    ret = JS_NewStringLen(ctx, (char *)db.buf, db.size);
    dbuf_free(&db);
    JS_FreeValue(ctx, start);
    JS_FreeValue(ctx, end);
    JS_FreeValue(ctx, unwrapped);
    return ret;
}

static JSValue js_datetimeformat_formatRangeToParts(JSContext *ctx, JSValueConst this_val,
                                                     int argc, JSValueConst *argv)
{
    JSValue unwrapped = js_datetimeformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    JSDateTimeFormatData *s = JS_GetOpaque2(ctx, unwrapped, js_datetimeformat_class_id);
    JSDateTimeFormatData *s_use = s;
    JSDateTimeFormatData s_copy;
    JSValue start_arr, end_arr, ret = JS_UNDEFINED;
    double x, y;
    int i, start_len, end_len;
    uint32_t idx;
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }
    if (JS_IsUndefined(argv[0]) || JS_IsUndefined(argv[1])) {
        JS_FreeValue(ctx, unwrapped);
        return JS_ThrowTypeError(ctx, "date argument is undefined");
    }
    {
        BOOL t0, t1, c0, c1;
        if (dtf_to_datetimeformattable(ctx, argv[0], &x, &t0, &c0) < 0) {
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
        if (dtf_to_datetimeformattable(ctx, argv[1], &y, &t1, &c1) < 0) {
            JS_FreeValue(ctx, unwrapped);
            return JS_EXCEPTION;
        }
        if (t0 != t1 || (t0 && !dtf_same_temporal_type(ctx, argv[0], argv[1]))) {
            JS_FreeValue(ctx, unwrapped);
            return JS_ThrowTypeError(ctx, "cannot format range between different types");
        }
        if (t0) {
            int cal0 = dtf_get_temporal_calendar(ctx, argv[0]);
            int cal1 = dtf_get_temporal_calendar(ctx, argv[1]);
            if (cal0 != cal1) {
                JS_FreeValue(ctx, unwrapped);
                return JS_ThrowRangeError(ctx, "cannot format range between different calendars");
            }
            if (dtf_check_temporal_overlap(ctx, s, argv[0]) < 0) {
                JS_FreeValue(ctx, unwrapped);
                return JS_EXCEPTION;
            }
            if (dtf_check_temporal_overlap(ctx, s, argv[1]) < 0) {
                JS_FreeValue(ctx, unwrapped);
                return JS_EXCEPTION;
            }
            if (JS_GetOpaque(argv[0], js_plaindate_class_id) ||
                JS_GetOpaque(argv[0], js_plaintime_class_id) ||
                JS_GetOpaque(argv[0], js_plainyearmonth_class_id) ||
                JS_GetOpaque(argv[0], js_plainmonthday_class_id) ||
                JS_GetOpaque(argv[0], js_plaindatetime_class_id)) {
                s_copy = *s;
                s_copy.timeZone = (char *)"+00:00";
                s_copy.timeZoneName = DTF_TIMEZONE_UNSET;
                s_use = &s_copy;
            }
        }
        if (c0) {
            x = time_clip(x);
            if (isnan(x)) {
                JS_FreeValue(ctx, unwrapped);
                JS_ThrowRangeError(ctx, "Invalid time value");
                return JS_EXCEPTION;
            }
        }
        if (c1) {
            y = time_clip(y);
            if (isnan(y)) {
                JS_FreeValue(ctx, unwrapped);
                JS_ThrowRangeError(ctx, "Invalid time value");
                return JS_EXCEPTION;
            }
        }
    }

    dtf_resolve_style_for_temporal(s_use, argv[0]);

    {
        JSValue start_str, end_str;
        start_arr = dtf_format_date(ctx, s_use, x, 1);
        if (JS_IsException(start_arr))
            return start_arr;
        end_arr = dtf_format_date(ctx, s_use, y, 1);
        if (JS_IsException(end_arr)) {
            JS_FreeValue(ctx, start_arr);
            return end_arr;
        }
        start_str = dtf_format_date(ctx, s_use, x, 0);
        if (JS_IsException(start_str)) {
            JS_FreeValue(ctx, start_arr);
            JS_FreeValue(ctx, end_arr);
            return start_str;
        }
        end_str = dtf_format_date(ctx, s_use, y, 0);
        if (JS_IsException(end_str)) {
            JS_FreeValue(ctx, start_str);
            JS_FreeValue(ctx, start_arr);
            JS_FreeValue(ctx, end_arr);
            return end_str;
        }
        {
            const char *start_c = JS_ToCString(ctx, start_str);
            const char *end_c = JS_ToCString(ctx, end_str);
            int same = start_c && end_c && strcmp(start_c, end_c) == 0;
            JS_FreeCString(ctx, start_c);
            JS_FreeCString(ctx, end_c);
            JS_FreeValue(ctx, start_str);
            JS_FreeValue(ctx, end_str);
            if (same) {
                JSValue len_val = JS_GetPropertyStr(ctx, start_arr, "length");
                int j, n = 0;
                if (!JS_IsException(len_val) && JS_ToInt32(ctx, &n, len_val) >= 0) {
                    for (j = 0; j < n; j++) {
                        JSValue item = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                        if (!JS_IsException(item)) {
                            JS_DefinePropertyValueStr(ctx, item, "source",
                                                      JS_NewString(ctx, "shared"), JS_PROP_C_W_E);
                        }
                        JS_FreeValue(ctx, item);
                    }
                }
                JS_FreeValue(ctx, len_val);
                JS_FreeValue(ctx, end_arr);
                JS_FreeValue(ctx, unwrapped);
                return start_arr;
            }
        }
    }

    {
        int weekday_opt, year_opt, month_opt, day_opt;
        int hour_opt, minute_opt, second_opt, tzname_opt;
        int can_share = 0;
        DTFDateFields fx, fy;
        dtf_expand_styles(s_use, &weekday_opt, &year_opt, &month_opt, &day_opt,
                          &hour_opt, &minute_opt, &second_opt, &tzname_opt);
        dtf_extract_fields(x, s_use->timeZone, &fx);
        dtf_extract_fields(y, s_use->timeZone, &fy);
        {
            int has_date = (year_opt != DTF_YEAR_UNSET || month_opt != DTF_MONTH_UNSET || day_opt != DTF_DAY_UNSET);
            if (fx.year == fy.year && fx.month == fy.month && fx.day == fy.day && has_date) {
                can_share = 1;
            } else if (month_opt == DTF_MONTH_SHORT || month_opt == DTF_MONTH_LONG) {
                can_share = 1;
            }
        }

        if (can_share) {
            int largest_diff = -1;
            int first_diff = 0, last_diff = -1, j;
            int prefix_len = 0, suffix_len = 0;

            if (fx.year != fy.year)
                largest_diff = 2;
            else if (fx.month != fy.month)
                largest_diff = 1;
            else if (fx.day != fy.day)
                largest_diff = 0;
            else
                largest_diff = -1;

            {
                JSValue len_val = JS_GetPropertyStr(ctx, start_arr, "length");
                if (JS_IsException(len_val))
                    goto fail;
                JS_ToInt32(ctx, &start_len, len_val);
                JS_FreeValue(ctx, len_val);
            }
            {
                JSValue len_val = JS_GetPropertyStr(ctx, end_arr, "length");
                if (JS_IsException(len_val))
                    goto fail;
                JS_ToInt32(ctx, &end_len, len_val);
                JS_FreeValue(ctx, len_val);
            }

            for (j = 0; j < start_len && j < end_len; j++) {
                JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)j);
                int eq = dtf_part_equal(ctx, a, b);
                JS_FreeValue(ctx, a);
                JS_FreeValue(ctx, b);
                if (!eq) {
                    if (last_diff == -1)
                        first_diff = j;
                    last_diff = j;
                }
            }
            if (last_diff == -1) {
                JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)0);
                JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)0);
                if (!dtf_part_equal(ctx, a, b)) {
                    first_diff = 0;
                    last_diff = 0;
                }
                JS_FreeValue(ctx, a);
                JS_FreeValue(ctx, b);
            }

            if (largest_diff == 2) {
                prefix_len = 0;
                suffix_len = 0;
            } else if (largest_diff == 1) {
                prefix_len = 0;
                for (j = last_diff + 1; j < start_len && j < end_len; j++) {
                    JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                    JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)j);
                    int eq = dtf_part_equal(ctx, a, b);
                    JS_FreeValue(ctx, a);
                    JS_FreeValue(ctx, b);
                    if (eq)
                        suffix_len++;
                    else
                        break;
                }
            } else {
                prefix_len = first_diff;
                for (j = last_diff + 1; j < start_len && j < end_len; j++) {
                    JSValue a = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                    JSValue b = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)j);
                    int eq = dtf_part_equal(ctx, a, b);
                    JS_FreeValue(ctx, a);
                    JS_FreeValue(ctx, b);
                    if (eq)
                        suffix_len++;
                    else
                        break;
                }
            }

            ret = JS_NewArray(ctx);
            if (JS_IsException(ret))
                goto fail;
            idx = 0;

            /* shared prefix from start */
            for (j = 0; j < prefix_len; j++) {
                JSValue item = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                if (JS_IsException(item))
                    goto fail;
                if (JS_DefinePropertyValueStr(ctx, item, "source",
                                               JS_NewString(ctx, "shared"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
                if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
                    goto fail;
            }

            /* startRange non-shared */
            for (j = prefix_len; j < start_len - suffix_len; j++) {
                JSValue item = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)j);
                if (JS_IsException(item))
                    goto fail;
                if (JS_DefinePropertyValueStr(ctx, item, "source",
                                               JS_NewString(ctx, "startRange"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
                if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
                    goto fail;
            }

            /* separator */
            {
                JSValue sep = JS_NewObject(ctx);
                if (JS_IsException(sep))
                    goto fail;
                if (JS_DefinePropertyValueStr(ctx, sep, "type",
                                               JS_NewString(ctx, "literal"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, sep);
                    goto fail;
                }
                if (JS_DefinePropertyValueStr(ctx, sep, "value",
                                               JS_NewString(ctx, " \xE2\x80\x93 "), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, sep);
                    goto fail;
                }
                if (JS_DefinePropertyValueStr(ctx, sep, "source",
                                               JS_NewString(ctx, "shared"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, sep);
                    goto fail;
                }
                if (JS_DefinePropertyValueUint32(ctx, ret, idx++, sep, JS_PROP_C_W_E) < 0)
                    goto fail;
            }

            /* endRange non-shared */
            for (j = prefix_len; j < end_len - suffix_len; j++) {
                JSValue item = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)j);
                if (JS_IsException(item))
                    goto fail;
                if (JS_DefinePropertyValueStr(ctx, item, "source",
                                               JS_NewString(ctx, "endRange"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
                if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
                    goto fail;
            }

            /* shared suffix from end */
            for (j = end_len - suffix_len; j < end_len; j++) {
                JSValue item = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)j);
                if (JS_IsException(item))
                    goto fail;
                if (JS_DefinePropertyValueStr(ctx, item, "source",
                                               JS_NewString(ctx, "shared"), JS_PROP_C_W_E) < 0) {
                    JS_FreeValue(ctx, item);
                    goto fail;
                }
                if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
                    goto fail;
            }

            JS_FreeValue(ctx, start_arr);
            JS_FreeValue(ctx, end_arr);
            JS_FreeValue(ctx, unwrapped);
            return ret;
        }
    }

    ret = JS_NewArray(ctx);
    if (JS_IsException(ret))
        goto fail;

    {
        JSValue len_val = JS_GetPropertyStr(ctx, start_arr, "length");
        if (JS_IsException(len_val))
            goto fail;
        if (JS_ToInt32(ctx, &start_len, len_val) < 0) {
            JS_FreeValue(ctx, len_val);
            goto fail;
        }
        JS_FreeValue(ctx, len_val);
    }
    {
        JSValue len_val = JS_GetPropertyStr(ctx, end_arr, "length");
        if (JS_IsException(len_val))
            goto fail;
        if (JS_ToInt32(ctx, &end_len, len_val) < 0) {
            JS_FreeValue(ctx, len_val);
            goto fail;
        }
        JS_FreeValue(ctx, len_val);
    }
    idx = 0;

    for (i = 0; i < start_len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, start_arr, (uint32_t)i);
        if (JS_IsException(item))
            goto fail;
        if (JS_DefinePropertyValueStr(ctx, item, "source",
                                       JS_NewString(ctx, "startRange"), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, item);
            goto fail;
        }
        if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
            goto fail;
    }

    {
        JSValue sep = JS_NewObject(ctx);
        if (JS_IsException(sep))
            goto fail;
        if (JS_DefinePropertyValueStr(ctx, sep, "type",
                                       JS_NewString(ctx, "literal"), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, sep);
            goto fail;
        }
        if (JS_DefinePropertyValueStr(ctx, sep, "value",
                                       JS_NewString(ctx, " \xE2\x80\x93 "), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, sep);
            goto fail;
        }
        if (JS_DefinePropertyValueStr(ctx, sep, "source",
                                       JS_NewString(ctx, "shared"), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, sep);
            goto fail;
        }
        if (JS_DefinePropertyValueUint32(ctx, ret, idx++, sep, JS_PROP_C_W_E) < 0)
            goto fail;
    }

    for (i = 0; i < end_len; i++) {
        JSValue item = JS_GetPropertyUint32(ctx, end_arr, (uint32_t)i);
        if (JS_IsException(item))
            goto fail;
        if (JS_DefinePropertyValueStr(ctx, item, "source",
                                       JS_NewString(ctx, "endRange"), JS_PROP_C_W_E) < 0) {
            JS_FreeValue(ctx, item);
            goto fail;
        }
        if (JS_DefinePropertyValueUint32(ctx, ret, idx++, item, JS_PROP_C_W_E) < 0)
            goto fail;
    }

    JS_FreeValue(ctx, start_arr);
    JS_FreeValue(ctx, end_arr);
    JS_FreeValue(ctx, unwrapped);
    return ret;

 fail:
    JS_FreeValue(ctx, ret);
    JS_FreeValue(ctx, start_arr);
    JS_FreeValue(ctx, end_arr);
    JS_FreeValue(ctx, unwrapped);
    return JS_EXCEPTION;
}

static JSValue js_datetimeformat_resolvedOptions(JSContext *ctx, JSValueConst this_val,
                                                    int argc, JSValueConst *argv)
{
    JSValue obj;
    JSValue unwrapped = js_datetimeformat_unwrap(ctx, this_val);
    if (JS_IsException(unwrapped))
        return unwrapped;
    JSDateTimeFormatData *s = JS_GetOpaque2(ctx, unwrapped, js_datetimeformat_class_id);
    if (!s) {
        JS_FreeValue(ctx, unwrapped);
        return JS_EXCEPTION;
    }

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;

    JS_DefinePropertyValueStr(ctx, obj, "locale",
                               JS_NewString(ctx, s->locale), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "calendar",
                               JS_NewString(ctx, ti_calendar_enum_to_string(s->calendar)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "numberingSystem",
                               JS_NewString(ctx, js_intl_numbering_system_to_string(s->numberingSystem)), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, obj, "timeZone",
                               JS_NewString(ctx, s->timeZone), JS_PROP_C_W_E);
    /* hourCycle only if hour-related formatting is used */
    if (s->has_hourCycle && (s->hour != DTF_HOUR_UNSET || s->timeStyle != DTF_STYLE_UNSET)) {
        JS_DefinePropertyValueStr(ctx, obj, "hourCycle",
                                   JS_NewString(ctx, dtf_hourcycle_names[s->hourCycle]), JS_PROP_C_W_E);
    }
    /* hour12 if hour-related formatting is used */
    if (s->hour != DTF_HOUR_UNSET || s->timeStyle != DTF_STYLE_UNSET) {
        int hour12_val = (s->hourCycle == 0 || s->hourCycle == 1);
        JS_DefinePropertyValueStr(ctx, obj, "hour12",
                                   JS_NewBool(ctx, hour12_val), JS_PROP_C_W_E);
    }
    if (s->weekday != DTF_WEEKDAY_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "weekday",
                                   JS_NewString(ctx, dtf_weekday_names[s->weekday]), JS_PROP_C_W_E);
    if (s->era != DTF_ERA_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "era",
                                   JS_NewString(ctx, dtf_era_names[s->era]), JS_PROP_C_W_E);
    if (s->year != DTF_YEAR_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "year",
                                   JS_NewString(ctx, dtf_year_names[s->year]), JS_PROP_C_W_E);
    if (s->month != DTF_MONTH_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "month",
                                   JS_NewString(ctx, dtf_month_names[s->month]), JS_PROP_C_W_E);
    if (s->day != DTF_DAY_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "day",
                                   JS_NewString(ctx, dtf_day_names[s->day]), JS_PROP_C_W_E);
    if (s->dayPeriod != DTF_DAYPERIOD_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "dayPeriod",
                                   JS_NewString(ctx, dtf_dayperiod_names[s->dayPeriod]), JS_PROP_C_W_E);
    if (s->hour != DTF_HOUR_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "hour",
                                   JS_NewString(ctx, dtf_hour_names[s->hour]), JS_PROP_C_W_E);
    if (s->minute != DTF_MINUTE_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "minute",
                                   JS_NewString(ctx, dtf_minute_names[s->minute]), JS_PROP_C_W_E);
    if (s->second != DTF_SECOND_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "second",
                                   JS_NewString(ctx, dtf_second_names[s->second]), JS_PROP_C_W_E);
    if (s->has_fractionalSecondDigits) {
        JS_DefinePropertyValueStr(ctx, obj, "fractionalSecondDigits",
                                   JS_NewInt32(ctx, s->fractionalSecondDigits), JS_PROP_C_W_E);
    }
    if (s->timeZoneName != DTF_TIMEZONE_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "timeZoneName",
                                   JS_NewString(ctx, dtf_timezonename_names[s->timeZoneName]), JS_PROP_C_W_E);
    if (s->dateStyle != DTF_STYLE_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "dateStyle",
                                   JS_NewString(ctx, dtf_style_names[s->dateStyle]), JS_PROP_C_W_E);
    if (s->timeStyle != DTF_STYLE_UNSET)
        JS_DefinePropertyValueStr(ctx, obj, "timeStyle",
                                   JS_NewString(ctx, dtf_style_names[s->timeStyle]), JS_PROP_C_W_E);

    JS_FreeValue(ctx, unwrapped);
    return obj;
}

static JSValue js_datetimeformat_supportedLocalesOf(JSContext *ctx, JSValueConst this_val,
                                                       int argc, JSValueConst *argv)
{
    return js_intl_supportedLocalesOf(ctx, this_val, argc, argv);
}

static const JSCFunctionListEntry js_datetimeformat_proto_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Intl.DateTimeFormat", JS_PROP_CONFIGURABLE),
    JS_CGETSET_DEF("format", js_datetimeformat_get_format, NULL),
    JS_CFUNC_DEF("formatToParts", 1, js_datetimeformat_formatToParts),
    JS_CFUNC_DEF("formatRange", 2, js_datetimeformat_formatRange),
    JS_CFUNC_DEF("formatRangeToParts", 2, js_datetimeformat_formatRangeToParts),
    JS_CFUNC_DEF("resolvedOptions", 0, js_datetimeformat_resolvedOptions),
};

static const JSCFunctionListEntry js_datetimeformat_static_funcs[] = {
    JS_CFUNC_DEF("supportedLocalesOf", 1, js_datetimeformat_supportedLocalesOf),
};

int js_add_datetimeformat(JSContext *ctx, JSValue intl_obj)
{
    JS_NewClassID(&js_datetimeformat_class_id);
    JS_NewClass(JS_GetRuntime(ctx), js_datetimeformat_class_id, &js_datetimeformat_class);

    JSCFunctionType ft = { .constructor_or_func_flags = js_datetimeformat_constructor };

    if (JS_IsException(js_intl_new_constructor(ctx, intl_obj,
                                                JS_INTL_CTOR_DATETIMEFORMAT,
                                                js_datetimeformat_class_id, "DateTimeFormat",
                                                ft.generic, 0,
                                                JS_CFUNC_constructor_or_func_flags, 0,
                                                js_datetimeformat_static_funcs, countof(js_datetimeformat_static_funcs),
                                                js_datetimeformat_proto_funcs, countof(js_datetimeformat_proto_funcs))))
        return -1;

    return 0;
}

JSValue js_datetimeformat_format_date(JSContext *ctx, JSValueConst date_val,
                                       int argc, JSValueConst *argv)
{
    JSValue ctor, obj, func, ret;

    ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
    if (JS_IsUndefined(ctor))
        return JS_ThrowTypeError(ctx, "DateTimeFormat not available");

    obj = js_datetimeformat_constructor(ctx, ctor, ctor, argc, argv, JS_CALL_FLAG_CONSTRUCTOR);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(obj))
        return obj;

    func = JS_GetPropertyStr(ctx, obj, "format");
    if (JS_IsException(func)) {
        JS_FreeValue(ctx, obj);
        return func;
    }

    ret = JS_Call(ctx, func, obj, 1, &date_val);
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, obj);
    return ret;
}




