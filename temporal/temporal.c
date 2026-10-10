/*
 * QuickJS Temporal implementation
 */

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"
#include "../intl/intl_private.h"

static int ti_is_valid_plain_offset_string(const char *str);

/* Parse a Temporal time zone string and return the fixed offset in nanoseconds.
   Returns 0 on success, -1 on RangeError, -2 on TypeError.
   Handles: "UTC", "±HH:MM", ISO date-time strings with Z/offset/bracket annotation.
   This is a partial implementation of ToTemporalTimeZone for fixed-offset time zones. */
int ti_parse_temporal_timezone_string(const char *str, int64_t *offset_ns)
{
    int has_bracket = 0;
    const char *bracket_open, *bracket_close;
    size_t len;

    if (!str || !str[0])
        return -1;
    len = strlen(str);

    /* Look for bracket annotation [name] at the end */
    bracket_open = strchr(str, '[');
    if (bracket_open) {
        bracket_close = strchr(bracket_open, ']');
        if (!bracket_close || bracket_close[1] != '\0')
            return -1;
        has_bracket = 1;
        /* Extract bracket content */
        if (bracket_close == bracket_open + 1)
            return -1;
        int inner_len = (int)(bracket_close - bracket_open - 1);
        if (inner_len == 3 && (bracket_open[1] == 'U' || bracket_open[1] == 'u') &&
            (bracket_open[2] == 'T' || bracket_open[2] == 't') &&
            (bracket_open[3] == 'C' || bracket_open[3] == 'c')) {
            *offset_ns = 0;
        } else if (inner_len == 6 && (bracket_open[1] == '+' || bracket_open[1] == '-')) {
            /* Offset in brackets like [+01:46] */
            int sign = (bracket_open[1] == '+') ? 1 : -1;
            int h1 = bracket_open[2] - '0';
            int h2 = bracket_open[3] - '0';
            if (bracket_open[4] != ':' || h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9)
                return -1;
            int m1 = bracket_open[5] - '0';
            int m2 = bracket_open[6] - '0';
            if (m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9)
                return -1;
            int hours = h1 * 10 + h2;
            int mins = m1 * 10 + m2;
            if (hours > 23 || mins > 59)
                return -1;
            *offset_ns = sign * ((int64_t)hours * 3600 + mins * 60) * 1000000000LL;
        } else {
            /* Accept IANA-like names in brackets: letters, slashes, underscores, hyphens, plus */
            int i;
            int valid_iana = 1;
            for (i = 1; i <= inner_len; i++) {
                char c = bracket_open[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) {
                    valid_iana = 0;
                    break;
                }
            }
            if (valid_iana && inner_len > 0) {
                *offset_ns = 0;
            } else {
                return -1;
            }
        }
    }

    /* If there's a bracket, validate the prefix is a valid date-time or time string */
    if (has_bracket) {
        int prefix_len = (int)(bracket_open - str);
        if (prefix_len == 0)
            return -1;
        if (prefix_len >= 7 && !memcmp(str, "-000000", 7))
            return -1;
        if (prefix_len > 0 && str[prefix_len - 1] == 'Z') {
            /* OK */
        } else {
            if (prefix_len >= 6 && (str[prefix_len - 6] == '+' || str[prefix_len - 6] == '-')
                && str[prefix_len - 3] == ':') {
                /* OK - regular offset */
            } else if (prefix_len == 8 && str[2] == ':' && str[5] == ':') {
                /* Time-only like HH:MM:SS */
                /* OK */
            } else if (memchr(str, 'T', (size_t)prefix_len) != NULL) {
                /* Date-time prefix */
                /* OK */
            } else {
                return -1;
            }
        }
        return 0;
    }

    /* No bracket - plain identifiers */
    if (len == 3 && (str[0] == 'U' || str[0] == 'u') &&
        (str[1] == 'T' || str[1] == 't') &&
        (str[2] == 'C' || str[2] == 'c')) {
        *offset_ns = 0;
        return 0;
    }

    /* Plain offset string ±HH */
    if (len == 3 && (str[0] == '+' || str[0] == '-')) {
        int sign = (str[0] == '+') ? 1 : -1;
        int h1 = str[1] - '0';
        int h2 = str[2] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9)
            return -1;
        int hours = h1 * 10 + h2;
        if (hours > 23)
            return -1;
        *offset_ns = sign * (int64_t)hours * 3600 * 1000000000LL;
        return 0;
    }

    /* Plain offset string ±HH:MM */
    if (len == 6 && (str[0] == '+' || str[0] == '-')) {
        int sign = (str[0] == '+') ? 1 : -1;
        int h1 = str[1] - '0';
        int h2 = str[2] - '0';
        int m1 = str[4] - '0';
        int m2 = str[5] - '0';
        if (str[3] != ':' || h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9 ||
            m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9)
            return -1;
        int hours = h1 * 10 + h2;
        int mins = m1 * 10 + m2;
        if (hours > 23 || mins > 59)
            return -1;
        *offset_ns = sign * ((int64_t)hours * 3600 + mins * 60) * 1000000000LL;
        return 0;
    }

    /* Plain offset string ±HHMM */
    if (len == 5 && (str[0] == '+' || str[0] == '-')) {
        int sign = (str[0] == '+') ? 1 : -1;
        int h1 = str[1] - '0';
        int h2 = str[2] - '0';
        int m1 = str[3] - '0';
        int m2 = str[4] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9 ||
            m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9)
            return -1;
        int hours = h1 * 10 + h2;
        int mins = m1 * 10 + m2;
        if (hours > 23 || mins > 59)
            return -1;
        *offset_ns = sign * ((int64_t)hours * 3600 + mins * 60) * 1000000000LL;
        return 0;
    }

    /* ISO date-time string ending with Z */
    if (len > 1 && str[len - 1] == 'Z') {
        if (len >= 7 && !memcmp(str, "-000000", 7))
            return -1;
        *offset_ns = 0;
        return 0;
    }

    /* ISO date-time string with ±HHMM or ±HH:MM offset at end */
    if (len >= 5) {
        const char *offset_start = NULL;
        int offset_len = 0;
        int i;
        for (i = (int)(len - 6); i >= 0; i--) {
            if ((str[i] == '+' || str[i] == '-') && str[i + 3] == ':') {
                offset_start = str + i;
                offset_len = 6;
                break;
            }
        }
        if (!offset_start) {
            for (i = (int)(len - 5); i >= 0; i--) {
                if ((str[i] == '+' || str[i] == '-') &&
                    str[i + 1] >= '0' && str[i + 1] <= '9' &&
                    str[i + 2] >= '0' && str[i + 2] <= '9' &&
                    str[i + 3] >= '0' && str[i + 3] <= '5' &&
                    str[i + 4] >= '0' && str[i + 4] <= '9') {
                    offset_start = str + i;
                    offset_len = 5;
                    break;
                }
            }
        }
        if (offset_start) {
            int sign = (offset_start[0] == '+') ? 1 : -1;
            int h1 = offset_start[1] - '0';
            int h2 = offset_start[2] - '0';
            int m1, m2;
            if (offset_len == 6) {
                m1 = offset_start[4] - '0';
                m2 = offset_start[5] - '0';
            } else {
                m1 = offset_start[3] - '0';
                m2 = offset_start[4] - '0';
            }
            if (h1 >= 0 && h1 <= 9 && h2 >= 0 && h2 <= 9 &&
                m1 >= 0 && m1 <= 5 && m2 >= 0 && m2 <= 9) {
                int hours = h1 * 10 + h2;
                int mins = m1 * 10 + m2;
                if (hours <= 23 && mins <= 59) {
                    if ((int)len > (int)(offset_start - str) + offset_len && offset_start[offset_len] == ':')
                        return -1;
                    if ((int)len > (int)(offset_start - str) + offset_len && offset_start[offset_len] == '.')
                        return -1;
                    if (len >= 7 && !memcmp(str, "-000000", 7))
                        return -1;
                    *offset_ns = sign * ((int64_t)hours * 3600 + mins * 60) * 1000000000LL;
                    return 0;
                }
            }
        }
    }

    /* Plain IANA time zone name */
    {
        int valid_iana = 1;
        size_t i;
        for (i = 0; i < len; i++) {
            char c = str[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) {
                valid_iana = 0;
                break;
            }
        }
        if (valid_iana && len > 0) {
            *offset_ns = 0;
            return 0;
        }
    }

    return -1;
}

/* Return the default time zone identifier from the TZ environment variable.
   Returns a newly allocated string, or NULL if not set or empty. */
void ti_get_default_timezone(char *buf, size_t buf_size)
{
    const char *tz = getenv("TZ");
    if (tz && tz[0]) {
        pstrcpy(buf, buf_size, tz);
        return;
    }
#if !defined(_WIN32)
    {
        char linkbuf[512];
        ssize_t len = readlink("/etc/localtime", linkbuf, sizeof(linkbuf) - 1);
        if (len > 0) {
            const char *p;
            linkbuf[len] = '\0';
            p = strstr(linkbuf, "/zoneinfo/");
            if (p) {
                pstrcpy(buf, buf_size, p + 10);
                return;
            }
        }
    }
    {
        FILE *f = fopen("/etc/timezone", "r");
        if (f) {
            char line[256];
            if (fgets(line, sizeof(line), f)) {
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\n')
                    line[len - 1] = '\0';
                fclose(f);
                if (line[0]) {
                    pstrcpy(buf, buf_size, line);
                    return;
                }
            }
            fclose(f);
        }
    }
#endif    
    /* fallback: compute offset from localtime */
    {
        time_t t = time(NULL);
        int offset_min = 0;
#if defined(_WIN32)
        {
            struct tm *tm;
            time_t gm_ti, loc_ti;

            tm = gmtime(&t);
            if (tm) {
                gm_ti = mktime(tm);
                tm = localtime(&t);
                if (tm) {
                    loc_ti = mktime(tm);
                    offset_min = (int)((gm_ti - loc_ti) / 60);
                }
            }
        }
#else
        {
            struct tm tm;
            if (localtime_r(&t, &tm))
                offset_min = (int)(-tm.tm_gmtoff / 60);
        }
#endif
        if (offset_min % 60 == 0) {
            snprintf(buf, buf_size, "Etc/GMT%+d", offset_min / 60);
        } else {
            int abs_min = abs(offset_min);
            char sign = offset_min <= 0 ? '+' : '-';
            snprintf(buf, buf_size, "%c%02d:%02d", sign, abs_min / 60, abs_min % 60);
        }
        return;
    }
}

/* Canonicalize a calendar identifier per spec CanonicalizeCalendar.
   Returns 0 on success with calendar type in *out, -1 on RangeError, -2 on TypeError. */
