#include <string.h>
#include <stdint.h>
#include "timezone.h"
#include "../cutils.h"

static const char * const tz_aliases[][2] = {
    {"America/Atka", "America/Adak"},
    {"America/Knox_IN", "America/Indiana/Knox"},
    {"Asia/Ashkhabad", "Asia/Ashgabat"},
    {"Asia/Calcutta", "Asia/Kolkata"},
    {"Asia/Dacca", "Asia/Dhaka"},
    {"Asia/Istanbul", "Europe/Istanbul"},
    {"Asia/Macao", "Asia/Macau"},
    {"Asia/Saigon", "Asia/Ho_Chi_Minh"},
    {"Asia/Tel_Aviv", "Asia/Jerusalem"},
    {"Asia/Thimbu", "Asia/Thimphu"},
    {"Asia/Ujung_Pandang", "Asia/Makassar"},
    {"Asia/Ulan_Bator", "Asia/Ulaanbaatar"},
    {"Atlantic/Faeroe", "Atlantic/Faroe"},
    {"Australia/ACT", "Australia/Sydney"},
    {"Australia/Canberra", "Australia/Sydney"},
    {"Australia/LHI", "Australia/Lord_Howe"},
    {"Australia/North", "Australia/Darwin"},
    {"Australia/NSW", "Australia/Sydney"},
    {"Australia/Queensland", "Australia/Brisbane"},
    {"Australia/South", "Australia/Adelaide"},
    {"Australia/Tasmania", "Australia/Hobart"},
    {"Australia/Victoria", "Australia/Melbourne"},
    {"Australia/West", "Australia/Perth"},
    {"Australia/Yancowinna", "Australia/Broken_Hill"},
    {"Brazil/Acre", "America/Rio_Branco"},
    {"Brazil/DeNoronha", "America/Noronha"},
    {"Brazil/East", "America/Sao_Paulo"},
    {"Brazil/West", "America/Manaus"},
    {"Canada/Atlantic", "America/Halifax"},
    {"Canada/Central", "America/Winnipeg"},
    {"Canada/Eastern", "America/Toronto"},
    {"Canada/Mountain", "America/Edmonton"},
    {"Canada/Newfoundland", "America/St_Johns"},
    {"Canada/Pacific", "America/Vancouver"},
    {"Canada/Saskatchewan", "America/Regina"},
    {"Canada/Yukon", "America/Whitehorse"},
    {"Chile/Continental", "America/Santiago"},
    {"Chile/EasterIsland", "Pacific/Easter"},
    {"Cuba", "America/Havana"},
    {"Egypt", "Africa/Cairo"},
    {"Eire", "Europe/Dublin"},
    {"Europe/Belfast", "Europe/London"},
    {"Europe/Tiraspol", "Europe/Chisinau"},
    {"Hongkong", "Asia/Hong_Kong"},
    {"Iceland", "Atlantic/Reykjavik"},
    {"Iran", "Asia/Tehran"},
    {"Israel", "Asia/Jerusalem"},
    {"Jamaica", "America/Jamaica"},
    {"Japan", "Asia/Tokyo"},
    {"Kwajalein", "Pacific/Kwajalein"},
    {"Libya", "Africa/Tripoli"},
    {"Mexico/BajaNorte", "America/Tijuana"},
    {"Mexico/BajaSur", "America/Mazatlan"},
    {"Mexico/General", "America/Mexico_City"},
    {"NZ", "Pacific/Auckland"},
    {"NZ-CHAT", "Pacific/Chatham"},
    {"Navajo", "America/Denver"},
    {"Pacific/Samoa", "Pacific/Pago_Pago"},
    {"Poland", "Europe/Warsaw"},
    {"Portugal", "Europe/Lisbon"},
    {"PRC", "Asia/Shanghai"},
    {"ROC", "Asia/Taipei"},
    {"ROK", "Asia/Seoul"},
    {"Singapore", "Asia/Singapore"},
    {"Turkey", "Europe/Istanbul"},
    {"US/Alaska", "America/Anchorage"},
    {"US/Aleutian", "America/Adak"},
    {"US/Arizona", "America/Phoenix"},
    {"US/Central", "America/Chicago"},
    {"US/East-Indiana", "America/Indiana/Indianapolis"},
    {"US/Eastern", "America/New_York"},
    {"US/Hawaii", "Pacific/Honolulu"},
    {"US/Indiana-Starke", "America/Indiana/Knox"},
    {"US/Michigan", "America/Detroit"},
    {"US/Mountain", "America/Denver"},
    {"US/Pacific", "America/Los_Angeles"},
    {"US/Samoa", "Pacific/Pago_Pago"},
    {"W-SU", "Europe/Moscow"},
};

static const char *ti_tz_canonical_name(int tz_id) {
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return NULL;
    return ti_tz_zone_data[tz_id].canonical_name;
}

