#ifndef TIMEZONE_H
#define TIMEZONE_H

#include <stdint.h>
#include "../cutils.h"

typedef struct {
    int64_t start_sec;
    int32_t offset_sec;
} TZTransition;

typedef struct {
    const char *canonical_name;
    const TZTransition *transitions;
    int transition_count;
} TZData;

typedef struct {
    const char *alias;
    int canonical_id;
} TZAlias;

/* Generated data (in tzdata_generated.c) */
extern const int ti_tz_zone_count;
extern const TZData ti_tz_zone_data[];
extern const TZAlias ti_tz_aliases[];
extern const int ti_tz_alias_count;

/* Lookup functions */
int ti_tz_find_id(const char *name);
int ti_tz_offset_at_sec(int tz_id, int64_t epoch_sec);
int ti_tz_prev_offset_at_sec(int tz_id, int64_t epoch_sec);
int64_t ti_tz_next_transition_sec(int tz_id, int64_t epoch_sec);
int64_t ti_tz_next_transition_at(int tz_id, int64_t epoch_sec, int epoch_nsec);
int64_t ti_tz_prev_transition_at(int tz_id, int64_t epoch_sec, int epoch_nsec);


/* Timezone alias canonicalization and equality */
const char *ti_canonicalize_timezone_alias(const char *tz);
int ti_timezones_are_equal(const char *tz1, const char *tz2);

/* Convert local wall-clock time to UTC epoch for a named timezone.
   dis_idx: 0=earlier, 1=later, 2=compatible, 3=reject.
   Returns 0 on success, -1 if reject disambiguation throws (caller should throw RangeError).
   On success, *out_utc_sec and *out_offset_sec are set. */
int ti_tz_local_to_utc(int tz_id, int64_t local_sec, int dis_idx,
                        int64_t *out_utc_sec, int *out_offset_sec);
int64_t ti_get_start_of_day_sec(int tz_id, int64_t local_day_start_sec);

#endif