static int ti_canonicalize_calendar(JSContext *ctx, JSValue val, CalendarType *out)
{
    const char *str;
    size_t len, i;
    char *lower;

    if (!JS_IsString(val))
        return -2;

    str = JS_ToCStringLen(ctx, &len, val);
    if (!str)
        return -2;

    if (len == 0) {
        JS_FreeCString(ctx, str);
        return -1;
    }

    lower = js_malloc(ctx, len + 1);
    if (!lower) {
        JS_FreeCString(ctx, str);
        return -2;
    }

    for (i = 0; i < len; i++) {
        char c = str[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }
    lower[len] = '\0';

    if (!strcmp(lower, "ethiopic-amete-alem"))
        *out = CAL_ETHIOAA;
    else if (!strcmp(lower, "islamicc"))
        *out = CAL_ISLAMIC_CIVIL;
    else
        *out = ti_calendar_string_to_enum(lower);

    js_free(ctx, lower);
    JS_FreeCString(ctx, str);

    if (*out == CAL_UNKNOWN)
        return -1;
    return 0;
}

/* Parse a calendar string per ParseTemporalCalendarString / ToTemporalCalendarIdentifier.
   Returns 0 on success with calendar type in *out, -1 on RangeError, -2 on TypeError. */
static int ti_parse_temporal_calendar_string(JSContext *ctx, JSValue val, CalendarType *out)
{
    const char *str;
    size_t len, i;
    const char *cal = NULL;
    size_t cal_len = 0;
    int looks_like_iso = 0;
    int has_annotation = 0;

    if (!JS_IsString(val))
        return -2;

    str = JS_ToCStringLen(ctx, &len, val);
    if (!str)
        return -2;

    if (len == 0) {
        JS_FreeCString(ctx, str);
        return -1;
    }

    if (ti_is_minus_zero_year(str)) {
        JS_FreeCString(ctx, str);
        return -1;
    }

    {
        char first = str[0];
        if (isdigit((unsigned char)first) || first == '+' || first == '-') {
            looks_like_iso = 1;
        }
    }

    for (i = 0; i < len; i++) {
        char c = str[i];
        if (c == '-' || c == ':' || c == 'T' || c == 't' ||
            c == '.' || c == ',' || (c == '+' && i == 0)) {
            looks_like_iso = 1;
        }
        if (c == '[') {
            const char *end = strchr(str + i, ']');
            if (!end) {
                JS_FreeCString(ctx, str);
                return -1;
            }
            has_annotation = 1;
            {
                const char *content = str + i + 1;
                size_t content_len = (size_t)(end - content);
                int j;
                int is_bang = 0;
                if (content_len > 0 && content[0] == '!') {
                    is_bang = 1;
                    content++;
                    content_len--;
                }
                if (content_len >= 5 && !memcmp(content, "u-ca=", 5)) {
                    cal = content + 5;
                    cal_len = content_len - 5;
                } else if (looks_like_iso) {
                    if (is_bang) {
                        JS_FreeCString(ctx, str);
                        return -1;
                    }
                } else {
                    if (content_len == 0) {
                        JS_FreeCString(ctx, str);
                        return -1;
                    }
                    {
                        char k0 = content[0];
                        if (!(islower((unsigned char)k0) || k0 == '_')) {
                            JS_FreeCString(ctx, str);
                            return -1;
                        }
                    }
                    for (j = 0; j < (int)content_len; j++) {
                        char kc = content[j];
                        if (kc == '=') {
                            break;
                        }
                        if (!(islower((unsigned char)kc) || isdigit((unsigned char)kc) || kc == '_' || kc == '-')) {
                            JS_FreeCString(ctx, str);
                            return -1;
                        }
                    }
                }
            }
            i = (size_t)(end - str);
        }
    }

    if (!looks_like_iso) {
        if (has_annotation) {
            JS_FreeCString(ctx, str);
            return -1;
        }
        cal = str;
        cal_len = len;
    } else {
        if (cal == NULL) {
            JS_FreeCString(ctx, str);
            *out = CAL_ISO8601;
            return 0;
        }
    }

    if (cal == NULL || cal_len == 0) {
        JS_FreeCString(ctx, str);
        return -1;
    }

    {
        char *lower = js_malloc(ctx, cal_len + 1);
        if (!lower) {
            JS_FreeCString(ctx, str);
            return -1;
        }
        for (i = 0; i < cal_len; i++) {
            char c = cal[i];
            lower[i] = (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
        }
        lower[cal_len] = '\0';

        if (!strcmp(lower, "ethiopic-amete-alem"))
            *out = CAL_ETHIOAA;
        else if (!strcmp(lower, "islamicc"))
            *out = CAL_ISLAMIC_CIVIL;
        else
            *out = ti_calendar_string_to_enum(lower);

        js_free(ctx, lower);
    }

    JS_FreeCString(ctx, str);
    if (*out == CAL_UNKNOWN)
        return -1;
    return 0;
}

/* Shared ISO date/time string parser for Temporal.
   Parses date, time, timezone offset/Z, and bracket annotations.
   Returns 0 on success, -1 on error (RangeError). */
int ti_parse_iso_datetime_string(JSContext *ctx, const char *str, TISODateTimeParseResult *out, int flags)
{
    const char *p = str;
    int has_sign = 0;
    int sign = 1;
    int i;

    memset(out, 0, sizeof(*out));

    /* Reject non-ASCII */
    for (i = 0; str[i]; i++) {
        if ((unsigned char)str[i] > 127)
            goto error;
    }

    /* Reject empty string */
    if (*p == '\0')
        goto error;

    /* Check for sign */
    if (*p == '+' || *p == '-') {
        has_sign = 1;
        sign = (*p == '-') ? -1 : 1;
        p++;
    }

    /* Try to parse date part */
    {
        const char *date_start = p;
        int year_len = 0;
        int month_len = 0;
        int day_len = 0;

        if ((flags & TI_PARSE_PLAIN_TIME)) {
            const char *q = str;
            while (*q && *q != '[') {
                if (*q == 'T' || *q == 't' || *q == ' ')
                    break;
                q++;
            }
            if (!*q || *q == '[')
                goto no_date;
        }

        if ((flags & TI_PARSE_PLAIN_MONTHDAY) && !has_sign) {
            /* MM-DD or MMDD */
            if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' &&
                p[2] == '-' &&
                p[3] >= '0' && p[3] <= '9' && p[4] >= '0' && p[4] <= '9') {
                const char *q = p + 5;
                if (*q == '\0' || *q == 'T' || *q == 't' || *q == '[') {
                    out->has_monthday = 1;
                    out->month = (p[0]-'0')*10 + (p[1]-'0');
                    out->day = (p[3]-'0')*10 + (p[4]-'0');
                    p = q;
                    goto no_date;
                }
            } else if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' &&
                       p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9') {
                const char *q = p + 4;
                if (*q == '\0' || *q == 'T' || *q == 't' || *q == '[') {
                    out->has_monthday = 1;
                    out->month = (p[0]-'0')*10 + (p[1]-'0');
                    out->day = (p[2]-'0')*10 + (p[3]-'0');
                    p = q;
                    goto no_date;
                }
            }
        }

        if ((flags & TI_PARSE_PLAIN_MONTHDAY) && has_sign && sign == -1 && p[0] == '-') {
            /* --MM-DD or --MMDD */
            const char *q = p + 1;
            if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9' &&
                q[2] == '-' &&
                q[3] >= '0' && q[3] <= '9' && q[4] >= '0' && q[4] <= '9') {
                const char *r = q + 5;
                if (*r == '\0' || *r == 'T' || *r == 't' || *r == '[') {
                    out->has_monthday = 1;
                    out->month = (q[0]-'0')*10 + (q[1]-'0');
                    out->day = (q[3]-'0')*10 + (q[4]-'0');
                    p = r;
                    goto no_date;
                }
            } else if (q[0] >= '0' && q[0] <= '9' && q[1] >= '0' && q[1] <= '9' &&
                       q[2] >= '0' && q[2] <= '9' && q[3] >= '0' && q[3] <= '9') {
                const char *r = q + 4;
                if (*r == '\0' || *r == 'T' || *r == 't' || *r == '[') {
                    out->has_monthday = 1;
                    out->month = (q[0]-'0')*10 + (q[1]-'0');
                    out->day = (q[2]-'0')*10 + (q[3]-'0');
                    p = r;
                    goto no_date;
                }
            }
            /* Not a valid --MM-DD, rewind and continue as normal date parsing */
            p = date_start - 1;
            has_sign = 0;
        }

        /* Count leading digits */
        while (*p >= '0' && *p <= '9') {
            year_len++;
            p++;
        }

        if (year_len == 4 && !has_sign) {
            /* Four-digit year without sign */
            if (*p == '-') {
                p++;
                /* Count month digits */
                while (*p >= '0' && *p <= '9') {
                    month_len++;
                    p++;
                }
                if (month_len == 2) {
                    if (*p == '-') {
                        p++;
                        /* Count day digits */
                        while (*p >= '0' && *p <= '9') {
                            day_len++;
                            p++;
                        }
                        if (day_len == 2) {
                            /* YYYY-MM-DD */
                            out->has_date = 1;
                            out->year = ((date_start[0]-'0')*1000 + (date_start[1]-'0')*100 +
                                         (date_start[2]-'0')*10 + (date_start[3]-'0'));
                            out->month = ((date_start[5]-'0')*10 + (date_start[6]-'0'));
                            out->day = ((date_start[8]-'0')*10 + (date_start[9]-'0'));
                        } else {
                            goto error;
                        }
                    } else if (*p == '\0' || *p == 'T' || *p == 't' || *p == ' ' || *p == '[') {
                        /* YYYY-MM - yearmonth only, no day */
                        /* Don't consume, rewind to after year-month part */
                        p = date_start + 4 + 1 + 2; /* after YYYY-MM */
                        out->has_yearmonth = 1;
                        out->year = ((date_start[0]-'0')*1000 + (date_start[1]-'0')*100 +
                                     (date_start[2]-'0')*10 + (date_start[3]-'0'));
                        out->month = ((date_start[5]-'0')*10 + (date_start[6]-'0'));
                        out->day = 1; /* default */
                    } else {
                        goto error;
                    }
                } else {
                    goto error;
                }
            } else if (year_len == 4) {
                /* No separator after 4-digit year: check for compact YYYYMMDD */
                if (*p == '\0' || *p == 'T' || *p == 't' || *p == ' ') {
                    /* Only 4 digits - not enough for compact date */
                    /* This could be just a year, not a date */
                    p = date_start;
                } else {
                    /* Trailing junk after year */
                    p = date_start;
                }
            }
        } else if (year_len == 6 && has_sign) {
            /* Signed extended year: ±YYYYYY */
            if (*p == '-') {
                p++;
                while (*p >= '0' && *p <= '9') {
                    month_len++;
                    p++;
                }
                if (month_len == 2) {
                    if (*p == '-') {
                        p++;
                        while (*p >= '0' && *p <= '9') {
                            day_len++;
                            p++;
                        }
                        if (day_len == 2) {
                            out->has_date = 1;
                            out->year = sign * ((date_start[0]-'0')*100000 + (date_start[1]-'0')*10000 +
                                                (date_start[2]-'0')*1000 + (date_start[3]-'0')*100 +
                                                (date_start[4]-'0')*10 + (date_start[5]-'0'));
                            if (out->year == 0 && sign < 0)
                                goto error;
                            out->month = (date_start[7]-'0')*10 + (date_start[8]-'0');
                            out->day = (date_start[10]-'0')*10 + (date_start[11]-'0');
                        } else {
                            goto error;
                        }
                    } else if (*p == '\0' || *p == 'T' || *p == 't' || *p == ' ' || *p == '[') {
                        /* ±YYYYYY-MM - yearmonth only, no day */
                        p = date_start + 6 + 1 + 2; /* after ±YYYYYY-MM */
                        out->has_yearmonth = 1;
                        out->year = sign * ((date_start[0]-'0')*100000 + (date_start[1]-'0')*10000 +
                                            (date_start[2]-'0')*1000 + (date_start[3]-'0')*100 +
                                            (date_start[4]-'0')*10 + (date_start[5]-'0'));
                        if (out->year == 0 && sign < 0)
                            goto error;
                        out->month = (date_start[7]-'0')*10 + (date_start[8]-'0');
                        out->day = 1; /* default */
                    } else {
                        goto error;
                    }
                } else {
                    goto error;
                }
            } else if (year_len == 6) {
                /* ±YYYYYY with no separator: check for ±YYYYYYMMDD */
                if (*p == '\0' || *p == 'T' || *p == 't' || *p == ' ') {
                    /* Only 6 digits - not enough */
                    p = date_start - 1; /* rewind to include sign */
                } else {
                    p = date_start - 1;
                }
            }
        } else if (year_len == 6 && !has_sign) {
            /* Compact YYYYMM without sign and without separators */
            out->has_yearmonth = 1;
            out->year = ((date_start[0]-'0')*1000 + (date_start[1]-'0')*100 +
                         (date_start[2]-'0')*10 + (date_start[3]-'0'));
            out->month = (date_start[4]-'0')*10 + (date_start[5]-'0');
            out->day = 1; /* default */
        } else if (year_len == 8 && !has_sign) {
            /* Compact YYYYMMDD without sign and without separators */
            /* We already consumed 8 digits. Validate positions. */
            out->has_date = 1;
            out->year = ((date_start[0]-'0')*1000 + (date_start[1]-'0')*100 +
                         (date_start[2]-'0')*10 + (date_start[3]-'0'));
            out->month = (date_start[4]-'0')*10 + (date_start[5]-'0');
            out->day = (date_start[6]-'0')*10 + (date_start[7]-'0');
            /* Don't rewind - p is already after 8 digits */
        } else if (year_len == 8 && has_sign) {
            /* Compact ±YYYYYYMM without separators */
            out->has_yearmonth = 1;
            out->year = sign * ((date_start[0]-'0')*100000 + (date_start[1]-'0')*10000 +
                                (date_start[2]-'0')*1000 + (date_start[3]-'0')*100 +
                                (date_start[4]-'0')*10 + (date_start[5]-'0'));
            if (out->year == 0 && sign < 0)
                goto error;
            out->month = (date_start[6]-'0')*10 + (date_start[7]-'0');
            out->day = 1; /* default */
        } else if (year_len == 10 && has_sign) {
            /* Compact ±YYYYYYMMDD */
            out->has_date = 1;
            out->year = sign * ((date_start[0]-'0')*100000 + (date_start[1]-'0')*10000 +
                                (date_start[2]-'0')*1000 + (date_start[3]-'0')*100 +
                                (date_start[4]-'0')*10 + (date_start[5]-'0'));
            if (out->year == 0 && sign < 0)
                goto error;
            out->month = (date_start[6]-'0')*10 + (date_start[7]-'0');
            out->day = (date_start[8]-'0')*10 + (date_start[9]-'0');
        } else {
            /* Not a recognizable date prefix */
            if (has_sign)
                p = date_start - 1;
            else
                p = date_start;
        }

    no_date:;
    }

    /* Check for time separator and parse time */
    if (*p == 'T' || *p == 't' || (*p == ' ' && out->has_date)) {
        p++;
        {
            int h1 = 0, h2 = 0, m1 = 0, m2 = 0, s1 = 0, s2 = 0;
            int has_explicit_seconds = 0;

            /* Try extended format HH:MM:SS or HH:MM */
            if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                h1 = p[0]-'0'; h2 = p[1]-'0';
                p += 2;
                if (*p == ':') {
                    p++;
                    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                        m1 = p[0]-'0'; m2 = p[1]-'0';
                        p += 2;
                        if (*p == ':') {
                            p++;
                            if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                                s1 = p[0]-'0'; s2 = p[1]-'0';
                                p += 2;
                                has_explicit_seconds = 1;
                                out->has_time = 1;
                                out->hour = h1*10 + h2;
                                out->minute = m1*10 + m2;
                                out->second = s1*10 + s2;
                            } else {
                                goto error;
                            }
                        } else {
                            /* HH:MM */
                            out->has_time = 1;
                            out->hour = h1*10 + h2;
                            out->minute = m1*10 + m2;
                            out->second = 0;
                        }
                    } else {
                        goto error;
                    }
                } else {
                    /* Could be compact HHMMSS or HHMM after T */
                    /* We already consumed HH, now check for MM and SS */
                    if (*p >= '0' && *p <= '9' && p[1] >= '0' && p[1] <= '9') {
                        m1 = p[0]-'0'; m2 = p[1]-'0';
                        p += 2;
                        if (*p >= '0' && *p <= '9' && p[1] >= '0' && p[1] <= '9') {
                            s1 = p[0]-'0'; s2 = p[1]-'0';
                            p += 2;
                            has_explicit_seconds = 1;
                            out->has_time = 1;
                            out->hour = h1*10 + h2;
                            out->minute = m1*10 + m2;
                            out->second = s1*10 + s2;
                        } else {
                            out->has_time = 1;
                            out->hour = h1*10 + h2;
                            out->minute = m1*10 + m2;
                            out->second = 0;
                        }
                    } else {
                        out->has_time = 1;
                        out->hour = h1*10 + h2;
                        out->minute = 0;
                        out->second = 0;
                    }
                }
            } else if (p[0] >= '0' && p[0] <= '9') {
                /* Single digit hour */
                h1 = p[0]-'0';
                p += 1;
                if (*p == ':') {
                    p++;
                    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                        m1 = p[0]-'0'; m2 = p[1]-'0';
                        p += 2;
                        out->has_time = 1;
                        out->hour = h1;
                        out->minute = m1*10 + m2;
                        out->second = 0;
                    } else {
                        goto error;
                    }
                } else {
                    goto error;
                }
            } else {
                goto error;
            }

            if (out->has_time) {
                if (out->hour < 0 || out->hour > 23 || out->minute < 0 || out->minute > 59 || out->second < 0 || out->second > 60)
                    goto error;
                if (out->second == 60)
                    out->second = 59;
            }

            /* Fractional seconds */
            if (*p == '.' || *p == ',') {
                if (!has_explicit_seconds)
                    goto error;
                int frac[9] = {0};
                int frac_len = 0;
                p++;
                while (*p >= '0' && *p <= '9' && frac_len < 9) {
                    frac[frac_len++] = *p - '0';
                    p++;
                }
                if (frac_len == 0)
                    goto error;
                for (i = frac_len; i < 9; i++) frac[i] = 0;
                out->millisecond = frac[0]*100 + frac[1]*10 + frac[2];
                out->microsecond = frac[3]*100 + frac[4]*10 + frac[5];
                out->nanosecond = frac[6]*100 + frac[7]*10 + frac[8];
            }
        }
    } else if (!out->has_date) {
        /* No date - try bare time (unambiguous) */
        /* This is for PlainTime strings without date prefix */
        int has_explicit_seconds = 0;
        if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
            int h1 = p[0]-'0', h2 = p[1]-'0';
            p += 2;
            if (*p == ':') {
                p++;
                if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                    int m1 = p[0]-'0', m2 = p[1]-'0';
                    p += 2;
                    if (*p == ':') {
                        p++;
                        if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                            int s1 = p[0]-'0', s2 = p[1]-'0';
                            p += 2;
                            has_explicit_seconds = 1;
                            out->has_time = 1;
                            out->hour = h1*10 + h2;
                            out->minute = m1*10 + m2;
                            out->second = s1*10 + s2;
                        } else {
                            goto error;
                        }
                    } else {
                        out->has_time = 1;
                        out->hour = h1*10 + h2;
                        out->minute = m1*10 + m2;
                        out->second = 0;
                    }
                } else {
                    goto error;
                }
            } else {
                /* Could be compact HHMMSS or HHMM */
                if (*p >= '0' && *p <= '9' && p[1] >= '0' && p[1] <= '9') {
                    int m1 = p[0]-'0', m2 = p[1]-'0';
                    p += 2;
                    if (*p >= '0' && *p <= '9' && p[1] >= '0' && p[1] <= '9') {
                        int s1 = p[0]-'0', s2 = p[1]-'0';
                        p += 2;
                        has_explicit_seconds = 1;
                        out->has_time = 1;
                        out->hour = h1*10 + h2;
                        out->minute = m1*10 + m2;
                        out->second = s1*10 + s2;
                    } else {
                        out->has_time = 1;
                        out->hour = h1*10 + h2;
                        out->minute = m1*10 + m2;
                        out->second = 0;
                    }
                } else {
                    out->has_time = 1;
                    out->hour = h1*10 + h2;
                    out->minute = 0;
                    out->second = 0;
                }
            }

            if (out->has_time) {
                if (out->hour < 0 || out->hour > 23 || out->minute < 0 || out->minute > 59 || out->second < 0 || out->second > 60)
                    goto error;
                if (out->second == 60)
                    out->second = 59;
            }

            /* Fractional seconds for bare time */
            if (*p == '.' || *p == ',') {
                if (!has_explicit_seconds)
                    goto error;
                int frac[9] = {0};
                int frac_len = 0;
                p++;
                while (*p >= '0' && *p <= '9' && frac_len < 9) {
                    frac[frac_len++] = *p - '0';
                    p++;
                }
                if (frac_len == 0)
                    goto error;
                for (i = frac_len; i < 9; i++) frac[i] = 0;
                out->millisecond = frac[0]*100 + frac[1]*10 + frac[2];
                out->microsecond = frac[3]*100 + frac[4]*10 + frac[5];
                out->nanosecond = frac[6]*100 + frac[7]*10 + frac[8];
            }
        } else if (p[0] >= '0' && p[0] <= '9') {
            /* Single digit hour */
            int h1 = p[0]-'0';
            p += 1;
            if (*p == ':') {
                p++;
                if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                    int m1 = p[0]-'0', m2 = p[1]-'0';
                    p += 2;
                    out->has_time = 1;
                    out->hour = h1;
                    out->minute = m1*10 + m2;
                    out->second = 0;
                } else {
                    goto error;
                }
            } else {
                out->has_time = 1;
                out->hour = h1;
                out->minute = 0;
                out->second = 0;
            }

            if (out->has_time) {
                if (out->hour < 0 || out->hour > 23 || out->minute < 0 || out->minute > 59 || out->second < 0 || out->second > 60)
                    goto error;
                if (out->second == 60)
                    out->second = 59;
            }
        } else {
            /* Not a time string either */
        }
    }

    /* Timezone offset or Z */
    if (*p == 'Z' || *p == 'z') {
        out->has_utc = 1;
        p++;
    } else if (*p == '+' || *p == '-') {
        int tz_sign = (*p == '-') ? -1 : 1;
        p++;
        if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
            int th = (p[0]-'0')*10 + (p[1]-'0');
            p += 2;
            if (*p == ':') {
                p++;
                if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                    int tm = (p[0]-'0')*10 + (p[1]-'0');
                    p += 2;
                    if (*p == ':') {
                        p++;
                        if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                            int ts = (p[0]-'0')*10 + (p[1]-'0');
                            p += 2;
                            if (th > 23 || tm > 59 || ts > 59)
                                goto error;
                            out->has_tz_offset = 1;
                            out->tz_offset_has_seconds = 1;
                            out->tz_offset_seconds = tz_sign * (th * 3600 + tm * 60 + ts);
                        } else {
                            goto error;
                        }
                    } else {
                        if (th > 23 || tm > 59)
                            goto error;
                        out->has_tz_offset = 1;
                        out->tz_offset_seconds = tz_sign * (th * 3600 + tm * 60);
                    }
                } else {
                    goto error;
                }
            } else if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                /* HHMM compact offset */
                int tm = (p[0]-'0')*10 + (p[1]-'0');
                p += 2;
                if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
                    /* HHMMSS */
                    int ts = (p[0]-'0')*10 + (p[1]-'0');
                    p += 2;
                    if (th > 23 || tm > 59 || ts > 59)
                        goto error;
                    out->has_tz_offset = 1;
                    out->tz_offset_has_seconds = 1;
                    out->tz_offset_seconds = tz_sign * (th * 3600 + tm * 60 + ts);
                } else {
                    if (th > 23 || tm > 59)
                        goto error;
                    out->has_tz_offset = 1;
                    out->tz_offset_seconds = tz_sign * (th * 3600 + tm * 60);
                }
            } else {
                /* HH offset only */
                if (th > 23)
                    goto error;
                out->has_tz_offset = 1;
                out->tz_offset_seconds = tz_sign * (th * 3600);
            }
        } else {
            goto error;
        }

        /* Fractional timezone offsets */
        if (*p == '.' || *p == ',') {
            int frac_count = 0;
            int frac_val = 0;
            p++;
            while (*p >= '0' && *p <= '9' && frac_count < 9) {
                frac_val = frac_val * 10 + (*p - '0');
                frac_count++;
                p++;
            }
            if (frac_count == 0)
                goto error;
            for (i = frac_count; i < 9; i++)
                frac_val *= 10;
            out->tz_offset_nanoseconds = tz_sign * frac_val;
        }
    }

    /* Bracket annotations */
    {
        int tz_annotation_count = 0;
        int cal_annotation_count = 0;
        int has_critical_calendar = 0;

        while (*p == '[') {
            int is_critical = 0;
            const char *key_start;
            int key_len;
            const char *val_start;
            int val_len;

            p++;
            if (*p == '!') {
                is_critical = 1;
                p++;
            }
            key_start = p;
            while (*p && *p != '=' && *p != ']') p++;
            key_len = (int)(p - key_start);
            if (*p == '=') {
                p++;
                val_start = p;
                while (*p && *p != ']') p++;
                if (*p != ']')
                    goto error;
                val_len = (int)(p - val_start);

                /* Validate key characters */
                {
                    int valid = 1;
                    for (i = 0; i < key_len; i++) {
                        char c = key_start[i];
                        if (i == 0) {
                            if (!((c >= 'a' && c <= 'z') || c == '_'))
                                valid = 0;
                        } else {
                            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
                                valid = 0;
                        }
                    }
                    if (!valid)
                        goto error;
                }

                if (key_len == 4 && memcmp(key_start, "u-ca", 4) == 0) {
                    cal_annotation_count++;
                    if (is_critical)
                        has_critical_calendar = 1;
                    if (!out->has_calendar && val_len > 0 && val_len < 64) {
                        char cal_buf[64];
                        int j;
                        memcpy(cal_buf, val_start, (size_t)val_len);
                        cal_buf[val_len] = '\0';
                        for (j = 0; j < val_len; j++) {
                            char c = cal_buf[j];
                            cal_buf[j] = (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
                        }
                        out->calendar = ti_calendar_string_to_enum_with_aliases(cal_buf);
                        out->has_calendar = 1;
                        out->calendar_critical = is_critical;
                    }
                } else {
                    if (is_critical)
                        goto error;
                }
            } else if (*p == ']') {
                tz_annotation_count++;
                if (!out->has_tz_annotation && key_len > 0 && key_len < 64) {
                    /* Reject sub-minute offsets in timezone annotations */
                    if (key_start[0] == '+' || key_start[0] == '-') {
                        int colon_count = 0;
                        int j;
                        for (j = 0; j < key_len; j++) {
                            if (key_start[j] == ':')
                                colon_count++;
                        }
                        if (colon_count >= 2 || (colon_count == 0 && key_len > 5))
                            goto error;
                    }
                    memcpy(out->tz_annotation, key_start, (size_t)key_len);
                    out->tz_annotation[key_len] = '\0';
                    out->has_tz_annotation = 1;
                }
            } else {
                goto error;
            }
            p++;
        }
        if (cal_annotation_count > 1 && has_critical_calendar)
            goto error;
        if (tz_annotation_count > 1)
            goto error;
    }

    /* Trailing junk */
    if (*p != '\0')
        goto error;

    if (flags & TI_PARSE_PLAIN_TIME) {
        if (!out->has_time)
            goto error;
        if (out->has_utc)
            goto error;
        {
            const char *q = str;
            int has_t = 0;
            while (*q && *q != '[') {
                if (*q == 'T' || *q == 't') {
                    has_t = 1;
                    break;
                }
                q++;
            }
            if (!has_t) {
                const char *q2 = str;
                while (*q2 && ((*q2 >= '0' && *q2 <= '9') || *q2 == '-')) q2++;
                int core_len = (int)(q2 - str);
                if (core_len == 4) {
                    int mm = (str[0]-'0')*10 + (str[1]-'0');
                    int dd = (str[2]-'0')*10 + (str[3]-'0');
                    int max_day[13] = {0,31,29,31,30,31,30,31,31,30,31,30,31};
                    if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= max_day[mm])
                        goto error;
                } else if (core_len == 6) {
                    int mm = (str[4]-'0')*10 + (str[5]-'0');
                    if (mm >= 1 && mm <= 12)
                        goto error;
                } else if (core_len == 5 && str[2] == '-') {
                    int mm = (str[0]-'0')*10 + (str[1]-'0');
                    int dd = (str[3]-'0')*10 + (str[4]-'0');
                    int max_day[13] = {0,31,29,31,30,31,30,31,31,30,31,30,31};
                    if (mm >= 1 && mm <= 12 && dd >= 1 && dd <= max_day[mm])
                        goto error;
                } else if (core_len == 7 && str[4] == '-') {
                    int mm = (str[5]-'0')*10 + (str[6]-'0');
                    if (mm >= 1 && mm <= 12)
                        goto error;
                }
            }
        }
    }

    if (flags & TI_PARSE_PLAIN_MONTHDAY) {
        if (!out->has_monthday && !out->has_date)
            goto error;
        if (out->has_monthday) {
            if (out->month < 1 || out->month > 12 || out->day < 1 || out->day > 31)
                goto error;
        }
        /* Time is only allowed with a full date (YYYY-MM-DD) */
        if (out->has_time && !out->has_date)
            goto error;
        /* UTC designator is never allowed for PlainMonthDay */
        if (out->has_utc)
            goto error;
        /* Numeric offsets are only allowed with time */
        if (out->has_tz_offset && !out->has_time)
            goto error;
    }

    return 0;