const char *ti_canonicalize_timezone_alias(const char *tz)
{
    int i;
    int n = (int)(sizeof(tz_aliases) / sizeof(tz_aliases[0]));
    for (i = 0; i < n; i++) {
        if (strcmp(tz, tz_aliases[i][0]) == 0)
            return tz_aliases[i][1];
        if (strcmp(tz, tz_aliases[i][1]) == 0)
            return tz_aliases[i][1];
    }
    int id = ti_tz_find_id(tz);
    if (id >= 0) {
        const char *canon = ti_tz_canonical_name(id);
        if (canon)
            return canon;
    }
    return tz;
}

BOOL ti_timezones_are_equal(const char *tz1, const char *tz2)
{
    if (strcmp(tz1, tz2) == 0)
        return TRUE;
    if (strcasecmp(tz1, tz2) == 0)
        return TRUE;
    const char *c1 = ti_canonicalize_timezone_alias(tz1);
    const char *c2 = ti_canonicalize_timezone_alias(tz2);
    return strcmp(c1, c2) == 0;
}

int ti_tz_find_id(const char *name) {
    int i;
    for (i = 0; i < ti_tz_alias_count; i++) {
        if (strcasecmp(name, ti_tz_aliases[i].alias) == 0)
            return ti_tz_aliases[i].canonical_id;
    }
    for (i = 0; i < ti_tz_zone_count; i++) {
        if (strcasecmp(name, ti_tz_zone_data[i].canonical_name) == 0)
            return i;
    }
    return -1;
}

int ti_tz_offset_at_sec(int tz_id, int64_t epoch_sec) {
    const TZData *tz;
    const TZTransition *tr;
    int n, lo, hi, mid;
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return 0;
    tz = &ti_tz_zone_data[tz_id];
    tr = tz->transitions;
    n = tz->transition_count;
    lo = 0;
    hi = n;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (tr[mid].start_sec <= epoch_sec)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo > 0)
        return tr[lo - 1].offset_sec;
    return 0;
}

int64_t ti_tz_next_transition_sec(int tz_id, int64_t epoch_sec) {
    const TZData *tz;
    const TZTransition *tr;
    int n, lo, hi, mid;
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return INT64_MIN;
    tz = &ti_tz_zone_data[tz_id];
    tr = tz->transitions;
    n = tz->transition_count;
    lo = 0;
    hi = n;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (tr[mid].start_sec <= epoch_sec)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < n)
        return tr[lo].start_sec;
    return INT64_MIN;
}

int64_t ti_tz_next_transition_at(int tz_id, int64_t epoch_sec, int epoch_nsec) {
    const TZData *tz;
    const TZTransition *tr;
    int n, lo, hi, mid;
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return INT64_MIN;
    tz = &ti_tz_zone_data[tz_id];
    tr = tz->transitions;
    n = tz->transition_count;
    lo = 0;
    hi = n;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (tr[mid].start_sec < epoch_sec || (tr[mid].start_sec == epoch_sec && epoch_nsec >= 0))
            lo = mid + 1;
        else
            hi = mid;
    }
    /* Skip no-op transitions where the offset doesn't change */
    while (lo < n && tr[lo].offset_sec == tr[lo - 1].offset_sec)
        lo++;
    if (lo < n)
        return tr[lo].start_sec;
    return INT64_MIN;
}

int64_t ti_tz_prev_transition_at(int tz_id, int64_t epoch_sec, int epoch_nsec) {
    const TZData *tz;
    const TZTransition *tr;
    int n, lo, hi, mid;
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return INT64_MIN;
    tz = &ti_tz_zone_data[tz_id];
    tr = tz->transitions;
    n = tz->transition_count;
    lo = 0;
    hi = n;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (tr[mid].start_sec < epoch_sec || (tr[mid].start_sec == epoch_sec && epoch_nsec > 0))
            lo = mid + 1;
        else
            hi = mid;
    }
    /* Skip no-op transitions where the offset doesn't change */
    {
        int i = lo - 1;
        while (i >= 1 && tr[i].offset_sec == tr[i - 1].offset_sec)
            i--;
        if (i >= 1)
            return tr[i].start_sec;
    }
    return INT64_MIN;
}

int ti_tz_prev_offset_at_sec(int tz_id, int64_t epoch_sec) {
    const TZData *tz;
    const TZTransition *tr;
    int n, lo, hi, mid;
    if (tz_id < 0 || tz_id >= ti_tz_zone_count)
        return 0;
    tz = &ti_tz_zone_data[tz_id];
    tr = tz->transitions;
    n = tz->transition_count;
    lo = 0;
    hi = n;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (tr[mid].start_sec <= epoch_sec)
            lo = mid + 1;
        else
            hi = mid;
    }
    /* lo-1 is the active transition, lo-2 is the previous one */
    if (lo > 1)
        return tr[lo - 2].offset_sec;
    return tr[0].offset_sec;
}