error:
    return -1;
}

/* Get a string option from an options object and validate it against a list of allowed values.
   Returns the index of the matched value, or default_index if the property is undefined.
   Returns -1 on error (exception thrown).
   Uses JS_ToCStringLen for exact length comparison to handle embedded null bytes. */
int ti_get_string_option(JSContext *ctx, JSValueConst options, const char *prop,
                          const char **valid_values, int num_values, int default_index)
{
    JSValue val;
    size_t slen;
    const char *str;
    int i;

    if (JS_IsUndefined(options))
        return default_index;

    val = JS_GetPropertyStr(ctx, options, prop);

    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val))
        return default_index;

    str = JS_ToCStringLen(ctx, &slen, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return -1;

    for (i = 0; i < num_values; i++) {
        size_t vlen = strlen(valid_values[i]);
        if (slen == vlen && memcmp(str, valid_values[i], vlen) == 0) {
            JS_FreeCString(ctx, str);
            return i;
        }
    }

    JS_ThrowRangeError(ctx, "invalid %s", prop);
    JS_FreeCString(ctx, str);
    return -1;
}

/* Get the "overflow" option from an options object.
   Returns 1 for "constrain", 0 for "reject".
   Returns -1 on error (exception thrown).
   Default is "constrain" (returns 1). */
int ti_get_overflow_option(JSContext *ctx, JSValueConst options, int *overflow_constrain)
{
    static const char *overflow_values[] = { "reject", "constrain" };
    int idx = ti_get_string_option(ctx, options, "overflow", overflow_values, 2, 1);
    if (idx < 0)
        return -1;
    *overflow_constrain = idx; /* 0 = reject, 1 = constrain */
    return 0;
}

TIUnit ti_string_to_unit(const char *str, size_t len)
{
    static const char *unit_strings[] = {
        "year", "month", "week", "day", "hour", "minute", "second",
        "millisecond", "microsecond", "nanosecond"
    };
    int i;
    for (i = 0; i < (int)(sizeof(unit_strings) / sizeof(unit_strings[0])); i++) {
        size_t unit_len = strlen(unit_strings[i]);
        if (len == unit_len && memcmp(str, unit_strings[i], unit_len) == 0)
            return (TIUnit)(TI_UNIT_YEAR + i);
        if (len == unit_len + 1 && str[len - 1] == 's' && memcmp(str, unit_strings[i], unit_len) == 0)
            return (TIUnit)(TI_UNIT_YEAR + i);
    }
    return TI_UNIT_INVALID;
}

/* Parse a unit option (smallestUnit, largestUnit, unit) from an options object.
   Returns 0 on success, -1 on error (exception thrown).
   flags: bitwise OR of TI_UNIT_OPT_ALLOW_UNDEFINED and/or TI_UNIT_OPT_ALLOW_AUTO.
   If ALLOW_UNDEFINED is set and the property is undefined, *out = default_unit.
   If ALLOW_AUTO is set and the value is "auto", *out = TI_UNIT_AUTO.
   Otherwise throws RangeError for undefined or invalid strings. */
int ti_parse_unit_option(JSContext *ctx, JSValueConst options, const char *prop,
                          TIUnit *out, int flags, TIUnit default_unit)
{
    JSValue val = JS_GetPropertyStr(ctx, options, prop);
    size_t len;
    const char *str;
    TIUnit unit;

    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val)) {
        JS_FreeValue(ctx, val);
        if (flags & TI_UNIT_OPT_ALLOW_UNDEFINED) {
            *out = default_unit;
            return 0;
        }
        JS_ThrowRangeError(ctx, "%s is required", prop);
        return -1;
    }

    str = JS_ToCStringLen(ctx, &len, val);
    JS_FreeValue(ctx, val);
    if (!str)
        return -1;

    if ((flags & TI_UNIT_OPT_ALLOW_AUTO) && len == 4 && memcmp(str, "auto", 4) == 0) {
        JS_FreeCString(ctx, str);
        *out = TI_UNIT_AUTO;
        return 0;
    }

    unit = ti_string_to_unit(str, len);
    JS_FreeCString(ctx, str);
    if (unit == TI_UNIT_INVALID) {
        JS_ThrowRangeError(ctx, "invalid %s", prop);
        return -1;
    }
    *out = unit;
    return 0;
}

int64_t ti_unit_to_ns(TIUnit unit)
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

/* Read the roundingIncrement option from an options object.
   Returns 0 on success, -1 on error (exception thrown).
   If the property is undefined, *out is left unchanged. */
int ti_get_rounding_increment_option(JSContext *ctx, JSValueConst options, int64_t *out)
{
    JSValue val = JS_GetPropertyStr(ctx, options, "roundingIncrement");
    double d;
    int64_t ri;

    if (JS_IsException(val))
        return -1;
    if (JS_IsUndefined(val)) {
        JS_FreeValue(ctx, val);
        return 0;
    }
    if (JS_ToFloat64(ctx, &d, val) < 0) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    JS_FreeValue(ctx, val);
    ri = (int64_t)d;
    if (!isfinite(d) || ri < 1 || ri > 1000000000LL) {
        JS_ThrowRangeError(ctx, "invalid roundingIncrement");
        return -1;
    }
    *out = ri;
    return 0;
}

/* Read the roundingMode option from an options object.
   Returns 0 on success, -1 on error (exception thrown).
   If default_mode is -1, uses ROUND_HALF_EXPAND as default. */
int ti_get_rounding_mode_option_default(JSContext *ctx, JSValueConst options, TIRoundingMode *out, int default_mode)
{
    JSValue val = JS_GetPropertyStr(ctx, options, "roundingMode");
    int ret;

    if (JS_IsException(val))
        return -1;

    if (JS_IsUndefined(val)) {
        *out = (default_mode < 0) ? ROUND_HALF_EXPAND : default_mode;
        JS_FreeValue(ctx, val);
        return 0;
    }

    ret = ti_parse_rounding_mode(ctx, val, out);
    JS_FreeValue(ctx, val);
    return ret;
}

int ti_get_rounding_mode_option(JSContext *ctx, JSValueConst options, TIRoundingMode *out)
{
    return ti_get_rounding_mode_option_default(ctx, options, out, -1);
}

/* Read the "calendarName" option from an options object.
   Returns 0 on success, -1 on error (exception thrown).
   *out is set to: 0=auto, 1=always, 2=critical, 3=never.
   Leaves *out unchanged if property is undefined. */
int ti_get_calendar_name_option(JSContext *ctx, JSValueConst options, int *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, "calendarName");
    size_t len;
    const char *str;
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        JS_FreeValue(ctx, v);
        return 0;
    }
    str = JS_ToCStringLen(ctx, &len, v);
    JS_FreeValue(ctx, v);
    if (!str)
        return -1;
    if (len == 6 && memcmp(str, "always", 6) == 0)
        *out = 1;
    else if (len == 8 && memcmp(str, "critical", 8) == 0)
        *out = 2;
    else if (len == 5 && memcmp(str, "never", 5) == 0)
        *out = 3;
    else if (len == 4 && memcmp(str, "auto", 4) == 0)
        *out = 0;
    else {
        JS_FreeCString(ctx, str);
        JS_ThrowRangeError(ctx, "invalid calendarName");
        return -1;
    }
    JS_FreeCString(ctx, str);
    return 0;
}

/* GetTemporalFractionalSecondDigitsOption as specified in Temporal spec.
   Returns 0 on success, -1 on error (exception thrown).
   *out is set to -1 for "auto" or 0-9 for a digit count.
   Leaves *out unchanged if property is undefined. */
int ti_get_fractional_second_digits_option(JSContext *ctx, JSValueConst options, int *out)
{
    JSValue v = JS_GetPropertyStr(ctx, options, "fractionalSecondDigits");
    if (JS_IsException(v))
        return -1;
    if (JS_IsUndefined(v)) {
        *out = -1;
        return 0;
    }
    if (JS_IsNumber(v)) {
        double d;
        if (JS_ToFloat64(ctx, &d, v) < 0)
            goto fail;
        JS_FreeValue(ctx, v);
        if (isnan(d) || isinf(d)) {
            JS_ThrowRangeError(ctx, "invalid fractionalSecondDigits");
            return -1;
        }
        int digitCount = (int)floor(d);
        if (digitCount < 0 || digitCount > 9) {
            JS_ThrowRangeError(ctx, "invalid fractionalSecondDigits");
            return -1;
        }
        *out = digitCount;
        return 0;
    } else {
        JSValue str = JS_ToString(ctx, v);
        JS_FreeValue(ctx, v);
        if (JS_IsException(str))
            return -1;
        size_t len;
        const char *p = JS_ToCStringLen(ctx, &len, str);
        if (!p) {
            JS_FreeValue(ctx, str);
            return -1;
        }
        int ok = (len == 4 && memcmp(p, "auto", 4) == 0);
        JS_FreeCString(ctx, p);
        JS_FreeValue(ctx, str);
        if (!ok) {
            JS_ThrowRangeError(ctx, "invalid fractionalSecondDigits");
            return -1;
        }
        *out = -1;
        return 0;
    }
fail:
    JS_FreeValue(ctx, v);
    return -1;
}

/* NegateRoundingMode as specified in GetDifferenceSettings step 6.
   For since operations, the rounding mode is inverted. */
TIRoundingMode ti_negate_rounding_mode(TIRoundingMode mode)
{
    switch (mode) {
    case ROUND_CEIL: return ROUND_FLOOR;
    case ROUND_FLOOR: return ROUND_CEIL;
    case ROUND_HALF_CEIL: return ROUND_HALF_FLOOR;
    case ROUND_HALF_FLOOR: return ROUND_HALF_CEIL;
    default: return mode;
    }
}

/* ------------------------------------------------------------------ */
/* PrepareCalendarFields-like implementation                          */
/* ------------------------------------------------------------------ */

static void ti_prepared_fields_init(TIPreparedFields *fields)
{
    memset(fields, 0, sizeof(*fields));
    fields->calendar = CAL_ISO8601;
}

/* Helper to convert a JSValue to integer with truncation.
   Returns 0 on success, -1 on exception. */
static int ti_to_integer_with_truncation(JSContext *ctx, JSValue v, int64_t *out)
{
    double d;
    if (JS_ToFloat64(ctx, &d, v) < 0)
        return -1;
    if (!isfinite(d)) {
        JS_ThrowRangeError(ctx, "value must be finite");
        return -1;
    }
    *out = (int64_t)d;
    return 0;
}

/* Helper to convert a JSValue to positive integer with truncation.
   Returns 0 on success, -1 on exception. */
static int ti_to_positive_integer_with_truncation(JSContext *ctx, JSValue v, int *out)
{
    double d;
    if (JS_ToFloat64(ctx, &d, v) < 0)
        return -1;
    if (!isfinite(d) || d <= 0) {
        JS_ThrowRangeError(ctx, "value must be a finite positive number");
        return -1;
    }
    *out = (int)d;
    return 0;
}

/* Resolve a calendar JSValue to a CalendarType.
   Returns 0 on success, -1 on exception. */
int ti_resolve_calendar_value(JSContext *ctx, JSValue cal_v, CalendarType *out)
{
    if (JS_IsString(cal_v)) {
        int cal_res;
        size_t cal_len;
        const char *cal_raw = JS_ToCStringLen(ctx, &cal_len, cal_v);
        if (!cal_raw)
            return -1;
        int looks_temporal = 0;
        if (cal_len > 0) {
            char first = cal_raw[0];
            if (isdigit((unsigned char)first) || first == '+' || first == '-')
                looks_temporal = 1;
            else if ((first == 'T' || first == 't') && cal_len > 1 && isdigit((unsigned char)cal_raw[1]))
                looks_temporal = 1;
        }
        if (looks_temporal) {
            cal_res = ti_parse_temporal_calendar_string(ctx, cal_v, out);
        } else {
            cal_res = ti_canonicalize_calendar(ctx, cal_v, out);
        }
        JS_FreeCString(ctx, cal_raw);
        if (cal_res == -2) {
            JS_ThrowTypeError(ctx, "invalid calendar");
            return -1;
        }
        if (cal_res < 0) {
            JS_ThrowRangeError(ctx, "invalid calendar ID");
            return -1;
        }
    } else if (JS_IsObject(cal_v)) {
        JSPlainDateData *pd = JS_GetOpaque(cal_v, js_plaindate_class_id);
        JSPlainDateTimeData *pdt = JS_GetOpaque(cal_v, js_plaindatetime_class_id);
        JSPlainMonthDayData *pmd = JS_GetOpaque(cal_v, js_plainmonthday_class_id);
        JSPlainYearMonthData *pym = JS_GetOpaque(cal_v, js_plainyearmonth_class_id);
        JSZonedDateTimeData *zdt = js_zoneddatetime_class_id ? JS_GetOpaque(cal_v, js_zoneddatetime_class_id) : NULL;
        if (pd) *out = pd->calendar;
        else if (pdt) *out = pdt->calendar;
        else if (pmd) *out = pmd->calendar;
        else if (pym) *out = pym->calendar;
        else if (zdt) *out = zdt->calendar;
        else {
            JS_ThrowTypeError(ctx, "invalid calendar");
            return -1;
        }
    } else {
        JS_ThrowTypeError(ctx, "invalid calendar");
        return -1;
    }
    return 0;
}

/* Read the calendar property from an object and resolve it.
   Returns 0 on success, -1 on exception. Default is CAL_ISO8601. */
int ti_resolve_calendar_from_object(JSContext *ctx, JSValueConst obj, CalendarType *out)
{
    JSValue cal_v = JS_GetPropertyStr(ctx, obj, "calendar");
    if (JS_IsException(cal_v))
        return -1;
    if (JS_IsUndefined(cal_v)) {
        *out = CAL_ISO8601;
        return 0;
    }
    int ret = ti_resolve_calendar_value(ctx, cal_v, out);
    JS_FreeValue(ctx, cal_v);
    return ret;
}

/* Check if obj is a partial Temporal object suitable for .with() methods.
   Returns 0 on success, -1 on exception (TypeError already thrown).
   Rejects non-objects, Temporal internal slot objects, and objects with
   calendar or timeZone properties. */
int ti_is_partial_temporal_object(JSContext *ctx, JSValueConst obj)
{
    JSValue v;
    if (!JS_IsObject(obj))
        return JS_ThrowTypeError(ctx, "expected an object"), -1;
    if (JS_GetOpaque(obj, js_plaindate_class_id) ||
        JS_GetOpaque(obj, js_plaintime_class_id) ||
        JS_GetOpaque(obj, js_plaindatetime_class_id) ||
        JS_GetOpaque(obj, js_plainmonthday_class_id) ||
        JS_GetOpaque(obj, js_plainyearmonth_class_id) ||
        JS_GetOpaque(obj, js_zoneddatetime_class_id))
        return JS_ThrowTypeError(ctx, "object must not be a Temporal instance"), -1;
    v = JS_GetPropertyStr(ctx, obj, "calendar");
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, v);
        return -1;
    }
    if (!JS_IsUndefined(v)) {
        JS_FreeValue(ctx, v);
        return JS_ThrowTypeError(ctx, "object must not have a calendar property"), -1;
    }
    JS_FreeValue(ctx, v);
    v = JS_GetPropertyStr(ctx, obj, "timeZone");
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, v);
        return -1;
    }
    if (!JS_IsUndefined(v)) {
        JS_FreeValue(ctx, v);
        return JS_ThrowTypeError(ctx, "object must not have a timeZone property"), -1;
    }
    JS_FreeValue(ctx, v);
    return 0;
}

/* Month code formatting helper */
JSValue ti_get_month_code(JSContext *ctx, CalendarType cal, int year, int month)
{
    char buf[8];
    int mc, has_l;
    ti_calendar_month_to_month_code(cal, year, month, &mc, &has_l);
    snprintf(buf, sizeof(buf), "M%02d%s", mc, has_l ? "L" : "");
    return JS_NewString(ctx, buf);
}

/* Read fields from a property bag according to field_mask.
   required_mask: fields that MUST be present (throw TypeError if missing).
   partial: if true, at least one field must be present (like ~partial~ in spec).
   calendar: the resolved calendar type (used to determine if era/eraYear should be read).
   Returns 0 on success, -1 on exception. */