/* Convert local wall-clock time to UTC epoch for a named timezone.
   dis_idx: 0=earlier, 1=later, 2=compatible, 3=reject.
   Returns 0 on success, -1 if reject disambiguation throws (caller should throw RangeError).
   On success, *out_utc_sec and *out_offset_sec are set. */
int ti_tz_local_to_utc(int tz_id, int64_t local_sec, int dis_idx,
                        int64_t *out_utc_sec, int *out_offset_sec)
{
    int64_t guess_sec = local_sec;
    int iter;
    int converged = 0;
    int64_t utc1 = 0;
    int offset1 = 0;

    for (iter = 0; iter < 4; iter++) {
        int offset_sec = ti_tz_offset_at_sec(tz_id, guess_sec);
        int64_t new_sec = local_sec - offset_sec;
        if (new_sec == guess_sec) {
            utc1 = guess_sec;
            offset1 = offset_sec;
            converged = 1;
            break;
        }
        guess_sec = new_sec;
    }

    if (converged) {
        /* Check for ambiguity (overlap): another candidate with prev offset */
        int prev_offset = ti_tz_prev_offset_at_sec(tz_id, utc1);
        if (prev_offset != offset1) {
            int64_t cand2 = local_sec - prev_offset;
            if (cand2 != utc1 && ti_tz_offset_at_sec(tz_id, cand2) == prev_offset) {
                /* Two candidates - sort by UTC time */
                int64_t earlier_utc, later_utc;
                int earlier_offset, later_offset;
                if (cand2 < utc1) {
                    earlier_utc = cand2;
                    earlier_offset = prev_offset;
                    later_utc = utc1;
                    later_offset = offset1;
                } else {
                    earlier_utc = utc1;
                    earlier_offset = offset1;
                    later_utc = cand2;
                    later_offset = prev_offset;
                }
                if (dis_idx == 3) /* reject */
                    return -1;
                if (dis_idx == 1) { /* later */
                    *out_utc_sec = later_utc;
                    *out_offset_sec = later_offset;
                } else { /* compatible / earlier */
                    *out_utc_sec = earlier_utc;
                    *out_offset_sec = earlier_offset;
                }
                return 0;
            }
        }
        /* Single candidate */
        *out_utc_sec = utc1;
        *out_offset_sec = offset1;
        return 0;
    } else {
        /* Gap - no converged value */
        int64_t guess1 = guess_sec;
        int offset_guess1 = ti_tz_offset_at_sec(tz_id, guess1);
        int64_t guess2 = local_sec - offset_guess1;
        int offset_guess2 = ti_tz_offset_at_sec(tz_id, guess2);
        int64_t mapped_local1 = guess1 + offset_guess1;
        int64_t mapped_local2 = guess2 + offset_guess2;

        if (dis_idx == 3) /* reject */
            return -1;

        if (dis_idx == 0) { /* earlier: use candidate mapping to earlier local time */
            if (mapped_local1 < mapped_local2) {
                *out_utc_sec = guess1;
                *out_offset_sec = offset_guess1;
            } else {
                *out_utc_sec = guess2;
                *out_offset_sec = offset_guess2;
            }
        } else { /* compatible / later */
            if (mapped_local1 > mapped_local2) {
                *out_utc_sec = guess1;
                *out_offset_sec = offset_guess1;
            } else {
                *out_utc_sec = guess2;
                *out_offset_sec = offset_guess2;
            }
        }
        return 0;
    }
}

/* GetStartOfDay: return the UTC epoch for the start of a local day.
   Uses the spec GetStartOfDay algorithm:
   - Try earlier disambiguation for midnight local time
   - If midnight maps back correctly, that's the start of day
   - Otherwise (gap at midnight), return the next transition after (midnight - 1 day)
   Returns the UTC epoch, or INT64_MIN on error (should not happen for valid days). */
int64_t ti_get_start_of_day_sec(int tz_id, int64_t local_day_start_sec)
{
    int64_t utc_sec;
    int out_offset_sec;
    if (ti_tz_local_to_utc(tz_id, local_day_start_sec, 0, &utc_sec, &out_offset_sec) < 0)
        return INT64_MIN;
    if (utc_sec + ti_tz_offset_at_sec(tz_id, utc_sec) == local_day_start_sec)
        return utc_sec;
    /* Gap at midnight: return the next transition after (midnight - 1 day) */
    {
        int64_t next_trans = ti_tz_next_transition_sec(tz_id, local_day_start_sec - 86400LL);
        if (next_trans != INT64_MIN)
            return next_trans;
    }
    return utc_sec;
}