int ti_prepare_calendar_fields(JSContext *ctx, JSValueConst obj,
                                uint32_t field_mask, uint32_t required_mask,
                                int partial, TIPreparedFields *out,
                                CalendarType calendar)
{
    JSValue v;
    ti_prepared_fields_init(out);
    out->calendar = calendar;

    if (!JS_IsObject(obj)) {
        JS_ThrowTypeError(ctx, "expected an object");
        return -1;
    }

    if (calendar != CAL_ISO8601)
        field_mask |= TI_FIELD_ERA | TI_FIELD_ERA_YEAR;

    /* Read fields in alphabetical order */

    /* day */
    if (field_mask & TI_FIELD_DAY) {
        v = JS_GetPropertyStr(ctx, obj, "day");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            if (ti_to_positive_integer_with_truncation(ctx, v, &out->day) < 0) {
                goto fail;
            }
            out->present_mask |= TI_FIELD_DAY;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* era */
    if (field_mask & TI_FIELD_ERA) {
        v = JS_GetPropertyStr(ctx, obj, "era");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            const char *era_str = JS_ToCString(ctx, v);
            if (!era_str)
                goto fail;
            out->era = ti_string_to_era(era_str);
            JS_FreeCString(ctx, era_str);
            out->present_mask |= TI_FIELD_ERA;
        }
    }

    /* eraYear */
    if (field_mask & TI_FIELD_ERA_YEAR) {
        v = JS_GetPropertyStr(ctx, obj, "eraYear");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            if (ti_to_integer_with_truncation(ctx, v, &out->era_year) < 0) {
                goto fail;
            }
            out->present_mask |= TI_FIELD_ERA_YEAR;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* hour */
    if (field_mask & TI_FIELD_HOUR) {
        v = JS_GetPropertyStr(ctx, obj, "hour");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->hour = (int)tmp;
            out->present_mask |= TI_FIELD_HOUR;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* microsecond */
    if (field_mask & TI_FIELD_MICROSECOND) {
        v = JS_GetPropertyStr(ctx, obj, "microsecond");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->microsecond = (int)tmp;
            out->present_mask |= TI_FIELD_MICROSECOND;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* millisecond */
    if (field_mask & TI_FIELD_MILLISECOND) {
        v = JS_GetPropertyStr(ctx, obj, "millisecond");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->millisecond = (int)tmp;
            out->present_mask |= TI_FIELD_MILLISECOND;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* minute */
    if (field_mask & TI_FIELD_MINUTE) {
        v = JS_GetPropertyStr(ctx, obj, "minute");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->minute = (int)tmp;
            out->present_mask |= TI_FIELD_MINUTE;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* month */
    if (field_mask & TI_FIELD_MONTH) {
        v = JS_GetPropertyStr(ctx, obj, "month");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            if (ti_to_positive_integer_with_truncation(ctx, v, &out->month) < 0) {
                goto fail;
            }
            out->present_mask |= TI_FIELD_MONTH;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* monthCode */
    if (field_mask & TI_FIELD_MONTH_CODE) {
        v = JS_GetPropertyStr(ctx, obj, "monthCode");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            JSValue prim = JS_ToPrimitive(ctx, v, JS_HINT_STRING);
            JS_FreeValue(ctx, v);
            v = JS_UNDEFINED;
            if (JS_IsException(prim)) {
                JS_FreeValue(ctx, prim);
                goto fail;
            }
            if (!JS_IsString(prim)) {
                JS_FreeValue(ctx, prim);
                JS_ThrowTypeError(ctx, "monthCode must be a string");
                goto fail;
            }
            {
                const char *mc = JS_ToCString(ctx, prim);
                if (!mc) {
                    JS_FreeValue(ctx, prim);
                    goto fail;
                }
                /* Validate monthCode format: M## or M##L */
                if (mc[0] == 'M' && isdigit((unsigned char)mc[1]) && isdigit((unsigned char)mc[2])) {
                    if (mc[3] == '\0') {
                        out->month_from_code = atoi(mc + 1);
                        out->month_code_has_l = 0;
                    } else if (mc[3] == 'L' && mc[4] == '\0') {
                        out->month_from_code = atoi(mc + 1);
                        out->month_code_has_l = 1;
                    } else {
                        JS_FreeCString(ctx, mc);
                        JS_FreeValue(ctx, prim);
                        JS_ThrowRangeError(ctx, "monthCode is not well-formed");
                        goto fail;
                    }
                } else {
                    JS_FreeCString(ctx, mc);
                    JS_FreeValue(ctx, prim);
                    JS_ThrowRangeError(ctx, "monthCode is not well-formed");
                    goto fail;
                }
                JS_FreeCString(ctx, mc);
            }
            JS_FreeValue(ctx, prim);
            out->present_mask |= TI_FIELD_MONTH_CODE;
        }
    }

    /* nanosecond */
    if (field_mask & TI_FIELD_NANOSECOND) {
        v = JS_GetPropertyStr(ctx, obj, "nanosecond");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->nanosecond = (int)tmp;
            out->present_mask |= TI_FIELD_NANOSECOND;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* offset */
    if (field_mask & TI_FIELD_OFFSET) {
        v = JS_GetPropertyStr(ctx, obj, "offset");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            JSValue prim = JS_ToPrimitive(ctx, v, JS_HINT_STRING);
            JS_FreeValue(ctx, v);
            v = JS_UNDEFINED;
            if (JS_IsException(prim)) {
                JS_FreeValue(ctx, prim);
                goto fail;
            }
            if (!JS_IsString(prim)) {
                JS_FreeValue(ctx, prim);
                JS_ThrowTypeError(ctx, "offset must be a string");
                goto fail;
            }
            {
                const char *str = JS_ToCString(ctx, prim);
                size_t len;
                int sign;
                if (!str) {
                    JS_FreeValue(ctx, prim);
                    goto fail;
                }
                len = strlen(str);
                if (len == 1 && (str[0] == 'Z' || str[0] == 'z')) {
                    out->offset_ns = 0;
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, prim);
                    out->present_mask |= TI_FIELD_OFFSET;
                    goto offset_done;
                }
                if (!ti_is_valid_plain_offset_string(str)) {
                    JS_FreeCString(ctx, str);
                    JS_FreeValue(ctx, prim);
                    JS_ThrowRangeError(ctx, "invalid offset string");
                    goto fail;
                }
                sign = (str[0] == '-') ? -1 : 1;
                if (len == 3) {
                    int h = (str[1] - '0') * 10 + (str[2] - '0');
                    out->offset_ns = sign * h * 3600LL * 1000000000LL;
                } else if (len == 5) {
                    int h = (str[1] - '0') * 10 + (str[2] - '0');
                    int m = (str[3] - '0') * 10 + (str[4] - '0');
                    out->offset_ns = sign * (h * 3600LL + m * 60LL) * 1000000000LL;
                } else if (len == 6) {
                    int h = (str[1] - '0') * 10 + (str[2] - '0');
                    int m = (str[4] - '0') * 10 + (str[5] - '0');
                    out->offset_ns = sign * (h * 3600LL + m * 60LL) * 1000000000LL;
                } else if (len >= 9) {
                    int h = (str[1] - '0') * 10 + (str[2] - '0');
                    int m = (str[4] - '0') * 10 + (str[5] - '0');
                    int s = (str[7] - '0') * 10 + (str[8] - '0');
                    int frac = 0, i;
                    out->offset_ns = sign * ((h * 3600LL + m * 60LL + s) * 1000000000LL);
                    if (len > 9) {
                        int frac_count = 0;
                        for (i = 10; i < (int)len; i++) {
                            frac = frac * 10 + (str[i] - '0');
                            frac_count++;
                        }
                        for (i = frac_count; i < 9; i++)
                            frac *= 10;
                        out->offset_ns += sign * frac;
                    }
                }
                JS_FreeCString(ctx, str);
            }
            JS_FreeValue(ctx, prim);
            out->present_mask |= TI_FIELD_OFFSET;
        }
    }
offset_done:

    /* second */
    if (field_mask & TI_FIELD_SECOND) {
        v = JS_GetPropertyStr(ctx, obj, "second");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            int64_t tmp;
            if (ti_to_integer_with_truncation(ctx, v, &tmp) < 0) {
                goto fail;
            }
            out->second = (int)tmp;
            out->present_mask |= TI_FIELD_SECOND;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* timeZone */
    if (field_mask & TI_FIELD_TIMEZONE) {
        v = JS_GetPropertyStr(ctx, obj, "timeZone");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            if (!JS_IsString(v) && !JS_IsObject(v)) {
                JS_ThrowTypeError(ctx, "timeZone must be a string or object");
                goto fail;
            }
            if (JS_IsString(v)) {
                const char *tz = JS_ToCString(ctx, v);
                int64_t tz_offset_ns;
                if (!tz || *tz == '\0') {
                    JS_FreeCString(ctx, tz);
                    JS_ThrowRangeError(ctx, "invalid timeZone");
                    goto fail;
                }
                if (ti_is_minus_zero_year(tz)) {
                    JS_FreeCString(ctx, tz);
                    JS_ThrowRangeError(ctx, "minus zero year is not allowed");
                    goto fail;
                }
                if (ti_parse_temporal_timezone_string(tz, &tz_offset_ns) < 0) {
                    JS_FreeCString(ctx, tz);
                    JS_ThrowRangeError(ctx, "invalid timeZone");
                    goto fail;
                }
                pstrcpy(out->time_zone, sizeof(out->time_zone), tz);
                JS_FreeCString(ctx, tz);
            } else if (JS_IsObject(v)) {
                JSZonedDateTimeData *zdt = JS_GetOpaque(v, js_zoneddatetime_class_id);
                if (!zdt) {
                    JS_ThrowTypeError(ctx, "timeZone must be a string or object");
                    goto fail;
                }
                pstrcpy(out->time_zone, sizeof(out->time_zone), zdt->timeZone);
            }
            JS_FreeValue(ctx, v);
            v = JS_UNDEFINED;
            out->present_mask |= TI_FIELD_TIMEZONE;
        }
    }

    /* year */
    if (field_mask & TI_FIELD_YEAR) {
        v = JS_GetPropertyStr(ctx, obj, "year");
        if (JS_IsException(v))
            goto fail;
        if (!JS_IsUndefined(v)) {
            if (ti_is_minus_zero(v)) {
                JS_ThrowRangeError(ctx, "minus zero year is not allowed");
                goto fail;
            }
            if (ti_to_integer_with_truncation(ctx, v, &out->year) < 0) {
                goto fail;
            }
            out->present_mask |= TI_FIELD_YEAR;
        }
        JS_FreeValue(ctx, v);
        v = JS_UNDEFINED;
    }

    /* Check required fields */
    {
        static const char *field_names[] = {
            "year", "month", "monthCode", "day", "hour", "minute", "second",
            "millisecond", "microsecond", "nanosecond", "offset", "timeZone",
            "calendar", "era", "eraYear"
        };
        uint32_t missing = required_mask & ~out->present_mask;
        if (missing) {
            int i = __builtin_ctz(missing);
            JS_ThrowTypeError(ctx, "missing %s", field_names[i]);
            goto fail;
        }
    }

    /* Check partial requirement */
    if (partial && !out->present_mask) {
        JS_ThrowTypeError(ctx, "object must contain at least one property");
        goto fail;
    }

    return 0;

fail:
    JS_FreeValue(ctx, v);
    return -1;
}

/* Create a PlainDate from prepared fields */
/* Resolve year, month, day from prepared fields.
   Sets *year, *month, *day. Returns 0 on success, -1 on exception. */
int ti_resolve_date_from_fields(JSContext *ctx, TIPreparedFields *fields, int overflow_constrain,
                                 int *year, int *month, int *day, BOOL require_day)
{
    int has_year = (fields->present_mask & TI_FIELD_YEAR) != 0;
    int has_era = (fields->present_mask & TI_FIELD_ERA) != 0;
    int has_eraYear = (fields->present_mask & TI_FIELD_ERA_YEAR) != 0;

    *year = (int)fields->year;

    if (fields->calendar != CAL_ISO8601) {
        if (has_era || has_eraYear) {
            if (!has_era || !has_eraYear) {
                JS_ThrowTypeError(ctx, "era and eraYear must both be present");
                return -1;
            }
            if (!ti_calendar_requires_era(fields->calendar)) {
                if (!has_year) {
                    JS_ThrowTypeError(ctx, "era and eraYear cannot replace year for calendar not using eras");
                    return -1;
                }
                /* year is present: ignore era/eraYear */
            } else {
                int cal_year_from_era;
                if (ti_calendar_era_to_calendar_year(fields->calendar, fields->era, (int)fields->era_year, &cal_year_from_era) < 0) {
                    JS_ThrowRangeError(ctx, "invalid era");
                    return -1;
                }
                if (has_year && *year != cal_year_from_era) {
                    JS_ThrowRangeError(ctx, "year and era/eraYear must agree");
                    return -1;
                }
                *year = cal_year_from_era;
                has_year = 1;
            }
        }
    }

    if (!has_year) {
        JS_ThrowTypeError(ctx, "missing required property");
        return -1;
    }
    if (require_day && !(fields->present_mask & TI_FIELD_DAY)) {
        JS_ThrowTypeError(ctx, "missing required property");
        return -1;
    }
    if (!(fields->present_mask & (TI_FIELD_MONTH | TI_FIELD_MONTH_CODE))) {
        JS_ThrowTypeError(ctx, "missing required property");
        return -1;
    }

    *month = (fields->present_mask & TI_FIELD_MONTH) ? fields->month : 0;
    if (require_day)
        *day = fields->day;

    if (fields->present_mask & TI_FIELD_MONTH_CODE) {
        if (!ti_calendar_validate_month_code(fields->calendar, fields->month_from_code, fields->month_code_has_l)) {
            JS_ThrowRangeError(ctx, "monthCode is not valid for this calendar");
            return -1;
        }
        {
            int resolved = ti_calendar_month_code_to_month(fields->calendar, *year, fields->month_from_code, fields->month_code_has_l);
            if (fields->present_mask & TI_FIELD_MONTH) {
                if (resolved >= 0 && fields->month != resolved) {
                    JS_ThrowRangeError(ctx, "month and monthCode must agree");
                    return -1;
                }
            } else {
                *month = resolved;
            }
        }
    }

    {
        int max_month = ti_calendar_months_in_year(fields->calendar, *year);
        if (*month > max_month) {
            if (!overflow_constrain) {
                JS_ThrowRangeError(ctx, "invalid date");
                return -1;
            }
            *month = max_month;
        } else if (*month < 1) {
            if (!overflow_constrain) {
                JS_ThrowRangeError(ctx, "invalid date");
                return -1;
            }
            *month = 1;
        }
        if (require_day) {
            int dim = ti_calendar_days_in_month(fields->calendar, *year, *month);
            if (*day > dim) {
                if (!overflow_constrain) {
                    JS_ThrowRangeError(ctx, "invalid date");
                    return -1;
                }
                *day = dim;
            } else if (*day < 1) {
                if (!overflow_constrain) {
                    JS_ThrowRangeError(ctx, "invalid date");
                    return -1;
                }
                *day = 1;
            }
        }
    }

    return 0;
}


/* ------------------------------------------------------------------ */
/* ISO calendar date arithmetic helpers for CalendarDateUntil etc.     */
/* ------------------------------------------------------------------ */

/* CalendarDateUntil for ISO8601 calendar as specified in calendar.html.
   This uses direct arithmetic instead of loops for performance. */
void ti_iso_calendar_date_until(int one_year, int one_month, int one_day,
                                 int two_year, int two_month, int two_day,
                                 TIUnit largestUnit,
                                 int64_t *out_years, int64_t *out_months,
                                 int64_t *out_weeks, int64_t *out_days)
{
    int sign;
    int64_t years = 0, months = 0, weeks = 0, days = 0;
    int iy, im, id;
    int64_t remaining_days;

    /* CompareISODate */
    if (one_year < two_year) sign = -1;
    else if (one_year > two_year) sign = 1;
    else if (one_month < two_month) sign = -1;
    else if (one_month > two_month) sign = 1;
    else if (one_day < two_day) sign = -1;
    else if (one_day > two_day) sign = 1;
    else sign = 0;

    if (out_years) *out_years = 0;
    if (out_months) *out_months = 0;
    if (out_weeks) *out_weeks = 0;
    if (out_days) *out_days = 0;
    if (sign == 0)
        return;

    sign = -sign;

    if (largestUnit == TI_UNIT_YEAR) {
        years = (int64_t)two_year - (int64_t)one_year;
        if (sign == 1 && (two_month < one_month || (two_month == one_month && two_day < one_day)))
            years -= 1;
        else if (sign == -1 && (two_month > one_month || (two_month == one_month && two_day > one_day)))
            years += 1;
    }

    if (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) {
        int64_t iy64 = (int64_t)one_year + years;
        int64_t im64 = (int64_t)one_month;
        im64 = ((im64 - 1) % 12) + 1;
        /* Balance year if months went out of range */
        iy64 += (int64_t)floor_div64((int)(im64 - 1), 12);
        im64 = ((im64 - 1) - floor_div64((int)(im64 - 1), 12) * 12) + 1;
        months = ((int64_t)two_year - iy64) * 12 + ((int64_t)two_month - im64);
        if (sign == 1 && two_day < one_day)
            months -= 1;
        else if (sign == -1 && two_day > one_day)
            months += 1;
    }

    /* Compute intermediate date after adding years and months */
    iy = one_year + (int)years;
    im = one_month + (int)months;
    /* BalanceISOYearMonth */
    iy += (int)floor_div64(im - 1, 12);
    im = (im - 1) - (int)floor_div64(im - 1, 12) * 12 + 1;
    /* RegulateISODate with constrain */
    id = one_day;
    if (id < 1) id = 1;
    if (id > pd_days_in_month(iy, im)) id = pd_days_in_month(iy, im);

    remaining_days = pdt_to_ordinal_days(two_year, two_month, two_day) -
                     pdt_to_ordinal_days(iy, im, id);

    if (largestUnit == TI_UNIT_WEEK) {
        weeks = remaining_days / 7;
        days = remaining_days % 7;
    } else {
        days = remaining_days;
    }

    if (out_years) *out_years = years;
    if (out_months) *out_months = months;
    if (out_weeks) *out_weeks = weeks;
    if (out_days) *out_days = days;
}

int ti_iso_calendar_date_add(int year, int month, int day,
                              int64_t years, int64_t months, int64_t weeks, int64_t days,
                              int *out_year, int *out_month, int *out_day)
{
    int64_t y = (int64_t)year + years;
    int64_t m = (int64_t)month + months;
    int64_t d = (int64_t)day;

    /* BalanceISOYearMonth */
    y += floor_div64(m - 1, 12);
    m = (m - 1) - floor_div64(m - 1, 12) * 12 + 1;

    /* RegulateISODate (constrain): clamp day to month length */
    int dim = pd_days_in_month((int)y, (int)m);
    if (d > dim) d = dim;
    if (d < 1) d = 1;

    /* Add weeks and days, then BalanceISODate */
    {
        js_int128_t delta = (js_int128_t)weeks * 7 + days;
        if (y < INT_MIN || y > INT_MAX)
            return -1;
        js_int128_t ordinal = pdt_to_ordinal_days((int)y, (int)m, 1);
        ordinal += (d - 1) + delta;
        if (ordinal < INT64_MIN || ordinal > INT64_MAX)
            return -1;
        {
            int fy, fm, fd;
            pdt_from_ordinal_days((int64_t)ordinal, &fy, &fm, &fd);
            y = fy;
            m = fm;
            d = fd;
        }
    }

    *out_year = (int)y;
    *out_month = (int)m;
    *out_day = (int)d;
    return 0;
}

/* Add a duration to an ISO datetime, handling time overflow into days */
int ti_iso_datetime_add_duration(
    int year, int month, int day,
    int hour, int minute, int second, int ms, int us, int ns,
    int64_t dyears, int64_t dmonths, int64_t dweeks, int64_t ddays,
    int64_t dhours, int64_t dminutes, int64_t dseconds,
    int64_t dms, js_int128_t dus, js_int128_t dns,
    int *out_year, int *out_month, int *out_day,
    int *out_hour, int *out_minute, int *out_second,
    int *out_ms, int *out_us, int *out_ns)
{
    int64_t base_sec = hour * 3600LL + minute * 60LL + second;
    js_int128_t total_ns = (js_int128_t)base_sec * 1000000000LL + (js_int128_t)ms * 1000000LL + (js_int128_t)us * 1000LL + ns;
    int64_t dur_sec = dhours * 3600LL + dminutes * 60LL + dseconds;
    js_int128_t dur_ns = (js_int128_t)dur_sec * 1000000000LL + (js_int128_t)dms * 1000000LL + dus * 1000LL + dns;
    total_ns += dur_ns;

    int64_t delta_days = (int64_t)(total_ns / 86400000000000LL);
    js_int128_t rem_ns = total_ns % 86400000000000LL;
    if (rem_ns < 0) {
        delta_days--;
        rem_ns += 86400000000000LL;
    }

    *out_hour = (int)(rem_ns / 3600000000000LL);
    rem_ns %= 3600000000000LL;
    *out_minute = (int)(rem_ns / 60000000000LL);
    rem_ns %= 60000000000LL;
    *out_second = (int)(rem_ns / 1000000000LL);
    rem_ns %= 1000000000LL;
    *out_ms = (int)(rem_ns / 1000000LL);
    rem_ns %= 1000000LL;
    *out_us = (int)(rem_ns / 1000LL);
    *out_ns = (int)(rem_ns % 1000LL);

    return ti_iso_calendar_date_add(year, month, day, dyears, dmonths, dweeks, ddays + delta_days,
                             out_year, out_month, out_day);
}

/* DifferenceISODateTime for ISO calendar (no timezone) */
int ti_iso_datetime_difference(
    int y1, int m1, int d1, int h1, int min1, int s1, int ms1, int us1, int ns1,
    int y2, int m2, int d2, int h2, int min2, int s2, int ms2, int us2, int ns2,
    TIUnit largestUnit,
    JSDurationData *out)
{
    int64_t time1_sec = h1 * 3600LL + min1 * 60LL + s1;
    int64_t time2_sec = h2 * 3600LL + min2 * 60LL + s2;
    js_int128_t time1_ns = (js_int128_t)time1_sec * 1000000000LL + (js_int128_t)ms1 * 1000000LL + (js_int128_t)us1 * 1000LL + ns1;
    js_int128_t time2_ns = (js_int128_t)time2_sec * 1000000000LL + (js_int128_t)ms2 * 1000000LL + (js_int128_t)us2 * 1000LL + ns2;
    js_int128_t time_diff = time2_ns - time1_ns;
    int time_sign = (time_diff > 0) ? 1 : (time_diff < 0) ? -1 : 0;

    int64_t ord1 = pdt_to_ordinal_days(y1, m1, d1);
    int64_t ord2 = pdt_to_ordinal_days(y2, m2, d2);
    int date_sign = (ord1 < ord2) ? -1 : (ord1 > ord2) ? 1 : 0;

    int ay = y2, am = m2, ad = d2;

    if (date_sign != 0 && date_sign == time_sign) {
        /* AddDaysToISODate: use BalanceISODate, not constrain */
        ad = d2 + time_sign;
        am = m2;
        ay = y2;
        while (ad < 1) {
            am--;
            if (am < 1) { am = 12; ay--; }
            ad += pd_days_in_month(ay, am);
        }
        while (ad > pd_days_in_month(ay, am)) {
            ad -= pd_days_in_month(ay, am);
            am++;
            if (am > 12) { am = 1; ay++; }
        }
        time_diff -= (js_int128_t)time_sign * 86400000000000LL;
    }

    TIUnit dateLargestUnit = (largestUnit <= TI_UNIT_DAY) ? largestUnit : TI_UNIT_DAY;

    int64_t years = 0, months = 0, weeks = 0, days = 0;
    ti_iso_calendar_date_until(y1, m1, d1, ay, am, ad, dateLargestUnit,
                                &years, &months, &weeks, &days);

    if (largestUnit != dateLargestUnit) {
        /* For time-unit largestUnit, convert all calendar units to nanoseconds.
           Use the ordinal day difference for exact conversion. */
        int64_t total_days = pdt_to_ordinal_days(ay, am, ad) - ord1;
        time_diff += (js_int128_t)total_days * 86400000000000LL;
        years = months = weeks = days = 0;
    }

    memset(out, 0, sizeof(*out));
    out->years = years;
    out->months = months;
    out->weeks = weeks;
    out->days = days;

    if (time_diff < 0) {
        js_int128_t abs_time = -time_diff;
        out->hours = -(int64_t)(abs_time / 3600000000000LL);
        abs_time %= 3600000000000LL;
        out->minutes = -(int64_t)(abs_time / 60000000000LL);
        abs_time %= 60000000000LL;
        out->seconds = -(int64_t)(abs_time / 1000000000LL);
        abs_time %= 1000000000LL;
        out->milliseconds = -(int64_t)(abs_time / 1000000LL);
        abs_time %= 1000000LL;
        out->microseconds.int128_value = -(abs_time / 1000LL);
        out->nanoseconds.int128_value = -(abs_time % 1000LL);
    } else {
        out->hours = (int64_t)(time_diff / 3600000000000LL);
        time_diff %= 3600000000000LL;
        out->minutes = (int64_t)(time_diff / 60000000000LL);
        time_diff %= 60000000000LL;
        out->seconds = (int64_t)(time_diff / 1000000000LL);
        time_diff %= 1000000000LL;
        out->milliseconds = (int64_t)(time_diff / 1000000LL);
        time_diff %= 1000000LL;
        out->microseconds.int128_value = time_diff / 1000LL;
        out->nanoseconds.int128_value = time_diff % 1000LL;
    }

    return 0;
}

/* Balance time fields in a JSDurationData based on largestUnit, matching
   TemporalDurationFromInternal behavior. */
void ti_balance_time_duration(JSDurationData *d, TIUnit largestUnit)
{
    js_int128_t total_ns = ti_duration_time_fields_to_ns(d);
    if (largestUnit > TI_UNIT_DAY)
        total_ns += (js_int128_t)d->days * 86400000000000LL;
    int sign = (total_ns < 0) ? -1 : (total_ns > 0) ? 1 : 0;
    js_int128_t abs_ns = total_ns >= 0 ? total_ns : -total_ns;

    d->hours = 0;
    d->minutes = 0;
    d->seconds = 0;
    d->milliseconds = 0;
    d->microseconds.int128_value = 0;
    d->nanoseconds.int128_value = 0;

    if (sign == 0) return;

    if (largestUnit <= TI_UNIT_DAY) {
        /* Date category: balance up to days */
        js_int128_t total_seconds = abs_ns / 1000000000LL;
        js_int128_t ns = abs_ns % 1000000000LL;
        js_int128_t total_minutes = total_seconds / 60;
        js_int128_t seconds = total_seconds % 60;
        js_int128_t total_hours = total_minutes / 60;
        js_int128_t minutes = total_minutes % 60;
        js_int128_t days = total_hours / 24;
        js_int128_t hours = total_hours % 24;
        d->days += sign * (int64_t)days;
        d->hours = sign * (int64_t)hours;
        d->minutes = sign * (int64_t)minutes;
        d->seconds = sign * (int64_t)seconds;
        d->milliseconds = sign * (int64_t)(ns / 1000000LL);
        ns %= 1000000LL;
        d->microseconds.int128_value = sign * (ns / 1000LL);
        d->nanoseconds.int128_value = sign * (ns % 1000LL);
    } else if (largestUnit == TI_UNIT_HOUR) {
        js_int128_t total_seconds = abs_ns / 1000000000LL;
        js_int128_t ns = abs_ns % 1000000000LL;
        js_int128_t total_minutes = total_seconds / 60;
        js_int128_t seconds = total_seconds % 60;
        js_int128_t hours = total_minutes / 60;
        js_int128_t minutes = total_minutes % 60;
        d->hours = sign * (int64_t)hours;
        d->minutes = sign * (int64_t)minutes;
        d->seconds = sign * (int64_t)seconds;
        d->milliseconds = sign * (int64_t)(ns / 1000000LL);
        ns %= 1000000LL;
        d->microseconds.int128_value = sign * (ns / 1000LL);
        d->nanoseconds.int128_value = sign * (ns % 1000LL);
    } else if (largestUnit == TI_UNIT_MINUTE) {
        js_int128_t total_seconds = abs_ns / 1000000000LL;
        js_int128_t ns = abs_ns % 1000000000LL;
        js_int128_t minutes = total_seconds / 60;
        js_int128_t seconds = total_seconds % 60;
        d->minutes = sign * (int64_t)minutes;
        d->seconds = sign * (int64_t)seconds;
        d->milliseconds = sign * (int64_t)(ns / 1000000LL);
        ns %= 1000000LL;
        d->microseconds.int128_value = sign * (ns / 1000LL);
        d->nanoseconds.int128_value = sign * (ns % 1000LL);
    } else if (largestUnit == TI_UNIT_SECOND) {
        js_int128_t total_ms = abs_ns / 1000000LL;
        js_int128_t ns = abs_ns % 1000000LL;
        js_int128_t seconds = total_ms / 1000;
        js_int128_t ms = total_ms % 1000;
        d->seconds = sign * (int64_t)seconds;
        d->milliseconds = sign * (int64_t)ms;
        d->microseconds.int128_value = sign * (ns / 1000LL);
        d->nanoseconds.int128_value = sign * (ns % 1000LL);
    } else if (largestUnit == TI_UNIT_MILLISECOND) {
        js_int128_t total_us = abs_ns / 1000LL;
        js_int128_t ns = abs_ns % 1000LL;
        js_int128_t ms = total_us / 1000;
        js_int128_t us = total_us % 1000;
        d->milliseconds = sign * (int64_t)ms;
        d->microseconds.int128_value = sign * us;
        d->nanoseconds.int128_value = sign * ns;
    } else if (largestUnit == TI_UNIT_MICROSECOND) {
        js_int128_t us = abs_ns / 1000LL;
        js_int128_t ns = abs_ns % 1000LL;
        d->microseconds.int128_value = sign * us;
        d->nanoseconds.int128_value = sign * ns;
    } else {
        /* Nanosecond largestUnit: keep total in nanoseconds */
        d->nanoseconds.int128_value = sign * abs_ns;
    }
}

/* Calendar-aware datetime difference for non-ISO calendars.
   The date fields are interpreted as calendar fields. */
int ti_calendar_datetime_difference(
    CalendarType cal,
    int y1, int m1, int d1, int h1, int min1, int s1, int ms1, int us1, int ns1,
    int y2, int m2, int d2, int h2, int min2, int s2, int ms2, int us2, int ns2,
    TIUnit largestUnit,
    JSDurationData *out)
{
    int64_t time1_sec = h1 * 3600LL + min1 * 60LL + s1;
    int64_t time2_sec = h2 * 3600LL + min2 * 60LL + s2;
    js_int128_t time1_ns = (js_int128_t)time1_sec * 1000000000LL + (js_int128_t)ms1 * 1000000LL + (js_int128_t)us1 * 1000LL + ns1;
    js_int128_t time2_ns = (js_int128_t)time2_sec * 1000000000LL + (js_int128_t)ms2 * 1000000LL + (js_int128_t)us2 * 1000LL + ns2;
    js_int128_t time_diff = time2_ns - time1_ns;
    int time_sign = (time_diff > 0) ? 1 : (time_diff < 0) ? -1 : 0;

    int date_sign;
    if (y1 != y2) date_sign = (y1 < y2) ? -1 : 1;
    else if (m1 != m2) date_sign = (m1 < m2) ? -1 : 1;
    else if (d1 != d2) date_sign = (d1 < d2) ? -1 : 1;
    else date_sign = 0;

    int ay = y2, am = m2, ad = d2;

    if (date_sign != 0 && date_sign == time_sign) {
        ad = d2 + time_sign;
        if (time_sign > 0) {
            int dim = ti_calendar_days_in_month(cal, y2, m2);
            if (ad > dim) {
                ad = 1;
                am = m2 + 1;
                if (am > ti_calendar_months_in_year(cal, y2)) {
                    am = 1;
                    ay = y2 + 1;
                }
            }
        } else {
            if (ad < 1) {
                am = m2 - 1;
                if (am < 1) {
                    ay = y2 - 1;
                    am = ti_calendar_months_in_year(cal, ay);
                }
                ad = ti_calendar_days_in_month(cal, ay, am);
            }
        }
        time_diff -= (js_int128_t)time_sign * 86400000000000LL;
    }

    TIUnit dateLargestUnit = (largestUnit <= TI_UNIT_DAY) ? largestUnit : TI_UNIT_DAY;

    int64_t years = 0, months = 0, weeks = 0, days = 0;

    if ((cal == CAL_CHINESE || cal == CAL_DANGI || cal == CAL_HEBREW) &&
        (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH)) {
        int mc1, has_l1, mc2, has_l2;
        ti_calendar_month_to_month_code(cal, y1, m1, &mc1, &has_l1);
        ti_calendar_month_to_month_code(cal, ay, am, &mc2, &has_l2);
        ti_calendar_date_until_with_monthcode(cal,
            y1, m1, d1, mc1, 1, has_l1,
            ay, am, ad, mc2, 1, has_l2, dateLargestUnit,
            &years, &months, &weeks, &days);
    } else if (cal != CAL_ISO8601 && cal != CAL_GREGORY) {
        ti_calendar_date_until(cal, y1, m1, d1, ay, am, ad, dateLargestUnit,
            &years, &months, &weeks, &days);
    } else {
        ti_iso_calendar_date_until(y1, m1, d1, ay, am, ad, dateLargestUnit,
            &years, &months, &weeks, &days);
    }

    if (largestUnit != dateLargestUnit) {
        int64_t total_days = 0;
        int64_t dummy_y = 0, dummy_m = 0, dummy_w = 0;
        if (cal == CAL_CHINESE || cal == CAL_DANGI || cal == CAL_HEBREW) {
            int mc1, has_l1, mc2, has_l2;
            ti_calendar_month_to_month_code(cal, y1, m1, &mc1, &has_l1);
            ti_calendar_month_to_month_code(cal, ay, am, &mc2, &has_l2);
            ti_calendar_date_until_with_monthcode(cal,
                y1, m1, d1, mc1, 1, has_l1,
                ay, am, ad, mc2, 1, has_l2, TI_UNIT_DAY,
                &dummy_y, &dummy_m, &dummy_w, &total_days);
        } else {
            ti_calendar_date_until(cal, y1, m1, d1, ay, am, ad, TI_UNIT_DAY,
                &dummy_y, &dummy_m, &dummy_w, &total_days);
        }
        time_diff += (js_int128_t)total_days * 86400000000000LL;
        years = months = weeks = days = 0;
    }

    memset(out, 0, sizeof(*out));
    out->years = years;
    out->months = months;
    out->weeks = weeks;
    out->days = days;

    if (time_diff < 0) {
        js_int128_t abs_time = -time_diff;
        out->hours = -(int64_t)(abs_time / 3600000000000LL);
        abs_time %= 3600000000000LL;
        out->minutes = -(int64_t)(abs_time / 60000000000LL);
        abs_time %= 60000000000LL;
        out->seconds = -(int64_t)(abs_time / 1000000000LL);
        abs_time %= 1000000000LL;
        out->milliseconds = -(int64_t)(abs_time / 1000000LL);
        abs_time %= 1000000LL;
        out->microseconds.int128_value = -(abs_time / 1000LL);
        out->nanoseconds.int128_value = -(abs_time % 1000LL);
    } else {
        out->hours = (int64_t)(time_diff / 3600000000000LL);
        time_diff %= 3600000000000LL;
        out->minutes = (int64_t)(time_diff / 60000000000LL);
        time_diff %= 60000000000LL;
        out->seconds = (int64_t)(time_diff / 1000000000LL);
        time_diff %= 1000000000LL;
        out->milliseconds = (int64_t)(time_diff / 1000000LL);
        time_diff %= 1000000LL;
        out->microseconds.int128_value = time_diff / 1000LL;
        out->nanoseconds.int128_value = time_diff % 1000LL;
    }

    return 0;
}

/* Round a duration relative to an ISO datetime. For ISO calendar only. */
int ti_round_relative_duration_iso(
    JSContext *ctx,
    int rel_year, int rel_month, int rel_day,
    int rel_hour, int rel_minute, int rel_second,
    int rel_ms, int rel_us, int rel_ns,
    const JSDurationData *duration,
    TIUnit largestUnit, TIUnit smallestUnit,
    int64_t increment, TIRoundingMode roundingMode,
    BOOL has_timeZone,
    JSDurationData *out)
{
    int end_year, end_month, end_day, end_hour, end_minute, end_second, end_ms, end_us, end_ns;
    js_int128_t us_val = duration->microseconds.int128_value;
    js_int128_t ns_val = duration->nanoseconds.int128_value;
    JSDurationData rounded = {0};

    /* Compute end = relativeTo + duration */
    if (ti_iso_datetime_add_duration(
        rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
        duration->years, duration->months, duration->weeks, duration->days,
        duration->hours, duration->minutes, duration->seconds,
        duration->milliseconds, us_val, ns_val,
        &end_year, &end_month, &end_day, &end_hour, &end_minute, &end_second, &end_ms, &end_us, &end_ns) < 0) {
        JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
        return -1;
    }

    if (!ti_is_date_within_iso_limits(end_year, end_month, end_day)) {
        JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
        return -1;
    }

    if (smallestUnit == TI_UNIT_YEAR || smallestUnit == TI_UNIT_MONTH ||
        smallestUnit == TI_UNIT_WEEK || (has_timeZone && smallestUnit == TI_UNIT_DAY)) {
        /* Calendar unit rounding (day is only a calendar unit for ZonedDateTime) */
        int64_t ry, rm, rw, rd;
        int64_t rel_time_ns = (rel_hour * 3600LL + rel_minute * 60LL + rel_second) * 1000000000LL
                            + rel_ms * 1000000LL + rel_us * 1000LL + rel_ns;
        int64_t dest_time_ns = (end_hour * 3600LL + end_minute * 60LL + end_second) * 1000000000LL
                             + end_ms * 1000000LL + end_us * 1000LL + end_ns;

        /* Compute the normalized difference between rel and end, then round
           that. This is necessary because the input duration may not be in
           normalized form (e.g. P5Y6M7W8D from Duration.prototype.round). */
        JSDurationData diff = {0};
        ti_iso_datetime_difference(rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
                                    end_year, end_month, end_day, end_hour, end_minute, end_second, end_ms, end_us, end_ns,
                                    largestUnit, &diff);

        if (ti_round_iso_date_duration(ctx, rel_year, rel_month, rel_day,
                                        end_year, end_month, end_day,
                                        rel_time_ns, dest_time_ns,
                                        diff.years, diff.months, diff.weeks, diff.days,
                                        largestUnit, smallestUnit, increment, roundingMode,
                                        0,
                                        has_timeZone,
                                        &ry, &rm, &rw, &rd) < 0)
            return -1;

        memset(&rounded, 0, sizeof(rounded));
        rounded.years = ry;
        rounded.months = rm;
        rounded.weeks = rw;
        rounded.days = rd;
    } else {
        /* Time unit rounding */
        js_int128_t day_time_ns;
        if (has_timeZone) {
            /* For ZonedDateTime, days are kept separate because day length
               varies due to timezone offsets. Only round the time part. */
            day_time_ns = ti_duration_time_fields_to_ns(duration);
        } else {
            day_time_ns = (js_int128_t)duration->days * 86400000000000LL;
            day_time_ns += ti_duration_time_fields_to_ns(duration);
        }

        js_int128_t unit_ns;
        switch (smallestUnit) {
        case TI_UNIT_DAY: unit_ns = 86400000000000LL; break;
        case TI_UNIT_HOUR: unit_ns = 3600000000000LL; break;
        case TI_UNIT_MINUTE: unit_ns = 60000000000LL; break;
        case TI_UNIT_SECOND: unit_ns = 1000000000LL; break;
        case TI_UNIT_MILLISECOND: unit_ns = 1000000LL; break;
        case TI_UNIT_MICROSECOND: unit_ns = 1000LL; break;
        case TI_UNIT_NANOSECOND: unit_ns = 1LL; break;
        default: unit_ns = 1LL; break;
        }

        js_int128_t rounded_ns = ti_round_to_increment128(day_time_ns, unit_ns * increment, roundingMode);

        int64_t new_days;
        js_int128_t new_time_ns;
        if (largestUnit <= TI_UNIT_DAY) {
            /* Date category: split into days and time using truncate toward zero */
            new_days = (int64_t)(rounded_ns / 86400000000000LL);
            new_time_ns = rounded_ns % 86400000000000LL;
        } else {
            /* Time category: keep everything in time */
            new_days = 0;
            new_time_ns = rounded_ns;
        }

        memset(&rounded, 0, sizeof(rounded));
        rounded.years = duration->years;
        rounded.months = duration->months;
        rounded.weeks = duration->weeks;
        if (has_timeZone) {
            rounded.days = duration->days + new_days;
        } else {
            rounded.days = new_days;
        }

        if (new_time_ns < 0) {
            js_int128_t abs_time = -new_time_ns;
            if (TI_UNIT_HOUR >= largestUnit) {
                rounded.hours = -(int64_t)(abs_time / 3600000000000LL);
                abs_time %= 3600000000000LL;
            }
            if (TI_UNIT_MINUTE >= largestUnit) {
                rounded.minutes = -(int64_t)(abs_time / 60000000000LL);
                abs_time %= 60000000000LL;
            }
            if (TI_UNIT_SECOND >= largestUnit) {
                rounded.seconds = -(int64_t)(abs_time / 1000000000LL);
                abs_time %= 1000000000LL;
            }
            if (TI_UNIT_MILLISECOND >= largestUnit) {
                rounded.milliseconds = -(int64_t)(abs_time / 1000000LL);
                abs_time %= 1000000LL;
            }
            if (TI_UNIT_MICROSECOND >= largestUnit) {
                rounded.microseconds.int128_value = -(abs_time / 1000LL);
            }
            if (TI_UNIT_NANOSECOND >= largestUnit) {
                if (largestUnit == TI_UNIT_NANOSECOND && smallestUnit == TI_UNIT_NANOSECOND)
                    rounded.nanoseconds.int128_value = -abs_time;
                else
                    rounded.nanoseconds.int128_value = -(abs_time % 1000LL);
            }
        } else {
            if (TI_UNIT_HOUR >= largestUnit) {
                rounded.hours = (int64_t)(new_time_ns / 3600000000000LL);
                new_time_ns %= 3600000000000LL;
            }
            if (TI_UNIT_MINUTE >= largestUnit) {
                rounded.minutes = (int64_t)(new_time_ns / 60000000000LL);
                new_time_ns %= 60000000000LL;
            }
            if (TI_UNIT_SECOND >= largestUnit) {
                rounded.seconds = (int64_t)(new_time_ns / 1000000000LL);
                new_time_ns %= 1000000000LL;
            }
            if (TI_UNIT_MILLISECOND >= largestUnit) {
                rounded.milliseconds = (int64_t)(new_time_ns / 1000000LL);
                new_time_ns %= 1000000LL;
            }
            if (TI_UNIT_MICROSECOND >= largestUnit) {
                rounded.microseconds.int128_value = new_time_ns / 1000LL;
            }
            if (TI_UNIT_NANOSECOND >= largestUnit) {
                if (largestUnit == TI_UNIT_NANOSECOND && smallestUnit == TI_UNIT_NANOSECOND)
                    rounded.nanoseconds.int128_value = new_time_ns;
                else
                    rounded.nanoseconds.int128_value = new_time_ns % 1000LL;
            }
        }
    }

    if (largestUnit <= TI_UNIT_DAY) {
        /* Date category largestUnit: recompute by adding rounded duration back
           to relativeTo so that calendar constraints and unit conversions are
           applied correctly (e.g., days -> years). */
        /* For calendar-unit rounding (year/month/week), return the rounded
           duration directly. Recomputing via CalendarDateUntil can collapse
           the rounded unit due to calendar constraints (e.g. Feb 29 + 1 year
           becomes Feb 28, and diffing gives 11 months 30 days instead of 1
           year). The spec's NudgeToCalendarUnit returns the nudged duration
           directly, not via a diff recomputation. */
        if (smallestUnit == TI_UNIT_YEAR || smallestUnit == TI_UNIT_MONTH ||
            smallestUnit == TI_UNIT_WEEK) {
            /* ISO calendar balancing: bubble months into years when
               largestUnit permits. */
            if (largestUnit == TI_UNIT_YEAR && rounded.months != 0) {
                int64_t years_from_months = rounded.months / 12;
                rounded.years += years_from_months;
                rounded.months -= years_from_months * 12;
            }
            *out = rounded;
            return 0;
        }
        int ny, nm, nd, nh, nmin, ns, nms, nus, nns;
        js_int128_t r_us = rounded.microseconds.int128_value;
        js_int128_t r_ns = rounded.nanoseconds.int128_value;
        if (ti_iso_datetime_add_duration(
            rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
            rounded.years, rounded.months, rounded.weeks, rounded.days,
            rounded.hours, rounded.minutes, rounded.seconds,
            rounded.milliseconds, r_us, r_ns,
            &ny, &nm, &nd, &nh, &nmin, &ns, &nms, &nus, &nns) < 0) {
            JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
            return -1;
        }

        return ti_iso_datetime_difference(
            rel_year, rel_month, rel_day, rel_hour, rel_minute, rel_second, rel_ms, rel_us, rel_ns,
            ny, nm, nd, nh, nmin, ns, nms, nus, nns,
            largestUnit, out);
    } else {
        /* Time category largestUnit: return directly */
        *out = rounded;
        return 0;
    }
}

int ti_is_date_within_iso_limits(int year, int month, int day)
{
    if (year < -271821 || year > 275760) return 0;
    if (year == -271821 && (month < 4 || (month == 4 && day < 19))) return 0;
    if (year == 275760 && (month > 9 || (month == 9 && day > 13))) return 0;
    return 1;
}

int ti_round_iso_date_duration(JSContext *ctx,
                                int oy, int om, int od,
                                int dy, int dm, int dd,
                                int64_t rel_time_ns, int64_t dest_time_ns,
                                int64_t years, int64_t months, int64_t weeks, int64_t days,
                                TIUnit largestUnit, TIUnit smallestUnit,
                                int64_t increment, TIRoundingMode rounding_mode,
                                int do_bubble,
                                BOOL has_timeZone,
                                int64_t *out_years, int64_t *out_months,
                                int64_t *out_weeks, int64_t *out_days)
{
    int sign;
    int64_t r1, r2;
    int64_t start_years, start_months, start_weeks, start_days;
    int64_t end_years, end_months, end_weeks, end_days;
    int sy, sm, sd, ey, em, ed;
    int64_t start_ord, end_ord, dest_ord;
    js_int128_t numerator, denominator;
    int cmp;
    int even;
    int64_t ry, rm, rw, rd;
    int choice;

    /* Determine sign from the relationship between rel and dest.
       Per spec RoundRelativeDuration step 2, if duration sign is 0, set sign to 1. */
    {
        int64_t rel_ord = pdt_to_ordinal_days(oy, om, od);
        int64_t inner_dest_ord = pdt_to_ordinal_days(dy, dm, dd);
        if (rel_ord < inner_dest_ord || (rel_ord == inner_dest_ord && rel_time_ns < dest_time_ns))
            sign = 1;
        else if (rel_ord > inner_dest_ord || (rel_ord == inner_dest_ord && rel_time_ns > dest_time_ns))
            sign = -1;
        else
            sign = 1;
    }

    /* Compute r1 (truncated toward zero) and r2 (next multiple in sign direction) */
    switch (smallestUnit) {
    case TI_UNIT_YEAR: {
        r1 = (years / increment) * increment;
        r2 = r1 + increment * sign;
        start_years = r1; start_months = 0; start_weeks = 0; start_days = 0;
        end_years = r2; end_months = 0; end_weeks = 0; end_days = 0;
        break;
    }
    case TI_UNIT_MONTH: {
        r1 = (months / increment) * increment;
        r2 = r1 + increment * sign;
        start_years = years; start_months = r1; start_weeks = 0; start_days = 0;
        end_years = years; end_months = r2; end_weeks = 0; end_days = 0;
        break;
    }
    case TI_UNIT_WEEK: {
        int wsy, wsm, wsd, wey, wem, wed;
        int64_t wweeks, wdays;
        /* weeksStart = origin + years + months */
        if (ti_iso_calendar_date_add(oy, om, od, years, months, 0, 0, &wsy, &wsm, &wsd) < 0 ||
            ti_iso_calendar_date_add(wsy, wsm, wsd, 0, 0, 0, days, &wey, &wem, &wed) < 0) {
            JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
            return -1;
        }
        /* CalendarDateUntil(weeksStart, weeksEnd, week) */
        ti_iso_calendar_date_until(wsy, wsm, wsd, wey, wem, wed, TI_UNIT_WEEK,
                                    &wweeks, &wdays, NULL, NULL);
        r1 = ((weeks + wweeks) / increment) * increment;
        r2 = r1 + increment * sign;
        start_years = years; start_months = months; start_weeks = r1; start_days = 0;
        end_years = years; end_months = months; end_weeks = r2; end_days = 0;
        break;
    }
    case TI_UNIT_DAY: {
        r1 = (days / increment) * increment;
        r2 = r1 + increment * sign;
        start_years = years; start_months = months; start_weeks = weeks; start_days = r1;
        end_years = years; end_months = months; end_weeks = weeks; end_days = r2;
        break;
    }
    default:
        *out_years = years; *out_months = months; *out_weeks = weeks; *out_days = days;
        return 0;
    }

    /* Convert startDuration and endDuration to ordinal days */
    if (ti_iso_calendar_date_add(oy, om, od, start_years, start_months, start_weeks, start_days, &sy, &sm, &sd) < 0 ||
        ti_iso_calendar_date_add(oy, om, od, end_years, end_months, end_weeks, end_days, &ey, &em, &ed) < 0) {
        JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
        return -1;
    }
    if (has_timeZone) {
        int64_t start_epoch_days = pdt_to_ordinal_days(sy, sm, sd) - 719162;
        int64_t end_epoch_days = pdt_to_ordinal_days(ey, em, ed) - 719162;
        int64_t time_sec = rel_time_ns / 1000000000LL;
        int time_nsec = (int)(rel_time_ns % 1000000000LL);
        if (time_nsec < 0) {
            time_nsec += 1000000000;
            time_sec--;
        }
        JSTimeSpec start_ts = { start_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
        JSTimeSpec end_ts = { end_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
        if (!is_valid_epoch_timespec(&start_ts) || !is_valid_epoch_timespec(&end_ts)) {
            JS_ThrowRangeError(ctx, "rounded date outside valid instant range");
            return -1;
        }
    } else {
        if (!ti_is_date_within_iso_limits(sy, sm, sd) || !ti_is_date_within_iso_limits(ey, em, ed)) {
            JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
            return -1;
        }
    }
    start_ord = pdt_to_ordinal_days(sy, sm, sd);
    end_ord = pdt_to_ordinal_days(ey, em, ed);
    dest_ord = pdt_to_ordinal_days(dy, dm, dd);

    /* Check if dest is within [start, end] (for sign=1) or [end, start] (for sign=-1).
       Loop until dest is bracketed, shifting r1/r2 by increment each time. */
    {
        int iter = 0;
        if (sign == 1) {
            while (1) {
                int start_le_dest = (start_ord < dest_ord) || (start_ord == dest_ord && rel_time_ns <= dest_time_ns);
                int dest_le_end = (dest_ord < end_ord) || (dest_ord == end_ord && dest_time_ns <= rel_time_ns);
                if (start_le_dest && dest_le_end)
                    break;
                if (++iter > 10000) {
                    JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                    return -1;
                }
                r1 = r2;
                r2 = r1 + increment * sign;
                switch (smallestUnit) {
                case TI_UNIT_YEAR: start_years = r1; end_years = r2; break;
                case TI_UNIT_MONTH: start_months = r1; end_months = r2; break;
                case TI_UNIT_WEEK: start_weeks = r1; end_weeks = r2; break;
                case TI_UNIT_DAY: start_days = r1; end_days = r2; break;
                default: break;
                }
                if (ti_iso_calendar_date_add(oy, om, od, start_years, start_months, start_weeks, start_days, &sy, &sm, &sd) < 0 ||
                    ti_iso_calendar_date_add(oy, om, od, end_years, end_months, end_weeks, end_days, &ey, &em, &ed) < 0) {
                    JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                    return -1;
                }
                if (has_timeZone) {
                    int64_t start_epoch_days = pdt_to_ordinal_days(sy, sm, sd) - 719162;
                    int64_t end_epoch_days = pdt_to_ordinal_days(ey, em, ed) - 719162;
                    int64_t time_sec = rel_time_ns / 1000000000LL;
                    int time_nsec = (int)(rel_time_ns % 1000000000LL);
                    if (time_nsec < 0) {
                        time_nsec += 1000000000;
                        time_sec--;
                    }
                    JSTimeSpec start_ts = { start_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
                    JSTimeSpec end_ts = { end_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
                    if (!is_valid_epoch_timespec(&start_ts) || !is_valid_epoch_timespec(&end_ts)) {
                        JS_ThrowRangeError(ctx, "rounded date outside valid instant range");
                        return -1;
                    }
                } else {
                    if (!ti_is_date_within_iso_limits(sy, sm, sd) || !ti_is_date_within_iso_limits(ey, em, ed)) {
                        JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                        return -1;
                    }
                }
                start_ord = pdt_to_ordinal_days(sy, sm, sd);
                end_ord = pdt_to_ordinal_days(ey, em, ed);
            }
        } else {
            while (1) {
                int end_le_dest = (end_ord < dest_ord) || (end_ord == dest_ord && rel_time_ns <= dest_time_ns);
                int dest_le_start = (dest_ord < start_ord) || (dest_ord == start_ord && dest_time_ns <= rel_time_ns);
                if (end_le_dest && dest_le_start)
                    break;
                if (++iter > 10000) {
                    JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                    return -1;
                }
                r1 = r2;
                r2 = r1 + increment * sign;
                switch (smallestUnit) {
                case TI_UNIT_YEAR: start_years = r1; end_years = r2; break;
                case TI_UNIT_MONTH: start_months = r1; end_months = r2; break;
                case TI_UNIT_WEEK: start_weeks = r1; end_weeks = r2; break;
                case TI_UNIT_DAY: start_days = r1; end_days = r2; break;
                default: break;
                }
                if (ti_iso_calendar_date_add(oy, om, od, start_years, start_months, start_weeks, start_days, &sy, &sm, &sd) < 0 ||
                    ti_iso_calendar_date_add(oy, om, od, end_years, end_months, end_weeks, end_days, &ey, &em, &ed) < 0) {
                    JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                    return -1;
                }
                if (has_timeZone) {
                    int64_t start_epoch_days = pdt_to_ordinal_days(sy, sm, sd) - 719162;
                    int64_t end_epoch_days = pdt_to_ordinal_days(ey, em, ed) - 719162;
                    int64_t time_sec = rel_time_ns / 1000000000LL;
                    int time_nsec = (int)(rel_time_ns % 1000000000LL);
                    if (time_nsec < 0) {
                        time_nsec += 1000000000;
                        time_sec--;
                    }
                    JSTimeSpec start_ts = { start_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
                    JSTimeSpec end_ts = { end_epoch_days * 86400LL + time_sec, (uint32_t)time_nsec };
                    if (!is_valid_epoch_timespec(&start_ts) || !is_valid_epoch_timespec(&end_ts)) {
                        JS_ThrowRangeError(ctx, "rounded date outside valid instant range");
                        return -1;
                    }
                } else {
                    if (!ti_is_date_within_iso_limits(sy, sm, sd) || !ti_is_date_within_iso_limits(ey, em, ed)) {
                        JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
                        return -1;
                    }
                }
                start_ord = pdt_to_ordinal_days(sy, sm, sd);
                end_ord = pdt_to_ordinal_days(ey, em, ed);
            }
        }
    }

    if (start_ord == end_ord) {
        *out_years = start_years; *out_months = start_months; *out_weeks = start_weeks; *out_days = start_days;
        return 0;
    }

    /* Compute numerator and denominator in nanoseconds */
    {
        js_int128_t day_ns = 86400000000000LL;
        if (sign == 1) {
            numerator = (js_int128_t)(dest_ord - start_ord) * day_ns + (dest_time_ns - rel_time_ns);
            denominator = (js_int128_t)(end_ord - start_ord) * day_ns;
        } else {
            numerator = (js_int128_t)(start_ord - dest_ord) * day_ns + (rel_time_ns - dest_time_ns);
            denominator = (js_int128_t)(start_ord - end_ord) * day_ns;
        }
    }

    /* cmp = sign( abs(2*numerator) - abs(denominator) ) */
    {
        js_int128_t twice_num = numerator >= 0 ? numerator * 2 : -numerator * 2;
        js_int128_t abs_den = denominator >= 0 ? denominator : -denominator;
        if (twice_num < abs_den) cmp = -1;
        else if (twice_num > abs_den) cmp = 1;
        else cmp = 0;
    }

    /* even = (abs(r1) / increment) % 2 == 0 */
    {
        int64_t abs_r1 = r1 >= 0 ? r1 : -r1;
        even = (abs_r1 / increment) % 2 == 0;
    }

    /* Apply unsigned rounding mode */
    {
        int unsigned_mode;
        switch (rounding_mode) {
        case ROUND_CEIL: unsigned_mode = (sign < 0) ? 0 : 1; break;
        case ROUND_FLOOR: unsigned_mode = (sign < 0) ? 1 : 0; break;
        case ROUND_EXPAND: unsigned_mode = 1; break;
        case ROUND_TRUNC: unsigned_mode = 0; break;
        case ROUND_HALF_CEIL: unsigned_mode = (sign < 0) ? 2 : 3; break;
        case ROUND_HALF_FLOOR: unsigned_mode = (sign < 0) ? 3 : 2; break;
        case ROUND_HALF_EXPAND: unsigned_mode = 3; break;
        case ROUND_HALF_TRUNC: unsigned_mode = 2; break;
        case ROUND_HALF_EVEN: unsigned_mode = 4; break;
        default: unsigned_mode = 3; break;
        }

        if (numerator == 0) {
            choice = 0;
        } else if (numerator == denominator) {
            choice = 1;
        } else {
            switch (unsigned_mode) {
            case 0: /* zero */ choice = 0; break;
            case 1: /* infinity */ choice = 1; break;
            case 2: /* halfZero */
                if (cmp < 0) choice = 0;
                else if (cmp > 0) choice = 1;
                else choice = 0;
                break;
            case 3: /* halfInfinity */
                if (cmp < 0) choice = 0;
                else if (cmp > 0) choice = 1;
                else choice = 1;
                break;
            case 4: /* halfEven */
                if (cmp < 0) choice = 0;
                else if (cmp > 0) choice = 1;
                else choice = even ? 0 : 1;
                break;
            default: choice = 1; break;
            }
        }
    }

    if (choice == 0) {
        ry = start_years; rm = start_months; rw = start_weeks; rd = start_days;
    } else {
        ry = end_years; rm = end_months; rw = end_weeks; rd = end_days;
    }

    if (do_bubble && choice == 1 && smallestUnit != TI_UNIT_WEEK && smallestUnit != TI_UNIT_DAY) {
        int by, bm, bd;
        if (ti_iso_calendar_date_add(oy, om, od, ry, rm, rw, rd, &by, &bm, &bd) < 0 ||
            !ti_is_date_within_iso_limits(by, bm, bd)) {
            JS_ThrowRangeError(ctx, "rounded date outside valid ISO range");
            return -1;
        }
        ti_iso_calendar_date_until(oy, om, od, by, bm, bd, largestUnit,
                                    out_years, out_months, out_weeks, out_days);
    } else {
        *out_years = ry;
        *out_months = rm;
        *out_weeks = rw;
        *out_days = rd;
    }
    return 0;
}

static const JSCFunctionListEntry js_temporal_funcs[] = {
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal", JS_PROP_CONFIGURABLE),
};

static JSValue js_date_toTemporalInstant(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    double v;
    int64_t secs;
    int32_t nsecs;
    js_int128_t total_ns;
    JSTimeSpec ts;
    
    if (JS_ThisDateValue(ctx, &v, this_val))
        return JS_EXCEPTION;
    if (isnan(v))
        return JS_ThrowRangeError(ctx, "invalid Date");
    
    total_ns = (js_int128_t)(int64_t)v * 1000000LL;
    secs = (int64_t)(total_ns / 1000000000LL);
    nsecs = (int32_t)(total_ns % 1000000000LL);
    if (nsecs < 0) {
        secs--;
        nsecs += 1000000000;
    }
    ts.secs = secs;
    ts.nsecs = (uint32_t)nsecs;
    return js_create_instant(ctx, &ts);
}

/* Helper for Temporal.prototype.toLocaleString using Intl.DateTimeFormat.
   Falls back to toString_func if Intl is not available. */
JSValue ti_temporal_toLocaleString(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv,
                                    int default_mode,
                                    JSValue (*toString_func)(JSContext *, JSValueConst, int, JSValueConst *))
{
    JSValue ctor = JS_GetContextValue(ctx, JS_INTL_CTOR_DATETIMEFORMAT);
    JSValue new_argv[2];
    JSValue dtf, method, ret;

    if (JS_IsNull(ctor) || JS_IsUndefined(ctor)) {
        JS_FreeValue(ctx, ctor);
        return toString_func(ctx, this_val, 0, NULL);
    }

    new_argv[0] = argc > 0 ? JS_DupValue(ctx, argv[0]) : JS_UNDEFINED;
    new_argv[1] = argc > 1 ? JS_DupValue(ctx, argv[1]) : JS_UNDEFINED;

    if (JS_IsUndefined(new_argv[1]) || JS_IsNull(new_argv[1])) {
        JS_FreeValue(ctx, new_argv[1]);
        new_argv[1] = JS_NewObject(ctx);
        if (JS_IsException(new_argv[1])) {
            JS_FreeValue(ctx, new_argv[0]);
            JS_FreeValue(ctx, ctor);
            return JS_EXCEPTION;
        }
    }

    if (JS_IsObject(new_argv[1])) {
        int has_date = js_intl_options_has_date_fields(ctx, new_argv[1]);
        int has_time = js_intl_options_has_time_fields(ctx, new_argv[1]);
        if (has_date < 0 || has_time < 0) {
            JS_FreeValue(ctx, new_argv[0]);
            JS_FreeValue(ctx, new_argv[1]);
            JS_FreeValue(ctx, ctor);
            return JS_EXCEPTION;
        }
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

        /* Reject incompatible style options based on Temporal type */
        if (default_mode == 1 && has_ts) { /* date types */
            JS_FreeValue(ctx, new_argv[0]);
            JS_FreeValue(ctx, new_argv[1]);
            JS_FreeValue(ctx, ctor);
            return JS_ThrowTypeError(ctx, "timeStyle option is not allowed for this Temporal type");
        }
        if (default_mode == 2 && has_ds) { /* time types */
            JS_FreeValue(ctx, new_argv[0]);
            JS_FreeValue(ctx, new_argv[1]);
            JS_FreeValue(ctx, ctor);
            return JS_ThrowTypeError(ctx, "dateStyle option is not allowed for this Temporal type");
        }

        if (!has_ds && !has_ts) {
            if (default_mode == 0) { /* any: Instant, PlainDateTime */
                if (!has_date && !has_time) {
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
            } else if (default_mode == 1) { /* date: PlainDate, PlainYearMonth, PlainMonthDay */
                if (!has_date) {
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "year",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "month",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "day",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                }
            } else if (default_mode == 2) { /* time: PlainTime */
                if (!has_time) {
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "hour",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "minute",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                    JS_DefinePropertyValueStr(ctx, new_argv[1], "second",
                                               JS_NewString(ctx, "numeric"), JS_PROP_C_W_E);
                }
            }
        }
    }

    dtf = JS_CallConstructor(ctx, ctor, 2, new_argv);
    JS_FreeValue(ctx, ctor);
    if (JS_IsException(dtf)) {
        JS_FreeValue(ctx, new_argv[0]);
        JS_FreeValue(ctx, new_argv[1]);
        return dtf;
    }

    /* Calendar mismatch check for types with calendars */
    if (JS_GetOpaque(this_val, js_plaindate_class_id) ||
        JS_GetOpaque(this_val, js_plaindatetime_class_id) ||
        JS_GetOpaque(this_val, js_plainyearmonth_class_id) ||
        JS_GetOpaque(this_val, js_plainmonthday_class_id)) {
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
                    int mismatch;
                    if (JS_GetOpaque(this_val, js_plainyearmonth_class_id) ||
                        JS_GetOpaque(this_val, js_plainmonthday_class_id)) {
                        /* PlainYearMonth and PlainMonthDay: no ISO exemption */
                        mismatch = (dtf_cal && obj_cal && strcmp(obj_cal, dtf_cal) != 0);
                    } else {
                        /* PlainDate and PlainDateTime: ISO calendar is exempt */
                        mismatch = (dtf_cal && obj_cal && strcmp(obj_cal, "iso8601") != 0 &&
                                    strcmp(dtf_cal, "iso8601") != 0 && strcmp(obj_cal, dtf_cal) != 0);
                    }
                    if (mismatch) {
                        JS_FreeCString(ctx, dtf_cal);
                        JS_FreeCString(ctx, obj_cal);
                        JS_FreeValue(ctx, dtf_cal_val);
                        JS_FreeValue(ctx, obj_cal_val);
                        JS_FreeValue(ctx, dtf);
                        JS_FreeValue(ctx, new_argv[0]);
                        JS_FreeValue(ctx, new_argv[1]);
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

    JS_FreeValue(ctx, new_argv[0]);
    JS_FreeValue(ctx, new_argv[1]);

    method = JS_GetPropertyStr(ctx, dtf, "format");
    if (JS_IsException(method)) {
        JS_FreeValue(ctx, dtf);
        return method;
    }

    ret = JS_Call(ctx, method, dtf, 1, &this_val);
    JS_FreeValue(ctx, method);
    JS_FreeValue(ctx, dtf);
    return ret;
}

int JS_AddIntrinsicTemporal(JSContext *ctx)
{
    JSValue global_obj, temporal_obj;
    int ret;
    
    global_obj = JS_GetGlobalObject(ctx);
    temporal_obj = JS_NewObject(ctx);
    if (JS_IsException(temporal_obj))
        goto fail;
    
    ret = JS_SetPropertyFunctionList(ctx, temporal_obj, js_temporal_funcs,
                                     countof(js_temporal_funcs));
    if (ret < 0)
        goto fail;
    
    ret = js_add_instant(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_duration(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_plaintime(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_now(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_plaindate(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_plaindatetime(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_plainyearmonth(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_plainmonthday(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = js_add_zoneddatetime(ctx, temporal_obj);
    if (ret < 0)
        goto fail;
    
    ret = JS_DefinePropertyValueStr(ctx, global_obj, "Temporal", temporal_obj,
                                    JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    if (ret < 0)
        goto fail;
    
    {
        JSValue date_ctor = JS_GetPropertyStr(ctx, global_obj, "Date");
        if (JS_IsException(date_ctor))
            goto fail;
        if (!JS_IsUndefined(date_ctor)) {
            JSValue date_proto = JS_GetPropertyStr(ctx, date_ctor, "prototype");
            if (JS_IsException(date_proto)) {
                JS_FreeValue(ctx, date_ctor);
                goto fail;
            }
            if (!JS_IsUndefined(date_proto)) {
                JSValue func = JS_NewCFunction(ctx, js_date_toTemporalInstant, "toTemporalInstant", 0);
                JS_DefinePropertyValueStr(ctx, date_proto, "toTemporalInstant", func,
                                          JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
                JS_FreeValue(ctx, date_proto);
            }
            JS_FreeValue(ctx, date_ctor);
        }
    }
    
    JS_FreeValue(ctx, global_obj);
    return 0;
    
fail:
    JS_FreeValue(ctx, temporal_obj);
    JS_FreeValue(ctx, global_obj);
    return -1;
}
int ti_is_minus_zero_year(const char *str)
{
    /* Reject extended year -000000 (minus zero) per ISO 8601 / Temporal spec */
    if (str[0] == '-' && str[1] == '0' && str[2] == '0' &&
        str[3] == '0' && str[4] == '0' && str[5] == '0' && str[6] == '0') {
        char c = str[7];
        if (c == '\0' || c == '-' || c == 'T')
            return 1;
    }
    return 0;
}
static int ti_is_valid_plain_offset_string(const char *str)
{
    size_t len = strlen(str);
    int h1, h2, m1, m2, s1, s2, i;
    
    if (len < 3 || len > 19)
        return 0;
    if (str[0] != '+' && str[0] != '-')
        return 0;

    if (len == 3) {
        /* ±HH */
        h1 = str[1] - '0';
        h2 = str[2] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9)
            return 0;
        if (h1 * 10 + h2 > 23)
            return 0;
        return 1;
    }
    
    if (len == 5) {
        /* ±HHMM */
        h1 = str[1] - '0';
        h2 = str[2] - '0';
        m1 = str[3] - '0';
        m2 = str[4] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9 || m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9)
            return 0;
        if (h1 * 10 + h2 > 23)
            return 0;
        return 1;
    }
    
    if (len == 6) {
        /* ±HH:MM */
        if (str[3] != ':')
            return 0;
        h1 = str[1] - '0';
        h2 = str[2] - '0';
        m1 = str[4] - '0';
        m2 = str[5] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9 || m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9)
            return 0;
        if (h1 * 10 + h2 > 23)
            return 0;
        return 1;
    }
    
    if (len >= 9) {
        /* ±HH:MM:SS or ±HH:MM:SS.fraction */
        if (str[3] != ':' || str[6] != ':')
            return 0;
        h1 = str[1] - '0';
        h2 = str[2] - '0';
        m1 = str[4] - '0';
        m2 = str[5] - '0';
        s1 = str[7] - '0';
        s2 = str[8] - '0';
        if (h1 < 0 || h1 > 9 || h2 < 0 || h2 > 9 || m1 < 0 || m1 > 5 || m2 < 0 || m2 > 9 ||
            s1 < 0 || s1 > 5 || s2 < 0 || s2 > 9)
            return 0;
        if (h1 * 10 + h2 > 23 || m1 * 10 + m2 > 59)
            return 0;
        if (len == 9)
            return 1;
        if (str[9] != '.')
            return 0;
        for (i = 10; i < (int)len; i++) {
            if (str[i] < '0' || str[i] > '9')
                return 0;
        }
        return 1;
    }
    
    return 0;
}
int ti_is_minus_zero(JSValueConst v)
{
    /* Check if JS value is -0 (negative zero float64) */
    if (JS_VALUE_GET_TAG(v) == JS_TAG_FLOAT64) {
        double d = JS_VALUE_GET_FLOAT64(v);
        if (d == 0.0) {
            union { double d; uint64_t u; } u;
            u.d = d;
            if (u.u == 0x8000000000000000ULL)
                return 1;
        }
    }
    return 0;
}
void ti_format_canonical_timezone(int64_t offset_ns, int is_utc, char *out, size_t out_size)
{
    if (is_utc) {
        pstrcpy(out, (int)out_size, "UTC");
    } else {
        int64_t abs_ns = llabs(offset_ns);
        int hours = (int)(abs_ns / (3600LL * 1000000000LL));
        int minutes = (int)((abs_ns % (3600LL * 1000000000LL)) / (60LL * 1000000000LL));
        snprintf(out, out_size, "%c%02d:%02d", (offset_ns >= 0) ? '+' : '-', hours, minutes);
    }
}

/* Append calendar annotation to a string buffer.
   calendarName: 0=auto, 1=always, 2=critical, 3=never.
   Appends when calendarName is always/critical, or when auto and calendar is not ISO8601. */
void ti_append_calendar_annotation(char *buf, int buf_size, CalendarType cal, int calendarName)
{
    const char *cal_str;
    if (calendarName == 3)
        return;
    if (calendarName == 0 && cal == CAL_ISO8601)
        return;
    cal_str = ti_calendar_enum_to_string(cal);
    if (!cal_str)
        return;
    if (calendarName == 2)
        pstrcat(buf, buf_size, "[!u-ca=");
    else
        pstrcat(buf, buf_size, "[u-ca=");
    pstrcat(buf, buf_size, cal_str);
    pstrcat(buf, buf_size, "]");
}

int ti_canonicalize_timezone_identifier(const char *str, int64_t offset_ns, char *out, size_t out_size)
{
    size_t len = strlen(str);
    const char *bracket_open = strchr(str, '[');
    if (bracket_open) {
        const char *bracket_close = strchr(bracket_open, ']');
        if (bracket_close && bracket_close[1] == '\0') {
            int inner_len = (int)(bracket_close - bracket_open - 1);
            if (inner_len > 0 && inner_len < (int)out_size) {
                memcpy(out, bracket_open + 1, (size_t)inner_len);
                out[inner_len] = '\0';
                if (out[0] == 'u' || out[0] == 'U') {
                    if ((out[1] == 't' || out[1] == 'T') &&
                        (out[2] == 'c' || out[2] == 'C') && out[3] == '\0')
                        pstrcpy(out, (int)out_size, "UTC");
                }
                return 0;
            }
        }
    } else {
        int is_utc = (len == 3 && (str[0] == 'U' || str[0] == 'u') &&
                      (str[1] == 'T' || str[1] == 't') &&
                      (str[2] == 'C' || str[2] == 'c'));
        if (!is_utc && len > 0 && (str[len - 1] == 'Z' || str[len - 1] == 'z'))
            is_utc = 1;
        if (!is_utc && offset_ns == 0 && len > 0 &&
            str[0] != '+' && str[0] != '-' && str[0] != 'Z' && str[0] != 'z') {
            /* Accept IANA time zone names */
            int is_iana = 1;
            size_t i;
            for (i = 0; i < len; i++) {
                char c = str[i];
                if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                      (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '+')) {
                    is_iana = 0;
                    break;
                }
            }
            if (is_iana) {
                pstrcpy(out, (int)out_size, str);
                return 0;
            }
            return -1;
        }
        ti_format_canonical_timezone(offset_ns, is_utc, out, out_size);
        if (out[0] == 'u' || out[0] == 'U') {
            if ((out[1] == 't' || out[1] == 'T') &&
                (out[2] == 'c' || out[2] == 'C') && out[3] == '\0')
                pstrcpy(out, (int)out_size, "UTC");
        }
        return 0;
    }
    return -1;
}
int ti_validate_constructor_calendar(JSContext *ctx, JSValueConst v, CalendarType *out)
{
    const char *cal;
    size_t len;
    char *lower;
    int i;

    if (JS_IsUndefined(v)) {
        *out = CAL_ISO8601;
        return 0;
    }

    if (!JS_IsString(v)) {
        JS_ThrowTypeError(ctx, "calendar must be a string");
        return -1;
    }

    cal = JS_ToCString(ctx, v);
    if (!cal)
        return -1;
    len = strlen(cal);
    if (len == 0) {
        JS_FreeCString(ctx, cal);
        return JS_ThrowRangeError(ctx, "invalid calendar ID"), -1;
    }

    lower = js_malloc(ctx, len + 1);
    if (!lower) {
        JS_FreeCString(ctx, cal);
        return -1;
    }
    for (i = 0; i < (int)len; i++) {
        char c = cal[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
    }
    lower[len] = '\0';

    if (!strcmp(lower, "ethiopic-amete-alem"))
        *out = CAL_ETHIOAA;
    else if (!strcmp(lower, "islamicc"))
        *out = CAL_ISLAMIC_CIVIL;
    else
        *out = ti_calendar_string_to_enum(lower);

    js_free(ctx, lower);
    JS_FreeCString(ctx, cal);

    if (*out == CAL_UNKNOWN)
        return JS_ThrowRangeError(ctx, "invalid calendar ID"), -1;
    return 0;
}
int ti_validate_timezone_identifier(JSContext *ctx, JSValueConst v, const char **out_str)
{
    const char *str;
    size_t len, i;

    if (JS_IsUndefined(v)) {
        *out_str = NULL;
        return 0;
    }
    if (!JS_IsString(v)) {
        JS_ThrowTypeError(ctx, "timeZone must be a string");
        return -1;
    }
    str = JS_ToCString(ctx, v);
    if (!str)
        return -1;
    len = strlen(str);
    if (len == 0) {
        JS_FreeCString(ctx, str);
        return JS_ThrowRangeError(ctx, "invalid time zone"), -1;
    }
    /* Canonicalize UTC */
    if (len == 3 &&
        (str[0] == 'U' || str[0] == 'u') &&
        (str[1] == 'T' || str[1] == 't') &&
        (str[2] == 'C' || str[2] == 'c')) {
        JS_FreeCString(ctx, str);
        {
            JSValue utc_val = JS_NewString(ctx, "UTC");
            *out_str = JS_ToCString(ctx, utc_val);
            JS_FreeValue(ctx, utc_val);
        }
        return 1;
    }
    /* Reject strings that look like ISO date-time strings.
       ISO date-time strings start with a digit and contain 'T' with digits/colons.
       Plain IANA names like Etc/GMT+1 may contain 't' and digits but don't start with digits. */
    if (len > 0 && (str[0] >= '0' && str[0] <= '9')) {
        for (i = 0; i < len; i++) {
            char c = str[i];
            if (c == 'T' || c == 't') {
                int has_digit_or_colon = 0;
                size_t j;
                for (j = 0; j < len; j++) {
                    char d = str[j];
                    if ((d >= '0' && d <= '9') || d == ':') {
                        has_digit_or_colon = 1;
                        break;
                    }
                }
                if (has_digit_or_colon) {
                    JS_FreeCString(ctx, str);
                    return JS_ThrowRangeError(ctx, "invalid time zone"), -1;
                }
                break;
            }
        }
    }
    /* Reject legacy non-IANA 3-letter abbreviations */
    if (len == 3 && str[0] >= 'A' && str[0] <= 'Z' &&
        str[1] >= 'A' && str[1] <= 'Z' && str[2] >= 'A' && str[2] <= 'Z') {
        static const char *legacy_non_iana[] = {
            "ACT", "AET", "AGT", "ART", "AST", "BET", "BST", "CAT",
            "CNT", "CST", "CTT", "EAT", "ECT", "IET", "IST", "JST",
            "MIT", "NET", "NST", "PLT", "PNT", "PRT", "PST", "SST",
            "VST"
        };
        int is_legacy = 0;
        size_t j;
        for (j = 0; j < sizeof(legacy_non_iana) / sizeof(legacy_non_iana[0]); j++) {
            if (!strcmp(str, legacy_non_iana[j])) {
                is_legacy = 1;
                break;
            }
        }
        if (is_legacy) {
            JS_FreeCString(ctx, str);
            return JS_ThrowRangeError(ctx, "invalid time zone"), -1;
        }
    }
    *out_str = str;
    return 1;
}
JSValue js_create_temporal_object(JSContext *ctx, JSValueConst new_target,
                                                 JSClassID class_id, void *opaque)
{
    
    JSValue obj = JS_CreateFromConstructor(ctx, new_target, class_id);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    JS_SetOpaque(obj, opaque);
    return obj;
}
int is_valid_epoch_timespec(const JSTimeSpec *ts)
{
    if (ts->secs > INSTANT_MAX_SECS || ts->secs < -INSTANT_MAX_SECS)
        return 0;
    if (ts->secs == INSTANT_MAX_SECS && ts->nsecs != 0)
        return 0;
    return 1;
}

JSValue ti_timespec_to_bigint(JSContext *ctx, const JSTimeSpec *ts)
{
    js_int128_t total_ns = (js_int128_t)ts->secs * 1000000000LL + ts->nsecs;
    return JS_NewBigInt128(ctx, (uint64_t)total_ns, (uint64_t)(total_ns >> 64));
}

int ti_bigint_to_timespec(JSContext *ctx, JSValueConst val, JSTimeSpec *ts)
{
    uint64_t low, high;
    int32_t nsecs;
    int ret;
    js_int128_t total_ns;

    ret = JS_ToBigInt128(ctx, &low, &high, val);
    if (ret < 0)
        return -1;
    if (ret > 0) {
        JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
        return -1;
    }
    total_ns = (js_int128_t)low + ((js_int128_t)(int64_t)high << 64);
    if (total_ns / 1000000000LL > INT64_MAX ||
        total_ns / 1000000000LL < INT64_MIN) {
        JS_ThrowRangeError(ctx, "epochNanoseconds out of range");
        return -1;
    }
    ts->secs = (int64_t)(total_ns / 1000000000LL);
    nsecs = (int32_t)(total_ns % 1000000000LL);
    if (nsecs < 0) {
        ts->secs -= 1;
        nsecs += 1000000000;
    }
    ts->nsecs = (uint32_t)nsecs;
    return 0;
}
