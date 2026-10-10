/*
 * QuickJS Temporal calendar arithmetic and properties
 */

#include <string.h>
#include "../cutils.h"
#include "calendar.h"
#include "temporal_private.h"

const int ti_iso_month_days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

/* List of built-in calendar identifiers per spec AvailableCalendars().
   Order matches the CalendarType enum. */
const char *ti_calendar_names[] = {
    "buddhist", "chinese", "coptic", "dangi", "ethioaa", "ethiopic",
    "gregory", "hebrew", "indian", "islamic-civil", "islamic-tbla",
    "islamic-umalqura", "iso8601", "japanese", "persian", "roc"
};

CalendarType ti_calendar_string_to_enum(const char *str)
{
    int i;
    for (i = 0; i < CAL_COUNT; i++) {
        if (!strcmp(str, ti_calendar_names[i]))
            return i;
    }
    if (!strcmp(str, "ethiopic-amete-alem"))
        return CAL_ETHIOAA;
    if (!strcmp(str, "islamicc"))
        return CAL_ISLAMIC_CIVIL;
    return CAL_UNKNOWN;
}

const char *ti_calendar_enum_to_string(CalendarType cal)
{
    if (cal >= 0 && cal < CAL_COUNT)
        return ti_calendar_names[cal];
    return NULL;
}

/* Compressed Chinese/Dangi calendar data (3 bytes per entry) */
typedef struct {
    uint8_t data[3];
} ChineseCalendarEntry;

static inline int cce_cny_month(const ChineseCalendarEntry *e) {
    return (e->data[0] & 1) + 1;
}
static inline int cce_cny_day(const ChineseCalendarEntry *e) {
    return ((e->data[0] >> 1) & 0x1F) + 1;
}
static inline int cce_leap_month(const ChineseCalendarEntry *e) {
    return ((e->data[0] >> 6) & 3) | ((e->data[1] & 3) << 2);
}
static inline int cce_num_months(const ChineseCalendarEntry *e) {
    return cce_leap_month(e) ? 13 : 12;
}
static inline uint32_t cce_month_bitmap(const ChineseCalendarEntry *e) {
    return ((e->data[1] >> 2) & 0x3F) | ((uint32_t)(e->data[2] & 0x7F) << 6);
}
static inline int cce_month_length(const ChineseCalendarEntry *e, int month_idx) {
    return (cce_month_bitmap(e) & (1U << month_idx)) ? 30 : 29;
}

/* ============================================================================
   Chinese calendar data
   ============================================================================ */

static const ChineseCalendarEntry chinese_calendar_data[] = {
    {{48, 181, 42}}, {{25, 88, 43}}, {{5, 80, 23}}, {{172, 164, 54}}, {{21, 36, 59}}, 
    {{252, 41, 58}}, {{35, 44, 26}}, {{13, 156, 52}}, {{118, 89, 37}}, {{27, 108, 37}}, 
    {{7, 104, 43}}, {{240, 80, 91}}, {{25, 80, 29}}, {{1, 36, 29}}, {{168, 44, 90}}, 
    {{17, 76, 42}}, {{184, 173, 84}}, {{31, 180, 20}}, {{9, 180, 37}}, {{50, 169, 45}}, 
    {{27, 168, 54}}, {{5, 144, 46}}, {{236, 20, 109}}, {{21, 36, 53}}, {{252, 85, 106}}, 
    {{35, 84, 42}}, {{13, 180, 20}}, {{116, 181, 42}}, {{29, 212, 42}}, {{7, 168, 54}}, 
    {{48, 145, 118}}, {{25, 144, 58}}, {{67, 42, 117}}, {{39, 40, 53}}, {{17, 88, 42}}, 
    {{184, 217, 84}}, {{33, 104, 21}}, {{9, 84, 43}}, {{50, 41, 91}}, {{27, 72, 29}}, 
    {{5, 148, 58}}, {{236, 40, 53}}, {{19, 44, 25}}, {{250, 93, 42}}, {{35, 172, 42}}, 
    {{13, 104, 21}}, {{116, 85, 45}}, {{29, 164, 46}}, {{9, 72, 29}}, {{46, 149, 108}}, 
    {{23, 148, 44}}, {{1, 46, 105}}, {{39, 52, 37}}, {{15, 180, 42}}, {{248, 169, 85}}, 
    {{33, 208, 22}}, {{11, 164, 54}}, {{114, 73, 117}}, {{27, 72, 54}}, {{5, 148, 52}}, 
    {{236, 52, 105}}, {{19, 88, 41}}, {{58, 214, 74}}, {{35, 88, 43}}, {{15, 80, 27}}, 
    {{116, 165, 54}}, {{29, 36, 59}}, {{9, 72, 58}}, {{48, 153, 52}}, {{21, 172, 20}}, 
    {{188, 94, 41}}, {{37, 108, 37}}, {{17, 104, 45}}, {{184, 81, 91}}, {{33, 80, 29}}, 
    {{11, 36, 29}}, {{114, 77, 90}}, {{25, 76, 42}}, {{3, 172, 20}}, {{234, 108, 41}}, 
    {{19, 180, 41}}, {{250, 169, 45}}, {{35, 168, 54}}, {{15, 144, 46}}, {{118, 37, 109}}, 
    {{29, 36, 53}}, {{7, 84, 42}}, {{46, 181, 84}}, {{23, 180, 20}}, {{60, 180, 42}}, 
    {{168, 168, 85}}, {{17, 168, 54}}, {{186, 145, 118}}, {{33, 144, 58}}, {{11, 40, 53}}, 
    {{114, 85, 42}}, {{25, 92, 42}}, {{3, 88, 21}}, {{234, 212, 42}}, {{19, 84, 43}}, 
    {{60, 74, 91}}, {{37, 72, 29}}, {{15, 148, 58}}, {{120, 41, 89}}, {{31, 44, 25}}, 
    {{7, 108, 42}}, {{48, 105, 85}}, {{25, 168, 21}}, {{3, 100, 45}}, {{170, 72, 93}}, 
    {{19, 72, 29}}, {{186, 149, 108}}, {{35, 148, 44}}, {{11, 44, 41}}, {{114, 173, 82}}, 
    {{27, 180, 10}}, {{5, 172, 21}}, {{172, 164, 45}}, {{21, 164, 54}}, {{193, 73, 118}}, 
    {{39, 72, 58}}, {{15, 148, 52}}, {{118, 53, 105}}, {{31, 88, 41}}, {{9, 216, 10}}, 
    {{46, 213, 86}}, {{25, 80, 27}}, {{3, 164, 58}}, {{172, 72, 122}}, {{19, 72, 58}}, 
    {{186, 153, 52}}, {{33, 172, 20}}, {{11, 92, 41}}, {{114, 217, 74}}, {{27, 104, 45}}, 
    {{7, 80, 27}}, {{238, 36, 59}}, {{21, 36, 29}}, {{252, 77, 90}}, {{37, 76, 42}}, 
    {{15, 172, 20}}, {{180, 109, 41}}, {{29, 180, 42}}, {{9, 168, 21}}, {{48, 85, 109}}, 
    {{25, 144, 46}}, {{3, 36, 45}}, {{170, 76, 106}}, {{19, 84, 42}}, {{248, 181, 84}}, 
    {{33, 216, 20}}, {{11, 180, 42}}, {{116, 169, 86}}, {{27, 200, 22}}, {{5, 148, 54}}, 
    {{238, 40, 117}}, {{23, 40, 53}}, {{60, 86, 42}}, {{35, 92, 42}}, {{15, 88, 21}}, 
    {{182, 213, 42}}, {{29, 84, 43}}, {{9, 72, 27}}, {{48, 149, 58}}, {{25, 148, 58}}, 
    {{3, 40, 25}}, {{232, 92, 50}}, {{17, 108, 42}}, {{250, 105, 85}}, {{33, 168, 21}}, 
    {{11, 164, 45}}, {{116, 73, 93}}, {{29, 72, 45}}, {{5, 148, 44}}, {{44, 45, 89}}, 
    {{21, 44, 41}}, {{60, 174, 82}}, {{35, 180, 10}}, {{13, 180, 21}}, {{182, 165, 45}}, 
    {{31, 164, 54}}, {{9, 72, 54}}, {{48, 149, 116}}, {{25, 148, 52}}, {{131, 54, 105}}, 
    {{39, 88, 41}}, {{17, 216, 10}}, {{184, 213, 22}}, {{33, 84, 27}}, {{11, 164, 58}}, 
    {{116, 73, 122}}, {{29, 72, 58}}, {{7, 152, 52}}, {{236, 88, 41}}, {{19, 92, 41}}, 
    {{60, 90, 83}}, {{37, 104, 13}}, {{13, 84, 27}}, {{118, 37, 91}}, {{31, 36, 29}}, 
    {{9, 76, 26}}, {{46, 173, 84}}, {{23, 172, 20}}, {{1, 108, 41}}, {{170, 104, 85}}, 
    {{17, 168, 21}}, {{248, 85, 109}}, {{35, 144, 46}}, {{13, 36, 45}}, {{114, 77, 106}}, 
    {{27, 84, 42}}, {{5, 180, 20}}, {{44, 181, 42}}, {{19, 212, 42}}, {{124, 170, 86}}, 
    {{37, 72, 23}}, {{15, 148, 54}}, {{182, 41, 117}}, {{31, 40, 53}}, {{9, 84, 50}}, 
    {{48, 185, 84}}, {{23, 88, 21}}, {{1, 212, 42}}, {{170, 200, 86}}, {{19, 72, 27}}, 
    {{184, 149, 58}}, {{33, 148, 28}}, {{11, 44, 25}}, {{114, 93, 50}}, {{25, 172, 50}}, 
    {{5, 104, 21}}, {{236, 88, 43}}, {{21, 164, 45}}, {{252, 74, 93}}, {{37, 72, 45}}, 
    {{15, 148, 44}}, {{182, 45, 105}}, {{29, 44, 41}}, {{7, 172, 18}}, {{110, 109, 21}}, 
    {{23, 180, 22}}, {{1, 168, 45}}, {{170, 72, 109}}, {{19, 72, 54}}, {{250, 149, 116}}, 
    {{33, 148, 52}}, {{11, 84, 41}}, {{114, 181, 82}}, {{27, 216, 18}}, {{3, 212, 22}}, 
    {{236, 168, 54}}, {{21, 36, 59}}, {{1, 74, 122}}, {{37, 72, 58}}, {{15, 152, 52}}, 
    {{182, 89, 41}}, {{29, 92, 41}}, {{7, 88, 21}}, {{46, 85, 27}}, {{23, 84, 29}}, {{3, 36, 29}}, 
    {{232, 76, 58}}, {{17, 76, 26}}, {{248, 173, 84}}, {{33, 172, 20}}, {{9, 108, 41}}, 
    {{114, 105, 85}}, {{27, 168, 21}}, {{5, 148, 45}}, {{44, 41, 93}}, {{21, 40, 45}}, 
    {{60, 86, 106}}, {{37, 84, 42}}, {{13, 180, 20}}, {{180, 181, 42}}, {{29, 212, 42}}, 
    {{9, 168, 22}}, {{46, 149, 46}}, {{23, 148, 54}}, {{3, 40, 53}}, {{234, 84, 114}}, 
    {{17, 88, 50}}, {{248, 57, 101}}, {{33, 88, 21}}, {{11, 212, 42}}, {{114, 201, 86}}, 
    {{27, 72, 27}}, {{5, 148, 58}}, {{46, 41, 57}}, {{19, 44, 26}}, {{58, 94, 50}}, 
    {{35, 172, 18}}, {{13, 108, 21}}, {{180, 89, 43}}, {{29, 168, 45}}, {{9, 72, 29}}, 
    {{48, 149, 92}}, {{23, 20, 45}}, {{1, 44, 42}}, {{168, 108, 82}}, {{17, 172, 18}}, 
    {{248, 109, 37}}, {{33, 180, 22}}, {{13, 168, 46}}, {{118, 73, 109}}, {{29, 72, 54}}, 
    {{7, 148, 52}}, {{46, 45, 105}}, {{23, 84, 41}}, {{124, 182, 82}}, {{37, 216, 18}}, 
    {{15, 212, 26}}, {{184, 169, 54}}, {{31, 36, 59}}, {{11, 72, 58}}, {{50, 153, 116}}, 
    {{27, 168, 52}}, {{3, 88, 41}}, {{234, 216, 82}}, {{19, 88, 21}}, {{250, 85, 43}}, 
    {{33, 84, 45}}, {{13, 40, 29}}, {{116, 77, 58}}, {{29, 84, 26}}, {{5, 172, 20}}, 
    {{44, 93, 41}}, {{21, 108, 42}}, {{193, 106, 101}}, {{37, 168, 21}}, {{15, 148, 45}}, 
    {{184, 41, 93}}, {{33, 40, 45}}, {{9, 84, 44}}, {{112, 173, 84}}, {{25, 52, 21}}, 
    {{3, 180, 42}}, {{170, 168, 85}}, {{19, 168, 22}}, {{250, 149, 46}}, {{35, 148, 54}}, 
    {{13, 40, 53}}, {{116, 85, 116}}, {{29, 88, 52}}, {{7, 56, 37}}, {{44, 181, 42}}, 
    {{21, 88, 43}}, {{193, 210, 102}}, {{39, 72, 27}}, {{15, 148, 58}}, {{184, 41, 58}}, 
    {{31, 44, 26}}, {{9, 92, 52}}, {{112, 89, 37}}, {{23, 108, 37}}, {{3, 104, 43}}, 
    {{236, 80, 91}}, {{19, 80, 29}}, {{250, 21, 93}}, {{35, 20, 45}}, {{13, 44, 42}}, 
    {{178, 173, 84}}, {{27, 180, 18}}, {{5, 172, 37}}, {{46, 105, 45}}, {{21, 168, 54}}, 
    {{129, 82, 109}}, {{39, 136, 54}}, {{17, 20, 53}}, {{182, 85, 106}}, {{31, 84, 42}}, 
    {{9, 180, 20}}, {{112, 181, 42}}, {{23, 212, 42}}, {{3, 168, 54}}, {{236, 144, 118}}, 
    {{21, 136, 58}}, {{250, 25, 117}}, {{35, 40, 53}}, {{13, 88, 42}}, {{180, 217, 84}}, 
    {{27, 104, 21}}, {{5, 84, 43}}, {{46, 41, 91}}, {{23, 72, 29}}, {{60, 148, 58}}, 
    {{168, 40, 53}}, {{15, 44, 21}}, {{182, 93, 42}}, {{29, 172, 42}}, {{9, 104, 21}}, 
    {{112, 85, 43}}, {{25, 148, 45}}, {{3, 72, 29}}, {{234, 148, 106}}, {{19, 148, 44}}, 
    {{250, 45, 105}}, {{33, 52, 37}}, {{11, 180, 42}}, {{180, 169, 85}}, {{29, 208, 22}}
};

/* Look up Chinese calendar data for a given Gregorian year (which contains CNY).
   Returns NULL if out of range. */
static const ChineseCalendarEntry *lookup_chinese_year(int year)
{
    int idx = year - 1800;
    if (idx < 0 || idx >= (int)(sizeof(chinese_calendar_data) / sizeof(chinese_calendar_data[0])))
        return NULL;
    return &chinese_calendar_data[idx];
}

/* ============================================================================
   Dangi (Korean lunisolar) calendar data
   ============================================================================ */

static const ChineseCalendarEntry dangi_calendar_data[] = {
    {{112, 181, 42}}, {{25, 88, 43}}, {{5, 80, 23}}, {{172, 164, 54}}, {{21, 148, 58}}, 
    {{252, 41, 58}}, {{35, 44, 26}}, {{13, 156, 20}}, {{116, 93, 37}}, {{27, 108, 37}}, 
    {{7, 104, 43}}, {{240, 80, 91}}, {{25, 80, 29}}, {{193, 22, 93}}, {{39, 20, 45}}, 
    {{17, 44, 42}}, {{184, 173, 84}}, {{31, 180, 18}}, {{9, 180, 37}}, {{50, 169, 45}}, 
    {{27, 168, 46}}, {{5, 80, 46}}, {{236, 20, 109}}, {{21, 20, 53}}, {{252, 85, 106}}, 
    {{35, 84, 42}}, {{13, 180, 20}}, {{180, 181, 42}}, {{29, 212, 42}}, {{7, 168, 54}}, 
    {{48, 145, 118}}, {{25, 136, 58}}, {{67, 42, 117}}, {{39, 40, 53}}, {{17, 88, 42}}, 
    {{184, 217, 84}}, {{33, 104, 21}}, {{9, 84, 43}}, {{50, 41, 91}}, {{27, 72, 29}}, 
    {{5, 148, 58}}, {{236, 40, 85}}, {{19, 44, 21}}, {{250, 93, 42}}, {{35, 172, 42}}, 
    {{13, 104, 21}}, {{116, 85, 45}}, {{29, 164, 46}}, {{9, 72, 29}}, {{46, 149, 106}}, 
    {{23, 148, 44}}, {{1, 46, 105}}, {{39, 52, 37}}, {{15, 180, 42}}, {{248, 169, 85}}, 
    {{33, 208, 22}}, {{11, 164, 46}}, {{114, 73, 117}}, {{27, 72, 54}}, {{5, 148, 52}}, 
    {{236, 52, 105}}, {{19, 88, 37}}, {{58, 214, 74}}, {{35, 88, 43}}, {{15, 80, 27}}, 
    {{116, 165, 54}}, {{29, 36, 59}}, {{9, 72, 58}}, {{48, 153, 52}}, {{21, 156, 20}}, 
    {{188, 94, 41}}, {{37, 108, 37}}, {{17, 104, 45}}, {{184, 81, 91}}, {{33, 80, 29}}, 
    {{11, 36, 29}}, {{114, 77, 90}}, {{25, 76, 42}}, {{3, 172, 20}}, {{234, 108, 41}}, 
    {{19, 180, 37}}, {{250, 169, 45}}, {{35, 168, 54}}, {{15, 144, 46}}, {{118, 37, 109}}, 
    {{29, 36, 53}}, {{7, 84, 42}}, {{46, 173, 84}}, {{23, 180, 20}}, {{60, 183, 42}}, 
    {{37, 212, 42}}, {{17, 168, 54}}, {{186, 145, 118}}, {{33, 144, 58}}, {{11, 40, 53}}, 
    {{114, 85, 106}}, {{27, 88, 42}}, {{3, 88, 21}}, {{234, 212, 42}}, {{19, 84, 43}}, 
    {{60, 74, 91}}, {{37, 72, 29}}, {{15, 148, 58}}, {{120, 41, 89}}, {{31, 44, 25}}, 
    {{7, 108, 42}}, {{48, 89, 85}}, {{25, 168, 21}}, {{3, 100, 45}}, {{170, 72, 93}}, 
    {{19, 72, 29}}, {{186, 149, 108}}, {{35, 148, 44}}, {{11, 44, 41}}, {{114, 109, 74}}, 
    {{27, 180, 42}}, {{7, 168, 21}}, {{172, 164, 45}}, {{21, 164, 46}}, {{193, 73, 109}}, 
    {{39, 72, 54}}, {{15, 148, 52}}, {{118, 53, 105}}, {{31, 88, 37}}, {{9, 212, 10}}, 
    {{46, 181, 86}}, {{25, 80, 27}}, {{3, 164, 54}}, {{172, 72, 118}}, {{19, 72, 58}}, 
    {{186, 153, 52}}, {{33, 156, 20}}, {{11, 92, 41}}, {{114, 217, 74}}, {{27, 104, 43}}, 
    {{7, 80, 27}}, {{238, 164, 58}}, {{21, 36, 29}}, {{252, 77, 90}}, {{37, 76, 42}}, 
    {{15, 172, 20}}, {{180, 109, 41}}, {{29, 180, 37}}, {{9, 168, 45}}, {{50, 81, 109}}, 
    {{25, 144, 46}}, {{3, 36, 45}}, {{170, 76, 106}}, {{19, 84, 42}}, {{248, 173, 84}}, 
    {{33, 180, 20}}, {{11, 180, 42}}, {{116, 169, 85}}, {{27, 200, 54}}, {{7, 144, 54}}, 
    {{238, 36, 117}}, {{23, 40, 53}}, {{60, 86, 106}}, {{37, 88, 42}}, {{15, 88, 21}}, 
    {{182, 213, 42}}, {{29, 84, 43}}, {{9, 72, 27}}, {{48, 149, 58}}, {{25, 148, 58}}, 
    {{3, 40, 57}}, {{234, 88, 50}}, {{17, 108, 42}}, {{250, 89, 85}}, {{33, 168, 21}}, 
    {{11, 100, 45}}, {{116, 73, 93}}, {{29, 72, 29}}, {{5, 148, 28}}, {{44, 45, 89}}, 
    {{21, 44, 41}}, {{60, 174, 74}}, {{35, 180, 10}}, {{13, 172, 21}}, {{182, 165, 45}}, 
    {{31, 164, 54}}, {{9, 72, 54}}, {{48, 149, 108}}, {{25, 148, 52}}, {{131, 54, 105}}, 
    {{39, 88, 41}}, {{17, 216, 10}}, {{184, 181, 86}}, {{35, 80, 27}}, {{11, 164, 54}}, 
    {{116, 73, 118}}, {{29, 72, 58}}, {{7, 152, 52}}, {{236, 88, 41}}, {{19, 92, 41}}, 
    {{60, 218, 74}}, {{37, 104, 45}}, {{15, 80, 27}}, {{118, 37, 59}}, {{31, 36, 29}}, 
    {{9, 76, 26}}, {{46, 157, 84}}, {{23, 172, 20}}, {{1, 108, 41}}, {{170, 104, 85}}, 
    {{17, 168, 13}}, {{248, 85, 109}}, {{35, 144, 46}}, {{13, 36, 45}}, {{114, 77, 106}}, 
    {{27, 84, 42}}, {{5, 180, 20}}, {{236, 116, 41}}, {{19, 180, 42}}, {{124, 170, 86}}, 
    {{37, 72, 23}}, {{15, 148, 54}}, {{118, 37, 117}}, {{31, 40, 53}}, {{9, 84, 42}}, 
    {{48, 181, 84}}, {{23, 88, 21}}, {{1, 212, 42}}, {{170, 168, 86}}, {{19, 72, 27}}, 
    {{184, 149, 58}}, {{33, 148, 58}}, {{13, 40, 57}}, {{116, 89, 50}}, {{25, 108, 50}}, 
    {{5, 104, 21}}, {{236, 84, 43}}, {{21, 164, 45}}, {{252, 74, 93}}, {{37, 72, 29}}, 
    {{15, 148, 44}}, {{182, 45, 89}}, {{29, 44, 41}}, {{7, 172, 18}}, {{110, 109, 21}}, 
    {{23, 180, 21}}, {{1, 164, 45}}, {{170, 72, 109}}, {{19, 72, 54}}, {{250, 149, 116}}, 
    {{33, 148, 52}}, {{11, 52, 41}}, {{114, 181, 82}}, {{27, 216, 10}}, {{3, 212, 22}}, 
    {{236, 164, 54}}, {{21, 36, 55}}, {{1, 74, 118}}, {{37, 72, 58}}, {{15, 152, 52}}, 
    {{182, 89, 41}}, {{29, 92, 41}}, {{7, 88, 19}}, {{46, 213, 26}}, {{23, 84, 27}}, {{3, 36, 59}}, 
    {{234, 72, 58}}, {{17, 76, 26}}, {{248, 173, 84}}, {{33, 172, 20}}, {{9, 108, 41}}, 
    {{114, 105, 85}}, {{27, 168, 21}}, {{5, 84, 45}}, {{44, 37, 93}}, {{21, 36, 45}}, 
    {{60, 78, 106}}, {{37, 84, 42}}, {{13, 180, 20}}, {{180, 181, 42}}, {{29, 212, 42}}, 
    {{9, 168, 22}}, {{46, 149, 46}}, {{23, 148, 54}}, {{3, 40, 53}}, {{234, 84, 106}}, 
    {{17, 84, 50}}, {{248, 185, 84}}, {{33, 88, 21}}, {{11, 212, 42}}, {{114, 201, 86}}, 
    {{27, 72, 27}}, {{5, 148, 58}}, {{46, 41, 121}}, {{21, 40, 25}}, {{58, 94, 50}}, 
    {{35, 172, 50}}, {{15, 104, 21}}, {{180, 85, 43}}, {{29, 164, 45}}, {{9, 72, 29}}, 
    {{48, 149, 58}}, {{23, 148, 44}}, {{1, 44, 25}}, {{232, 92, 82}}, {{17, 172, 18}}, 
    {{248, 109, 21}}, {{33, 180, 22}}, {{13, 164, 45}}, {{118, 73, 109}}, {{29, 72, 54}}, 
    {{7, 148, 52}}, {{46, 45, 105}}, {{23, 84, 41}}, {{124, 182, 82}}, {{37, 216, 18}}, 
    {{15, 212, 22}}, {{184, 169, 54}}, {{31, 36, 59}}, {{11, 72, 58}}, {{50, 149, 116}}, 
    {{27, 152, 52}}, {{3, 88, 41}}, {{234, 184, 82}}, {{19, 88, 19}}, {{250, 85, 27}}, 
    {{33, 84, 27}}, {{13, 36, 59}}, {{118, 73, 58}}, {{29, 76, 26}}, {{5, 172, 20}}, 
    {{44, 93, 41}}, {{21, 108, 41}}, {{193, 106, 85}}, {{37, 168, 21}}, {{15, 148, 45}}, 
    {{184, 41, 93}}, {{33, 36, 45}}, {{9, 84, 42}}, {{112, 173, 84}}, {{25, 180, 20}}, 
    {{3, 180, 42}}, {{170, 168, 85}}, {{19, 168, 22}}, {{250, 149, 46}}, {{35, 148, 54}}, 
    {{13, 40, 53}}, {{116, 85, 114}}, {{29, 88, 50}}, {{7, 56, 37}}, {{44, 181, 42}}, 
    {{21, 212, 42}}, {{1, 200, 22}}, {{104, 148, 54}}, {{15, 148, 58}}, {{184, 41, 121}}, 
    {{33, 40, 26}}, {{9, 92, 50}}, {{112, 89, 37}}, {{23, 108, 21}}, {{3, 88, 43}}, 
    {{236, 80, 91}}, {{19, 72, 29}}, {{250, 149, 92}}, {{35, 20, 45}}, {{13, 44, 26}}, 
    {{178, 109, 82}}, {{27, 172, 18}}, {{5, 108, 37}}, {{46, 105, 45}}, {{21, 168, 46}}, 
    {{129, 74, 109}}, {{39, 136, 54}}, {{17, 20, 53}}, {{182, 45, 106}}, {{31, 84, 41}}, 
    {{9, 180, 18}}, {{48, 181, 37}}, {{23, 212, 22}}, {{3, 168, 54}}, {{236, 72, 118}}, 
    {{21, 136, 58}}, {{250, 21, 117}}, {{35, 168, 52}}, {{13, 88, 41}}, {{180, 217, 82}}, 
    {{27, 88, 21}}, {{5, 84, 43}}, {{46, 169, 54}}, {{23, 40, 29}}, {{60, 140, 58}}, 
    {{168, 24, 53}}, {{15, 172, 20}}, {{246, 93, 41}}, {{29, 108, 42}}, {{9, 104, 21}}, 
    {{112, 85, 43}}, {{25, 148, 45}}, {{3, 40, 29}}, {{234, 148, 90}}, {{19, 84, 42}}, 
    {{250, 173, 84}}, {{33, 52, 21}}, {{11, 180, 42}}, {{116, 169, 85}}, {{29, 168, 22}}
};

static const ChineseCalendarEntry *lookup_dangi_year(int year)
{
    int idx = year - 1800;
    if (idx < 0 || idx >= (int)(sizeof(dangi_calendar_data) / sizeof(dangi_calendar_data[0])))
        return NULL;
    return &dangi_calendar_data[idx];
}

/* Return the related Gregorian year for a Chinese/Dangi date.
   Returns iso_year if on/after Chinese New Year, iso_year - 1 otherwise.
   Falls back to iso_year if the year is not in the calendar data. */
int ti_chinese_related_year(CalendarType cal, int iso_year, int iso_month, int iso_day)
{
    const ChineseCalendarEntry *e;
    if (cal == CAL_DANGI)
        e = lookup_dangi_year(iso_year);
    else
        e = lookup_chinese_year(iso_year);
    if (e && (iso_month < cce_cny_month(e) ||
              (iso_month == cce_cny_month(e) && iso_day < cce_cny_day(e)))) {
        return iso_year - 1;
    }
    return iso_year;
}

/* ===== Chinese/Dangi calendar properties ===== */

int64_t pdt_to_ordinal_days(int year, int month, int day)
{
    int64_t y = (int64_t)year - 1;
    int64_t days = y * 365 + floor_div64(y, 4) - floor_div64(y, 100) + floor_div64(y, 400);
    int i;
    for (i = 1; i < month; i++)
        days += ti_iso_month_days[i];
    if (month > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)))
        days++;
    days += day - 1;
    return days;
}

/* Inverse of pdt_to_ordinal_days: convert ordinal day count (from 0001-01-01 = 0)
   back to year, month, day. Uses 400/100/4/1 year cycles for direct computation. */
void pdt_from_ordinal_days(int64_t ordinal, int *year, int *month, int *day)
{
    int64_t r400, r100, r4, r1;
    int64_t n400 = floor_divrem64(&r400, ordinal, 146097);
    int64_t n100 = floor_divrem64(&r100, r400, 36524);
    int64_t n4, n1;
    int64_t y;
    int doy, i;

    if (n100 == 4) { n100 = 3; r100 = 36524; }
    y = 1 + n400 * 400 + n100 * 100;

    n4 = floor_divrem64(&r4, r100, 1461);
    y += n4 * 4;

    n1 = floor_divrem64(&r1, r4, 365);
    if (n1 == 4) { n1 = 3; r1 = 365; }
    y += n1;

    doy = (int)r1;
    for (i = 1; i <= 12; i++) {
        int dim = ti_iso_month_days[i];
        if (i == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)))
            dim = 29;
        if (doy < dim) {
            *year = (int)y;
            *month = i;
            *day = doy + 1;
            return;
        }
        doy -= dim;
    }
    /* Should not reach here for valid ordinal */
    *year = (int)y;
    *month = 12;
    *day = 31;
}



static int ti_calendar_is_leap_year_chinese(int year)
{
    const ChineseCalendarEntry *entry = lookup_chinese_year(year);
    if (!entry)
        return 0;
    return cce_num_months(entry) == 13;
}

static int ti_calendar_days_in_year_chinese(int year)
{
    const ChineseCalendarEntry *entry = lookup_chinese_year(year);
    int i, days = 0;
    if (!entry)
        return 365;
    for (i = 0; i < cce_num_months(entry); i++)
        days += cce_month_length(entry, i);
    return days;
}

static int ti_calendar_months_in_year_chinese(int year)
{
    const ChineseCalendarEntry *entry = lookup_chinese_year(year);
    if (!entry)
        return 12;
    return cce_num_months(entry);
}

static int ti_calendar_days_in_month_chinese(int year, int month)
{
    const ChineseCalendarEntry *entry = lookup_chinese_year(year);
    if (!entry || month < 1 || month > cce_num_months(entry))
        return 30;
    return cce_month_length(entry, month - 1);
}

static int ti_calendar_is_leap_year_dangi(int year)
{
    const ChineseCalendarEntry *entry = lookup_dangi_year(year);
    if (!entry)
        return 0;
    return cce_num_months(entry) == 13;
}

static int ti_calendar_days_in_year_dangi(int year)
{
    const ChineseCalendarEntry *entry = lookup_dangi_year(year);
    int i, days = 0;
    if (!entry)
        return 365;
    for (i = 0; i < cce_num_months(entry); i++)
        days += cce_month_length(entry, i);
    return days;
}

static int ti_calendar_months_in_year_dangi(int year)
{
    const ChineseCalendarEntry *entry = lookup_dangi_year(year);
    if (!entry)
        return 12;
    return cce_num_months(entry);
}

static int ti_calendar_days_in_month_dangi(int year, int month)
{
    const ChineseCalendarEntry *entry = lookup_dangi_year(year);
    if (!entry || month < 1 || month > cce_num_months(entry))
        return 30;
    return cce_month_length(entry, month - 1);
}

/* ===== Coptic / Ethiopic / Ethioaa ===== */

static int coptic_ethiopic_is_leap(int year)
{
    int mod = year % 4;
    if (mod < 0) mod += 4;
    return mod == 3;
}

static int coptic_ethiopic_days_in_year(int year)
{
    return coptic_ethiopic_is_leap(year) ? 366 : 365;
}

static int coptic_ethiopic_months_in_year(int year)
{
    (void)year;
    return 13;
}

static int coptic_ethiopic_days_in_month(int year, int month)
{
    if (month < 1 || month > 13)
        return 30;
    if (month < 13)
        return 30;
    return coptic_ethiopic_is_leap(year) ? 6 : 5;
}

/* ===== Persian ===== */

static int persian_is_leap(int year)
{
    int64_t rem = (25LL * year + 11) % 33;
    if (rem < 0)
        rem += 33;
    return rem < 8;
}

static int persian_days_in_year(int year)
{
    return persian_is_leap(year) ? 366 : 365;
}

static int persian_months_in_year(int year)
{
    (void)year;
    return 12;
}

static int persian_days_in_month(int year, int month)
{
    static const int days[] = {31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29};
    if (month < 1 || month > 12)
        return 30;
    if (month == 12 && persian_is_leap(year))
        return 30;
    return days[month - 1];
}

/* ===== Islamic civil / tbla ===== */

static int islamic_civil_is_leap(int year)
{
    int rem = year % 30;
    if (rem < 0)
        rem += 30;
    return rem == 2 || rem == 5 || rem == 7 || rem == 10 || rem == 13 ||
           rem == 16 || rem == 18 || rem == 21 || rem == 24 || rem == 26 ||
           rem == 29;
}

static int islamic_civil_days_in_year(int year)
{
    return islamic_civil_is_leap(year) ? 355 : 354;
}

static int islamic_civil_months_in_year(int year)
{
    (void)year;
    return 12;
}

static int islamic_civil_days_in_month(int year, int month)
{
    if (month < 1 || month > 12)
        return 30;
    if (month == 12)
        return islamic_civil_is_leap(year) ? 30 : 29;
    return (month % 2 == 1) ? 30 : 29;
}

/* ===== Indian ===== */

static int indian_is_leap(int year)
{
    int mod = year % 4;
    if (mod < 0) mod += 4;
    return mod == 2;
}

static int indian_days_in_year(int year)
{
    return indian_is_leap(year) ? 366 : 365;
}

static int indian_months_in_year(int year)
{
    (void)year;
    return 12;
}

static int indian_days_in_month(int year, int month)
{
    if (month < 1 || month > 12)
        return 30;
    if (month == 1)
        return indian_is_leap(year) ? 31 : 30;
    if (month <= 6)
        return 31;
    return 30;
}

static int ti_indian_to_iso(int indian_year, int indian_month, int indian_day,
                             int *iso_y, int *iso_m, int *iso_d)
{
    int gyear = indian_year + 78;
    int doy = indian_day - 1;
    int ml[12] = {30, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30};
    int64_t base_ord;
    
    if (indian_month < 1 || indian_month > 12)
        return -1;
    if (indian_is_leap(indian_year))
        ml[0] = 31;
    if (indian_day < 1 || indian_day > ml[indian_month - 1])
        return -1;
    
    for (int i = 0; i < indian_month - 1; i++)
        doy += ml[i];
    
    base_ord = pdt_to_ordinal_days(gyear, 3, indian_is_leap(indian_year) ? 21 : 22);
    pdt_from_ordinal_days(base_ord + doy, iso_y, iso_m, iso_d);
    return 0;
}

static int ti_iso_to_indian(int iso_y, int iso_m, int iso_d,
                             int *indian_y, int *indian_m, int *indian_d)
{
    int64_t iso_ord = pdt_to_ordinal_days(iso_y, iso_m, iso_d);
    int candidate_y;
    
    for (candidate_y = iso_y - 79; candidate_y <= iso_y - 77; candidate_y++) {
        int gyear = candidate_y + 78;
        int base_d = indian_is_leap(candidate_y) ? 21 : 22;
        int64_t base_ord = pdt_to_ordinal_days(gyear, 3, base_d);
        int year_len = indian_days_in_year(candidate_y);
        
        if (iso_ord >= base_ord && iso_ord < base_ord + year_len) {
            int doy = (int)(iso_ord - base_ord);
            int ml[12] = {30, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30};
            int m;
            if (indian_is_leap(candidate_y))
                ml[0] = 31;
            for (m = 0; m < 12; m++) {
                if (doy < ml[m])
                    break;
                doy -= ml[m];
            }
            *indian_y = candidate_y;
            *indian_m = m + 1;
            *indian_d = doy + 1;
            return 0;
        }
    }
    return -1;
}

/* ===== Hebrew ===== */

static int hebrew_is_leap(int year)
{
    int mod = (7 * year + 1) % 19;
    if (mod < 0) mod += 19;
    return mod < 7;
}

static int hebrew_months_in_year(int year)
{
    return hebrew_is_leap(year) ? 13 : 12;
}

/* Year type determines Heshvan and Kislev lengths:
 * Deficient: Heshvan 29, Kislev 29
 * Regular:   Heshvan 29, Kislev 30
 * Complete:  Heshvan 30, Kislev 30
 */

static int64_t hebrew_start_of_year(int year);

static int hebrew_days_in_year(int year)
{
    static const int hebrew_year_lengths[80] = {
        383, 354, 355, 383, 355, 354, 385, 353, 384, 355,
        355, 383, 354, 355, 385, 354, 383, 355, 354, 383,
        355, 354, 385, 353, 355, 384, 355, 383, 354, 355,
        385, 353, 354, 385, 355, 383, 354, 355, 383, 354,
        355, 385, 354, 353, 385, 354, 385, 353, 354, 385,
        355, 353, 384, 355, 383, 355, 354, 385, 355, 354,
        383, 355, 354, 383, 355, 385, 354, 353, 385, 354,
        355, 383, 354, 385, 353, 355, 384, 355, 353, 384
    };
    static const int hebrew_year_lengths_epoch[11] = {
        384, 355, 355, 383, 354, 355, 385, 354, 383, 355, 354
    };
    if (year >= 0 && year <= 10)
        return hebrew_year_lengths_epoch[year];
    if (year >= 5730 && year <= 5809)
        return hebrew_year_lengths[year - 5730];
    /* Fallback: compute actual length from start-of-year algorithm */
    return (int)(hebrew_start_of_year(year + 1) - hebrew_start_of_year(year));
}

static int hebrew_days_in_month(int year, int month)
{
    int leap = hebrew_is_leap(year);
    int months = leap ? 13 : 12;
    int days_in_year = hebrew_days_in_year(year);
    int is_deficient, is_complete;

    if (month < 1 || month > months)
        return 30;

    if (leap) {
        is_deficient = (days_in_year == 383);
        is_complete = (days_in_year == 385);
    } else {
        is_deficient = (days_in_year == 353);
        is_complete = (days_in_year == 355);
    }

    if (month == 1) /* Tishri */
        return 30;
    if (month == 2) /* Heshvan */
        return is_complete ? 30 : 29;
    if (month == 3) /* Kislev */
        return is_deficient ? 29 : 30;
    if (month == 4) /* Tevet */
        return 29;
    if (month == 5) /* Shevat */
        return 30;

    if (!leap) {
        /* Common year */
        if (month == 6)  return 29; /* Adar */
        if (month == 7)  return 30; /* Nisan */
        if (month == 8)  return 29; /* Iyar */
        if (month == 9)  return 30; /* Sivan */
        if (month == 10) return 29; /* Tammuz */
        if (month == 11) return 30; /* Av */
        if (month == 12) return 29; /* Elul */
    } else {
        /* Leap year */
        if (month == 6)  return 30; /* Adar I */
        if (month == 7)  return 29; /* Adar II */
        if (month == 8)  return 30; /* Nisan */
        if (month == 9)  return 29; /* Iyar */
        if (month == 10) return 30; /* Sivan */
        if (month == 11) return 29; /* Tammuz */
        if (month == 12) return 30; /* Av */
        if (month == 13) return 29; /* Elul */
    }
    return 30;
}

/* ===== Hebrew ISO conversion (based on ICU) ===== */

/* Cumulative days to end of each month. Index 0 is placeholder. */
static const int16_t hebrew_month_start[][3] = {
    /* Deficient  Normal     Complete */
    {    0,          0,          0  },
    {   30,         30,         30  },
    {   59,         59,         60  },
    {   88,         89,         90  },
    {  117,        118,        119  },
    {  147,        148,        149  },
    {  176,        177,        178  },
    {  206,        207,        208  },
    {  235,        236,        237  },
    {  265,        266,        267  },
    {  294,        295,        296  },
    {  324,        325,        326  },
    {  353,        354,        355  },
    {  353,        354,        355  },
};

static const int16_t hebrew_leap_month_start[][3] = {
    /* Deficient  Normal     Complete */
    {    0,          0,          0  },
    {   30,         30,         30  },
    {   59,         59,         60  },
    {   88,         89,         90  },
    {  117,        118,        119  },
    {  147,        148,        149  },
    {  177,        178,        179  },
    {  206,        207,        208  },
    {  236,        237,        238  },
    {  265,        266,        267  },
    {  295,        296,        297  },
    {  324,        325,        326  },
    {  354,        355,        356  },
    {  383,        384,        385  },
};

#define HEBREW_HOUR_PARTS 1080
#define HEBREW_DAY_PARTS  (24 * HEBREW_HOUR_PARTS)
#define HEBREW_MONTH_FRACT (12 * HEBREW_HOUR_PARTS + 793)
#define HEBREW_MONTH_PARTS (29 * HEBREW_DAY_PARTS + HEBREW_MONTH_FRACT)
#define HEBREW_BAHARAD     (11 * HEBREW_HOUR_PARTS + 204)

/* Hebrew epoch offset for pdt_to_ordinal_days:
 * ICU startOfYear(1)=0 corresponds to 1 Tishri 1.
 * Mapping verified: d=1 -> ordinal=-1373427 (ISO -3760-09-08).
 * offset = 1 - (-1373427) = 1373428.
 */
#define HEBREW_EPOCH_OFFSET 1373428

static int64_t hebrew_start_of_year(int year)
{
    int64_t months = floor_div64(235LL * year - 234, 19);
    int64_t frac = months * HEBREW_MONTH_FRACT + HEBREW_BAHARAD;
    int64_t frac_div = floor_div64(frac, HEBREW_DAY_PARTS);
    int64_t day = months * 29 + frac_div;
    int wd;
    frac = frac - frac_div * HEBREW_DAY_PARTS;
    wd = (int)((day % 7 + 7) % 7);
    if (wd == 2 || wd == 4 || wd == 6) {
        day += 1;
        wd = (int)((day % 7 + 7) % 7);
    } else if (wd == 1 && frac >= 15 * HEBREW_HOUR_PARTS + 204 && !hebrew_is_leap(year)) {
        day += 2;
    } else if (wd == 0 && frac >= 21 * HEBREW_HOUR_PARTS + 589 && hebrew_is_leap(year - 1)) {
        day += 1;
    }
    return day;
}

static int hebrew_year_type(int year)
{
    int len = (int)(hebrew_start_of_year(year + 1) - hebrew_start_of_year(year));
    if (hebrew_is_leap(year))
        len -= 30;
    if (len == 353) return 0;
    if (len == 355) return 2;
    return 1;
}

static void ti_hebrew_date_to_iso(int cal_year, int cal_month, int cal_day,
                                  int *iso_year, int *iso_month, int *iso_day)
{
    int64_t d = hebrew_start_of_year(cal_year) + cal_day - 1;
    int i;
    int type = hebrew_year_type(cal_year);
    for (i = 1; i < cal_month; i++)
        d += hebrew_is_leap(cal_year) ? hebrew_leap_month_start[i][type] - hebrew_leap_month_start[i - 1][type]
                                       : hebrew_month_start[i][type] - hebrew_month_start[i - 1][type];
    pdt_from_ordinal_days(d - HEBREW_EPOCH_OFFSET, iso_year, iso_month, iso_day);
}

static void ti_iso_to_hebrew_date(int iso_year, int iso_month, int iso_day,
                                  int *cal_year, int *cal_month, int *cal_day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t d = ordinal + HEBREW_EPOCH_OFFSET;
    /* Estimate year using the same formula as ICU:
     * months ≈ floor(d * DAY_PARTS / MONTH_PARTS)
     * year ≈ floor((19 * months + 234) / 235) + 1
     */
    int64_t est_months = floor_div64(d * HEBREW_DAY_PARTS, HEBREW_MONTH_PARTS);
    int year = (int)floor_div64(19 * est_months + 234, 235) + 1;
    if (year < 1) year = 1;
    while (d < hebrew_start_of_year(year)) year--;
    while (d > hebrew_start_of_year(year + 1)) year++;
    {
        int day_of_year = (int)(d - hebrew_start_of_year(year)) + 1;
        int type = hebrew_year_type(year);
        int is_leap = hebrew_is_leap(year);
        const int16_t (*table)[3] = is_leap ? hebrew_leap_month_start : hebrew_month_start;
        int month = 0;
        while (month < 13 && day_of_year > table[month + 1][type]) month++;
        *cal_year = year;
        *cal_month = month + 1;
        *cal_day = day_of_year - table[month][type];
    }
}

/* ===== Islamic umalqura ===== */
/* Table-based observational calendar for years 1300-1600.
   Outside this range, falls back to Islamic civil rules. */

#define UMALQURA_YEAR_START 1300
#define UMALQURA_YEAR_END   1600

static const int16_t umalqura_year_lengths[301] = {
    0x0AAA, 0x0D54, 0x0EC9, 0x06D4, 0x06EA, 0x036C, 0x0AAD, 0x0555,
    0x06A9, 0x0792, 0x0BA9, 0x05D4, 0x0ADA, 0x055C, 0x0D2D, 0x0695,
    0x074A, 0x0B54, 0x0B6A, 0x05AD, 0x04AE, 0x0A4F, 0x0517, 0x068B,
    0x06A5, 0x0AD5, 0x02D6, 0x095B, 0x049D, 0x0A4D, 0x0D26, 0x0D95,
    0x05AC, 0x09B6, 0x02BA, 0x0A5B, 0x052B, 0x0A95, 0x06CA, 0x0AE9,
    0x02F4, 0x0976, 0x02B6, 0x0956, 0x0ACA, 0x0BA4, 0x0BD2, 0x05D9,
    0x02DC, 0x096D, 0x054D, 0x0AA5, 0x0B52, 0x0BA5, 0x05B4, 0x09B6,
    0x0557, 0x0297, 0x054B, 0x06A3, 0x0752, 0x0B65, 0x056A, 0x0AAB,
    0x052B, 0x0C95, 0x0D4A, 0x0DA5, 0x05CA, 0x0AD6, 0x0957, 0x04AB,
    0x094B, 0x0AA5, 0x0B52, 0x0B6A, 0x0575, 0x0276, 0x08B7, 0x045B,
    0x0555, 0x05A9, 0x05B4, 0x09DA, 0x04DD, 0x026E, 0x0936, 0x0AAA,
    0x0D54, 0x0DB2, 0x05D5, 0x02DA, 0x095B, 0x04AB, 0x0A55, 0x0B49,
    0x0B64, 0x0B71, 0x05B4, 0x0AB5, 0x0A55, 0x0D25, 0x0E92, 0x0EC9,
    0x06D4, 0x0AE9, 0x096B, 0x04AB, 0x0A93, 0x0D49, 0x0DA4, 0x0DB2,
    0x0AB9, 0x04BA, 0x0A5B, 0x052B, 0x0A95, 0x0B2A, 0x0B55, 0x055C,
    0x04BD, 0x023D, 0x091D, 0x0A95, 0x0B4A, 0x0B5A, 0x056D, 0x02B6,
    0x093B, 0x049B, 0x0655, 0x06A9, 0x0754, 0x0B6A, 0x056C, 0x0AAD,
    0x0555, 0x0B29, 0x0B92, 0x0BA9, 0x05D4, 0x0ADA, 0x055A, 0x0AAB,
    0x0595, 0x0749, 0x0764, 0x0BAA, 0x05B5, 0x02B6, 0x0A56, 0x0E4D,
    0x0B25, 0x0B52, 0x0B6A, 0x05AD, 0x02AE, 0x092F, 0x0497, 0x064B,
    0x06A5, 0x06AC, 0x0AD6, 0x055D, 0x049D, 0x0A4D, 0x0D16, 0x0D95,
    0x05AA, 0x05B5, 0x02DA, 0x095B, 0x04AD, 0x0595, 0x06CA, 0x06E4,
    0x0AEA, 0x04F5, 0x02B6, 0x0956, 0x0AAA, 0x0B54, 0x0BD2, 0x05D9,
    0x02EA, 0x096D, 0x04AD, 0x0A95, 0x0B4A, 0x0BA5, 0x05B2, 0x09B5,
    0x04D6, 0x0A97, 0x0547, 0x0693, 0x0749, 0x0B55, 0x056A, 0x0A6B,
    0x052B, 0x0A8B, 0x0D46, 0x0DA3, 0x05CA, 0x0AD6, 0x04DB, 0x026B,
    0x094B, 0x0AA5, 0x0B52, 0x0B69, 0x0575, 0x0176, 0x08B7, 0x025B,
    0x052B, 0x0565, 0x05B4, 0x09DA, 0x04ED, 0x016D, 0x08B6, 0x0AA6,
    0x0D52, 0x0DA9, 0x05D4, 0x0ADA, 0x095B, 0x04AB, 0x0653, 0x0729,
    0x0762, 0x0BA9, 0x05B2, 0x0AB5, 0x0555, 0x0B25, 0x0D92, 0x0EC9,
    0x06D2, 0x0AE9, 0x056B, 0x04AB, 0x0A55, 0x0D29, 0x0D54, 0x0DAA,
    0x09B5, 0x04BA, 0x0A3B, 0x049B, 0x0A4D, 0x0AAA, 0x0AD5, 0x02DA,
    0x095D, 0x045E, 0x0A2E, 0x0C9A, 0x0D55, 0x06B2, 0x06B9, 0x04BA,
    0x0A5D, 0x052D, 0x0A95, 0x0B52, 0x0BA8, 0x0BB4, 0x05B9, 0x02DA,
    0x095A, 0x0B4A, 0x0DA4, 0x0ED1, 0x06E8, 0x0B6A, 0x056D, 0x0535,
    0x0695, 0x0D4A, 0x0DA8, 0x0DD4, 0x06DA, 0x055B, 0x029D, 0x062B,
    0x0B15, 0x0B4A, 0x0B95, 0x05AA, 0x0AAE, 0x092E, 0x0C8F, 0x0527,
    0x0695, 0x06AA, 0x0AD6, 0x055D, 0x029D,
};

static const int32_t umalqura_year_starts[302] = {
    460322, 460676, 461030, 461385, 461739, 462094, 462448, 462803,
    463157, 463511, 463865, 464220, 464574, 464929, 465283, 465638,
    465992, 466346, 466700, 467055, 467410, 467764, 468119, 468473,
    468827, 469181, 469536, 469890, 470245, 470599, 470953, 471307,
    471662, 472016, 472371, 472725, 473080, 473434, 473788, 474142,
    474497, 474851, 475206, 475560, 475914, 476268, 476622, 476977,
    477332, 477686, 478041, 478395, 478749, 479103, 479458, 479812,
    480167, 480522, 480876, 481230, 481584, 481938, 482293, 482647,
    483002, 483356, 483710, 484064, 484419, 484773, 485128, 485483,
    485837, 486191, 486545, 486899, 487254, 487609, 487963, 488318,
    488672, 489026, 489380, 489734, 490089, 490444, 490798, 491152,
    491506, 491860, 492215, 492570, 492924, 493279, 493633, 493987,
    494341, 494695, 495050, 495404, 495759, 496113, 496467, 496821,
    497176, 497530, 497885, 498240, 498594, 498948, 499302, 499656,
    500011, 500366, 500720, 501075, 501429, 501783, 502137, 502492,
    502846, 503201, 503555, 503909, 504263, 504617, 504972, 505327,
    505681, 506036, 506390, 506744, 507098, 507452, 507807, 508161,
    508516, 508870, 509224, 509578, 509933, 510287, 510642, 510996,
    511351, 511705, 512059, 512413, 512768, 513123, 513477, 513831,
    514186, 514540, 514894, 515249, 515604, 515958, 516313, 516667,
    517021, 517375, 517729, 518084, 518439, 518793, 519147, 519501,
    519856, 520210, 520565, 520919, 521274, 521628, 521982, 522336,
    522690, 523045, 523400, 523754, 524108, 524462, 524816, 525171,
    525526, 525880, 526235, 526589, 526943, 527297, 527652, 528006,
    528361, 528715, 529070, 529424, 529778, 530132, 530487, 530841,
    531196, 531550, 531904, 532258, 532613, 532967, 533322, 533677,
    534031, 534385, 534739, 535093, 535448, 535803, 536157, 536512,
    536866, 537220, 537574, 537928, 538283, 538638, 538992, 539346,
    539700, 540054, 540409, 540763, 541118, 541473, 541827, 542181,
    542535, 542889, 543244, 543598, 543953, 544307, 544661, 545015,
    545370, 545724, 546079, 546434, 546788, 547142, 547496, 547850,
    548205, 548560, 548914, 549269, 549623, 549977, 550331, 550686,
    551040, 551395, 551749, 552103, 552457, 552812, 553166, 553521,
    553875, 554230, 554584, 554938, 555292, 555646, 556001, 556356,
    556710, 557064, 557418, 557772, 558127, 558481, 558836, 559191,
    559545, 559899, 560253, 560607, 560962, 561317, 561672, 562026,
    562380, 562734, 563088, 563443, 563797, 564152, 564506, 564861,
    565215, 565569, 565923, 566278, 566633, 566987
};

static int umalqura_in_range(int year)
{
    return year >= UMALQURA_YEAR_START && year <= UMALQURA_YEAR_END;
}

static int umalqura_month_length(int year_idx, int month)
{
    int val = umalqura_year_lengths[year_idx];
    int bit = (val >> (11 - (month - 1))) & 1;
    return bit ? 30 : 29;
}

static int islamic_umalqura_is_leap(int year)
{
    int idx;
    if (!umalqura_in_range(year))
        return islamic_civil_is_leap(year);
    idx = year - UMALQURA_YEAR_START;
    return umalqura_month_length(idx, 1) + umalqura_month_length(idx, 2) +
           umalqura_month_length(idx, 3) + umalqura_month_length(idx, 4) +
           umalqura_month_length(idx, 5) + umalqura_month_length(idx, 6) +
           umalqura_month_length(idx, 7) + umalqura_month_length(idx, 8) +
           umalqura_month_length(idx, 9) + umalqura_month_length(idx, 10) +
           umalqura_month_length(idx, 11) + umalqura_month_length(idx, 12) == 355;
}

static int islamic_umalqura_days_in_year(int year)
{
    int idx;
    if (!umalqura_in_range(year))
        return islamic_civil_days_in_year(year);
    idx = year - UMALQURA_YEAR_START;
    return umalqura_month_length(idx, 1) + umalqura_month_length(idx, 2) +
           umalqura_month_length(idx, 3) + umalqura_month_length(idx, 4) +
           umalqura_month_length(idx, 5) + umalqura_month_length(idx, 6) +
           umalqura_month_length(idx, 7) + umalqura_month_length(idx, 8) +
           umalqura_month_length(idx, 9) + umalqura_month_length(idx, 10) +
           umalqura_month_length(idx, 11) + umalqura_month_length(idx, 12);
}

static int islamic_umalqura_months_in_year(int year)
{
    (void)year;
    return 12;
}

static int islamic_umalqura_days_in_month(int year, int month)
{
    int idx;
    if (!umalqura_in_range(year))
        return islamic_civil_days_in_month(year, month);
    if (month < 1 || month > 12)
        return 29;
    idx = year - UMALQURA_YEAR_START;
    return umalqura_month_length(idx, month);
}

/* ===== Islamic civil <-> ISO conversion ===== */
#define ISLAMIC_CIVIL_EPOCH_ORDINAL 227014

static int64_t islamic_civil_month_start(int year, int month)
{
    return (59LL * month + 1) / 2 + 354LL * (year - 1) + floor_div64(11LL * year + 3, 30);
}

static void ti_islamic_civil_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    int64_t days_from_epoch = islamic_civil_month_start(year, month - 1) + (day - 1);
    pdt_from_ordinal_days(ISLAMIC_CIVIL_EPOCH_ORDINAL + days_from_epoch, iso_year, iso_month, iso_day);
}

static void ti_iso_to_islamic_civil(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t days = ordinal - ISLAMIC_CIVIL_EPOCH_ORDINAL;
    int y = (int)floor_div64(30 * days + 10646, 10631);
    int64_t yd = islamic_civil_month_start(y, 0);
    while (yd > days) {
        y--;
        yd = islamic_civil_month_start(y, 0);
    }
    while (yd + (islamic_civil_is_leap(y) ? 355 : 354) <= days) {
        y++;
        yd = islamic_civil_month_start(y, 0);
    }
    int doy = (int)(days - yd);
    int m = 0;
    while (m < 11 && doy >= islamic_civil_days_in_month(y, m + 1)) {
        doy -= islamic_civil_days_in_month(y, m + 1);
        m++;
    }
    *year = y;
    *month = m + 1;
    *day = doy + 1;
}

/* ===== Islamic tbla <-> ISO conversion ===== */
#define ISLAMIC_TBLA_EPOCH_ORDINAL 227013

static void ti_islamic_tbla_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    int64_t days_from_epoch = islamic_civil_month_start(year, month - 1) + (day - 1);
    pdt_from_ordinal_days(ISLAMIC_TBLA_EPOCH_ORDINAL + days_from_epoch, iso_year, iso_month, iso_day);
}

static void ti_iso_to_islamic_tbla(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t days = ordinal - ISLAMIC_TBLA_EPOCH_ORDINAL;
    int y = (int)floor_div64(30 * days + 10646, 10631);
    int64_t yd = islamic_civil_month_start(y, 0);
    while (yd > days) {
        y--;
        yd = islamic_civil_month_start(y, 0);
    }
    while (yd + (islamic_civil_is_leap(y) ? 355 : 354) <= days) {
        y++;
        yd = islamic_civil_month_start(y, 0);
    }
    int doy = (int)(days - yd);
    int m = 0;
    while (m < 11 && doy >= islamic_civil_days_in_month(y, m + 1)) {
        doy -= islamic_civil_days_in_month(y, m + 1);
        m++;
    }
    *year = y;
    *month = m + 1;
    *day = doy + 1;
}

static void ti_islamic_umalqura_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    if (umalqura_in_range(year)) {
        int idx = year - UMALQURA_YEAR_START;
        int64_t days_from_epoch = umalqura_year_starts[idx];
        int m;
        for (m = 1; m < month; m++)
            days_from_epoch += umalqura_month_length(idx, m);
        days_from_epoch += (day - 1);
        pdt_from_ordinal_days(ISLAMIC_CIVIL_EPOCH_ORDINAL + days_from_epoch, iso_year, iso_month, iso_day);
    } else {
        /* Fallback to civil outside table range */
        ti_islamic_civil_to_iso(year, month, day, iso_year, iso_month, iso_day);
    }
}

static void ti_iso_to_islamic_umalqura(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t days = ordinal - ISLAMIC_CIVIL_EPOCH_ORDINAL;
    int y, idx;
    /* Check if within table range */
    if (days >= umalqura_year_starts[0] && days < umalqura_year_starts[301]) {
        /* Binary search for year */
        int lo = 0, hi = 300;
        while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            if (umalqura_year_starts[mid] <= days)
                lo = mid;
            else
                hi = mid - 1;
        }
        y = UMALQURA_YEAR_START + lo;
        idx = lo;
    } else {
        /* Fallback to civil */
        ti_iso_to_islamic_civil(iso_year, iso_month, iso_day, year, month, day);
        return;
    }
    {
        int64_t yd = umalqura_year_starts[idx];
        int doy = (int)(days - yd);
        int m = 0;
        while (m < 11) {
            int mlen = umalqura_month_length(idx, m + 1);
            if (doy < mlen)
                break;
            doy -= mlen;
            m++;
        }
        *year = y;
        *month = m + 1;
        *day = doy + 1;
    }
}

/* ===== Calendar <-> ISO conversion functions ===== */

/* Coptic epoch: Thout 1, year 1 = August 29, 284 CE = ordinal 103604 */
#define COPTIC_EPOCH_ORDINAL 103604

static void ti_coptic_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    int64_t ordinal = COPTIC_EPOCH_ORDINAL + 365 * (int64_t)(year - 1) + floor_div64(year, 4) + 30 * (month - 1) + (day - 1);
    pdt_from_ordinal_days(ordinal, iso_year, iso_month, iso_day);
}

static void ti_iso_to_coptic(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t coptic_ordinal = ordinal - COPTIC_EPOCH_ORDINAL;
    int64_t q = floor_div64(coptic_ordinal, 1461);
    int64_t r = coptic_ordinal - q * 1461;
    int n;
    if (r < 365) n = 0;
    else if (r < 730) n = 1;
    else if (r < 1096) n = 2;
    else n = 3;
    *year = (int)(4 * q + n + 1);
    {
        int64_t rd_year_start = 365 * (*year - 1) + floor_div64(*year, 4);
        int64_t remaining = coptic_ordinal - rd_year_start;
        if (month)
            *month = (int)(remaining / 30 + 1);
        if (day)
            *day = (int)(remaining % 30 + 1);
    }
}

/* Ethiopic epoch: Maskaram 1, year 1 = August 27, 8 CE = ordinal 2795 */
#define ETHIOPIC_EPOCH_ORDINAL 2795

static void ti_ethiopic_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    int64_t ordinal = ETHIOPIC_EPOCH_ORDINAL + 365 * (int64_t)(year - 1) + floor_div64(year, 4) + 30 * (month - 1) + (day - 1);
    pdt_from_ordinal_days(ordinal, iso_year, iso_month, iso_day);
}

static void ti_iso_to_ethiopic(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t ethiopic_ordinal = ordinal - ETHIOPIC_EPOCH_ORDINAL;
    int64_t q = floor_div64(ethiopic_ordinal, 1461);
    int64_t r = ethiopic_ordinal - q * 1461;
    int n;
    if (r < 365) n = 0;
    else if (r < 730) n = 1;
    else if (r < 1096) n = 2;
    else n = 3;
    *year = (int)(4 * q + n + 1);
    {
        int64_t rd_year_start = 365 * (*year - 1) + floor_div64(*year, 4);
        int64_t remaining = ethiopic_ordinal - rd_year_start;
        if (month)
            *month = (int)(remaining / 30 + 1);
        if (day)
            *day = (int)(remaining % 30 + 1);
    }
}

static void ti_ethioaa_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    ti_ethiopic_to_iso(year - 5500, month, day, iso_year, iso_month, iso_day);
}

static void ti_iso_to_ethioaa(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    ti_iso_to_ethiopic(iso_year, iso_month, iso_day, year, month, day);
    *year += 5500;
}

/* Persian epoch: 1 Farvardin 1 AP = March 21, 622 CE = ordinal 226894 */
#define PERSIAN_EPOCH_ORDINAL 226894

static void ti_persian_to_iso(int year, int month, int day, int *iso_year, int *iso_month, int *iso_day)
{
    static const int num_days[12] = {0, 31, 62, 93, 124, 155, 186, 216, 246, 276, 306, 336};
    int64_t days_from_epoch = 365LL * (year - 1) + floor_div64(8LL * year + 21, 33);
    if (month > 1)
        days_from_epoch += num_days[month - 1];
    days_from_epoch += day - 1;
    pdt_from_ordinal_days(PERSIAN_EPOCH_ORDINAL + days_from_epoch, iso_year, iso_month, iso_day);
}

static void ti_iso_to_persian(int iso_year, int iso_month, int iso_day, int *year, int *month, int *day)
{
    static const int num_days[12] = {0, 31, 62, 93, 124, 155, 186, 216, 246, 276, 306, 336};
    int64_t ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int64_t days = ordinal - PERSIAN_EPOCH_ORDINAL;
    int64_t y = floor_div64(33LL * days + 3, 12053) + 1;
    int64_t farvardin1 = 365LL * (y - 1) + floor_div64(8LL * y + 21, 33);
    int64_t day_of_year = days - farvardin1;
    if (day_of_year == 365 && !persian_is_leap((int)y)) {
        y++;
        farvardin1 = 365LL * (y - 1) + floor_div64(8LL * y + 21, 33);
        day_of_year = days - farvardin1;
    }
    int64_t m;
    if (day_of_year < 216) {
        m = day_of_year / 31;
    } else {
        m = (day_of_year - 6) / 30;
    }
    *year = (int)y;
    *month = (int)(m + 1);
    *day = (int)(day_of_year - num_days[m] + 1);
}

/* Chinese/Dangi date <-> ISO conversion using precomputed tables */
static int ti_chinese_date_to_iso(int cal_year, int cal_month, int cal_day,
                                  int *iso_year, int *iso_month, int *iso_day,
                                  const ChineseCalendarEntry *data, int count)
{
    const ChineseCalendarEntry *e;
    int64_t ny_ordinal, target_ordinal;
    int doy = cal_day;
    int i;
    int idx = cal_year - 1800;
    if (idx < 0 || idx >= count)
        return -1;
    e = &data[idx];
    if (cal_month < 1 || cal_month > cce_num_months(e) || cal_day < 1 || cal_day > cce_month_length(e, cal_month - 1))
        return -1;
    ny_ordinal = pdt_to_ordinal_days(cal_year, cce_cny_month(e), cce_cny_day(e));
    for (i = 0; i < cal_month - 1; i++)
        doy += cce_month_length(e, i);
    target_ordinal = ny_ordinal + doy - 1;
    pdt_from_ordinal_days(target_ordinal, iso_year, iso_month, iso_day);
    return 0;
}

static int ti_iso_to_chinese_date(int iso_year, int iso_month, int iso_day,
                                  int *cal_year, int *cal_month, int *cal_day,
                                  const ChineseCalendarEntry *data, int count)
{
    int64_t target_ordinal = pdt_to_ordinal_days(iso_year, iso_month, iso_day);
    int i;
    for (i = 0; i < count; i++) {
        const ChineseCalendarEntry *e = &data[i];
        int64_t ny_ordinal = pdt_to_ordinal_days(1800 + i, cce_cny_month(e), cce_cny_day(e));
        int days_in_year = 0;
        int j;
        for (j = 0; j < cce_num_months(e); j++)
            days_in_year += cce_month_length(e, j);
        if (target_ordinal >= ny_ordinal && target_ordinal < ny_ordinal + days_in_year) {
            int doy = (int)(target_ordinal - ny_ordinal) + 1;
            int month = 1;
            while (month <= cce_num_months(e) && doy > cce_month_length(e, month - 1)) {
                doy -= cce_month_length(e, month - 1);
                month++;
            }
            *cal_year = 1800 + i;
            *cal_month = month;
            *cal_day = doy;
            return 0;
        }
    }
    return -1;
}

/* CalendarDateToISO: convert calendar year/month/day to ISO year/month/day */
int ti_calendar_date_to_iso(CalendarType cal, int cal_year, int cal_month, int cal_day,
                            int *iso_year, int *iso_month, int *iso_day)
{
    switch (cal) {
    case CAL_ISO8601:
    case CAL_GREGORY:
    case CAL_JAPANESE:
        *iso_year = cal_year;
        *iso_month = cal_month;
        *iso_day = cal_day;
        return 0;
    case CAL_BUDDHIST:
        *iso_year = cal_year - 543;
        *iso_month = cal_month;
        *iso_day = cal_day;
        return 0;
    case CAL_ROC:
        *iso_year = cal_year + 1911;
        *iso_month = cal_month;
        *iso_day = cal_day;
        return 0;
    case CAL_COPTIC:
        ti_coptic_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_ETHIOPIC:
        ti_ethiopic_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_ETHIOAA:
        ti_ethioaa_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_HEBREW:
        ti_hebrew_date_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_PERSIAN:
        ti_persian_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_ISLAMIC_CIVIL:
        ti_islamic_civil_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_ISLAMIC_TBLA:
        ti_islamic_tbla_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_ISLAMIC_UMALQURA:
        ti_islamic_umalqura_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
        return 0;
    case CAL_INDIAN:
        return ti_indian_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day);
    case CAL_CHINESE:
        if (ti_chinese_date_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day,
                                    chinese_calendar_data,
                                    sizeof(chinese_calendar_data) / sizeof(chinese_calendar_data[0])) == 0)
            return 0;
        break;
    case CAL_DANGI:
        if (ti_chinese_date_to_iso(cal_year, cal_month, cal_day, iso_year, iso_month, iso_day,
                                    dangi_calendar_data,
                                    sizeof(dangi_calendar_data) / sizeof(dangi_calendar_data[0])) == 0)
            return 0;
        break;
    default:
        break;
    }
    /* Fallback: treat calendar date as ISO date (old behavior) */
    *iso_year = cal_year;
    *iso_month = cal_month;
    *iso_day = cal_day;
    return 0;
}

/* ISOToCalendarDate: convert ISO year/month/day to calendar year/month/day */
int ti_iso_to_calendar_date(CalendarType cal, int iso_year, int iso_month, int iso_day,
                            int *cal_year, int *cal_month, int *cal_day)
{
    switch (cal) {
    case CAL_ISO8601:
    case CAL_GREGORY:
    case CAL_JAPANESE:
        *cal_year = iso_year;
        *cal_month = iso_month;
        *cal_day = iso_day;
        return 0;
    case CAL_BUDDHIST:
        *cal_year = iso_year + 543;
        *cal_month = iso_month;
        *cal_day = iso_day;
        return 0;
    case CAL_ROC:
        *cal_year = iso_year - 1911;
        *cal_month = iso_month;
        *cal_day = iso_day;
        return 0;
    case CAL_COPTIC:
        ti_iso_to_coptic(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_ETHIOPIC:
        ti_iso_to_ethiopic(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_ETHIOAA:
        ti_iso_to_ethioaa(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_HEBREW:
        ti_iso_to_hebrew_date(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_PERSIAN:
        ti_iso_to_persian(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_ISLAMIC_CIVIL:
        ti_iso_to_islamic_civil(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_ISLAMIC_TBLA:
        ti_iso_to_islamic_tbla(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_ISLAMIC_UMALQURA:
        ti_iso_to_islamic_umalqura(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
        return 0;
    case CAL_INDIAN:
        return ti_iso_to_indian(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day);
    case CAL_CHINESE:
        if (ti_iso_to_chinese_date(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day,
                                    chinese_calendar_data,
                                    sizeof(chinese_calendar_data) / sizeof(chinese_calendar_data[0])) == 0)
            return 0;
        break;
    case CAL_DANGI:
        if (ti_iso_to_chinese_date(iso_year, iso_month, iso_day, cal_year, cal_month, cal_day,
                                    dangi_calendar_data,
                                    sizeof(dangi_calendar_data) / sizeof(dangi_calendar_data[0])) == 0)
            return 0;
        break;
    default:
        break;
    }
    /* Fallback: treat ISO date as calendar date (old behavior) */
    *cal_year = iso_year;
    *cal_month = iso_month;
    *cal_day = iso_day;
    return 0;
}

/* ===== Public dispatch functions ===== */

int ti_calendar_is_leap_year(CalendarType cal, int year)
{
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOPIC:
    case CAL_ETHIOAA:
        return coptic_ethiopic_is_leap(year);
    case CAL_PERSIAN:
        return persian_is_leap(year);
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA:
        return islamic_civil_is_leap(year);
    case CAL_INDIAN:
        return indian_is_leap(year);
    case CAL_CHINESE:
        return ti_calendar_is_leap_year_chinese(year);
    case CAL_DANGI:
        return ti_calendar_is_leap_year_dangi(year);
    case CAL_HEBREW:
        return hebrew_is_leap(year);
    case CAL_ISLAMIC_UMALQURA:
        return islamic_umalqura_is_leap(year);
    case CAL_BUDDHIST:
        return pd_is_leap_year(year - 543);
    case CAL_ROC:
        return pd_is_leap_year(year + 1911);
    case CAL_GREGORY:
    case CAL_JAPANESE:
    case CAL_ISO8601:
    default:
        return pd_is_leap_year(year);
    }
}

int ti_calendar_days_in_year(CalendarType cal, int year)
{
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOPIC:
    case CAL_ETHIOAA:
        return coptic_ethiopic_days_in_year(year);
    case CAL_PERSIAN:
        return persian_days_in_year(year);
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA:
        return islamic_civil_days_in_year(year);
    case CAL_INDIAN:
        return indian_days_in_year(year);
    case CAL_CHINESE:
        return ti_calendar_days_in_year_chinese(year);
    case CAL_DANGI:
        return ti_calendar_days_in_year_dangi(year);
    case CAL_HEBREW:
        return hebrew_days_in_year(year);
    case CAL_ISLAMIC_UMALQURA:
        return islamic_umalqura_days_in_year(year);
    case CAL_BUDDHIST:
        return pd_is_leap_year(year - 543) ? 366 : 365;
    case CAL_ROC:
        return pd_is_leap_year(year + 1911) ? 366 : 365;
    case CAL_GREGORY:
    case CAL_JAPANESE:
    case CAL_ISO8601:
    default:
        return pd_is_leap_year(year) ? 366 : 365;
    }
}

int ti_calendar_months_in_year(CalendarType cal, int year)
{
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOPIC:
    case CAL_ETHIOAA:
        return coptic_ethiopic_months_in_year(year);
    case CAL_PERSIAN:
        return persian_months_in_year(year);
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA:
        return islamic_civil_months_in_year(year);
    case CAL_INDIAN:
        return indian_months_in_year(year);
    case CAL_CHINESE:
        return ti_calendar_months_in_year_chinese(year);
    case CAL_DANGI:
        return ti_calendar_months_in_year_dangi(year);
    case CAL_HEBREW:
        return hebrew_months_in_year(year);
    case CAL_ISLAMIC_UMALQURA:
        return islamic_umalqura_months_in_year(year);
    case CAL_BUDDHIST:
    case CAL_GREGORY:
    case CAL_JAPANESE:
    case CAL_ROC:
    case CAL_ISO8601:
    default:
        return 12;
    }
}

int ti_calendar_days_in_month(CalendarType cal, int year, int month)
{
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOPIC:
    case CAL_ETHIOAA:
        return coptic_ethiopic_days_in_month(year, month);
    case CAL_PERSIAN:
        return persian_days_in_month(year, month);
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA:
        return islamic_civil_days_in_month(year, month);
    case CAL_INDIAN:
        return indian_days_in_month(year, month);
    case CAL_CHINESE:
        return ti_calendar_days_in_month_chinese(year, month);
    case CAL_DANGI:
        return ti_calendar_days_in_month_dangi(year, month);
    case CAL_HEBREW:
        return hebrew_days_in_month(year, month);
    case CAL_ISLAMIC_UMALQURA:
        return islamic_umalqura_days_in_month(year, month);
    case CAL_BUDDHIST:
        return pd_days_in_month(year - 543, month);
    case CAL_ROC:
        return pd_days_in_month(year + 1911, month);
    case CAL_GREGORY:
    case CAL_JAPANESE:
    case CAL_ISO8601:
    default:
        return pd_days_in_month(year, month);
    }
}

int ti_calendar_day_of_year(CalendarType cal, int year, int month, int day)
{
    int doy = 0;
    int i;
    int dim = ti_calendar_days_in_month(cal, year, month);
    if (day > dim) day = dim;
    else if (day < 1) day = 1;
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOPIC:
    case CAL_ETHIOAA:
        if (month > 13)
            month = 13;
        doy = (month - 1) * 30 + day;
        return doy;
    case CAL_PERSIAN: {
        int pl[12] = {31, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 29};
        if (persian_is_leap(year))
            pl[11] = 30;
        for (i = 0; i < month - 1; i++)
            doy += pl[i];
        return doy + day;
    }
    case CAL_ISLAMIC_CIVIL:
    case CAL_ISLAMIC_TBLA: {
        int il[12] = {30, 29, 30, 29, 30, 29, 30, 29, 30, 29, 30, 29};
        if (islamic_civil_is_leap(year))
            il[11] = 30;
        for (i = 0; i < month - 1; i++)
            doy += il[i];
        return doy + day;
    }
    case CAL_INDIAN: {
        int il2[12] = {30, 31, 31, 31, 31, 31, 30, 30, 30, 30, 30, 30};
        if (indian_is_leap(year))
            il2[0] = 31;
        for (i = 0; i < month - 1; i++)
            doy += il2[i];
        return doy + day;
    }
    case CAL_HEBREW: {
        for (i = 1; i < month; i++)
            doy += hebrew_days_in_month(year, i);
        return doy + day;
    }
    case CAL_CHINESE: {
        const ChineseCalendarEntry *entry = lookup_chinese_year(year);
        if (!entry || month < 1 || month > cce_num_months(entry))
            return (month - 1) * 29 + day;
        for (i = 0; i < month - 1; i++)
            doy += cce_month_length(entry, i);
        return doy + day;
    }
    case CAL_DANGI: {
        const ChineseCalendarEntry *entry = lookup_dangi_year(year);
        if (!entry || month < 1 || month > cce_num_months(entry))
            return (month - 1) * 29 + day;
        for (i = 0; i < month - 1; i++)
            doy += cce_month_length(entry, i);
        return doy + day;
    }
    case CAL_ISLAMIC_UMALQURA: {
        if (umalqura_in_range(year)) {
            int idx = year - UMALQURA_YEAR_START;
            for (i = 1; i < month; i++)
                doy += umalqura_month_length(idx, i);
        } else {
            for (i = 1; i < month; i++)
                doy += islamic_civil_days_in_month(year, i);
        }
        return doy + day;
    }
    case CAL_BUDDHIST:
        return pd_day_of_year(year - 543, month, day);
    case CAL_ROC:
        return pd_day_of_year(year + 1911, month, day);
    case CAL_GREGORY:
    case CAL_JAPANESE:
    case CAL_ISO8601:
    default:
        return pd_day_of_year(year, month, day);
    }
}

/* ------------------------------------------------------------------ */
/* Calendar-aware date arithmetic for non-ISO calendars               */
/* ------------------------------------------------------------------ */

static int compare_calendar_dates(CalendarType cal, int y1, int m1, int d1,
                                    int y2, int m2, int d2)
{
    (void)cal;
    if (y1 != y2) return (y1 < y2) ? -1 : 1;
    if (m1 != m2) return (m1 < m2) ? -1 : 1;
    if (d1 != d2) return (d1 < d2) ? -1 : 1;
    return 0;
}

static int64_t calendar_days_between(CalendarType cal, int y1, int m1, int d1,
                                       int y2, int m2, int d2)
{
    int cmp = compare_calendar_dates(cal, y1, m1, d1, y2, m2, d2);
    if (cmp == 0) return 0;

    int sy = y1, sm = m1, sd = d1;
    int ey = y2, em = m2, ed = d2;
    int sign = 1;

    if (cmp > 0) {
        sy = y2; sm = m2; sd = d2;
        ey = y1; em = m1; ed = d1;
        sign = -1;
    }

    int64_t doy_s = ti_calendar_day_of_year(cal, sy, sm, sd);
    int64_t doy_e = ti_calendar_day_of_year(cal, ey, em, ed);
    int64_t total = 0;

    if (sy == ey) {
        total = doy_e - doy_s;
    } else {
        total = ti_calendar_days_in_year(cal, sy) - doy_s;
        for (int y = sy + 1; y < ey; y++)
            total += ti_calendar_days_in_year(cal, y);
        total += doy_e;
    }

    return total * sign;
}

static void ti_balance_month(int *year, int *month, CalendarType cal)
{
    int max_month;
    while (1) {
        max_month = (cal == CAL_ISO8601 || cal == CAL_GREGORY) ? 12 : ti_calendar_months_in_year(cal, *year);
        if (*month <= max_month && *month >= 1)
            break;
        if (*month > max_month) {
            *month -= max_month;
            (*year)++;
        } else {
            (*year)--;
            max_month = (cal == CAL_ISO8601 || cal == CAL_GREGORY) ? 12 : ti_calendar_months_in_year(cal, *year);
            *month += max_month;
        }
    }
}

void ti_calendar_date_until(CalendarType cal, int one_year, int one_month, int one_day,
                             int two_year, int two_month, int two_day,
                             int largestUnit,
                             int64_t *out_years, int64_t *out_months,
                             int64_t *out_weeks, int64_t *out_days)
{
    if (cal == CAL_ISO8601 || cal == CAL_GREGORY ||
        cal == CAL_BUDDHIST || cal == CAL_ROC || cal == CAL_JAPANESE) {
        ti_iso_calendar_date_until(one_year, one_month, one_day,
                                    two_year, two_month, two_day, largestUnit,
                                    out_years, out_months, out_weeks, out_days);
        return;
    }

    if (out_years) *out_years = 0;
    if (out_months) *out_months = 0;
    if (out_weeks) *out_weeks = 0;
    if (out_days) *out_days = 0;

    int cmp = compare_calendar_dates(cal, one_year, one_month, one_day,
                                      two_year, two_month, two_day);
    if (cmp == 0)
        return;

    int sign = (cmp > 0) ? -1 : 1;
    int64_t years = 0, months = 0, weeks = 0, days = 0;
    int cy = one_year, cm = one_month, cd = one_day;

    if (largestUnit == TI_UNIT_YEAR) {
        while (1) {
            int ny = cy + sign;
            int nm = cm;
            int nd = cd;

            int c = compare_calendar_dates(cal, ny, nm, nd,
                                            two_year, two_month, two_day);
            if (sign > 0 && c > 0) break;
            if (sign < 0 && c < 0) break;
            cy = ny; cm = nm; cd = nd;
            years += sign;
        }
    }

    if (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) {
        while (1) {
            int ny = cy;
            int nm = cm + sign;
            int nd = cd;
            if (nm > ti_calendar_months_in_year(cal, ny)) {
                nm = 1;
                ny++;
            } else if (nm < 1) {
                ny--;
                nm = ti_calendar_months_in_year(cal, ny);
            }

            int c = compare_calendar_dates(cal, ny, nm, nd,
                                            two_year, two_month, two_day);
            if (sign > 0 && c > 0) break;
            if (sign < 0 && c < 0) break;
            cy = ny; cm = nm; cd = nd;
            months += sign;
        }
    }

    days = calendar_days_between(cal, cy, cm, cd, two_year, two_month, two_day);

    if (largestUnit == TI_UNIT_WEEK) {
        weeks = days / 7;
        days = days % 7;
    }

    if (out_years) *out_years = years;
    if (out_months) *out_months = months;
    if (out_weeks) *out_weeks = weeks;
    if (out_days) *out_days = days;
}

void ti_calendar_date_until_with_monthcode(CalendarType cal,
    int one_year, int one_month, int one_day,
    int one_mc, BOOL one_has_mc, BOOL one_has_l,
    int two_year, int two_month, int two_day,
    int two_mc, BOOL two_has_mc, BOOL two_has_l,
    int largestUnit,
    int64_t *out_years, int64_t *out_months,
    int64_t *out_weeks, int64_t *out_days)
{
    /* non-ISO calendar date until with monthCode */
    if (cal == CAL_ISO8601 || cal == CAL_GREGORY ||
        cal == CAL_BUDDHIST || cal == CAL_ROC || cal == CAL_JAPANESE) {
        ti_iso_calendar_date_until(one_year, one_month, one_day,
                                    two_year, two_month, two_day, largestUnit,
                                    out_years, out_months, out_weeks, out_days);
        return;
    }

    if (out_years) *out_years = 0;
    if (out_months) *out_months = 0;
    if (out_weeks) *out_weeks = 0;
    if (out_days) *out_days = 0;

    int use_mc = one_has_mc && two_has_mc;
    int cmp;
    if (use_mc) {
        if (one_year != two_year)
            cmp = (one_year < two_year) ? -1 : 1;
        else if (one_mc != two_mc)
            cmp = (one_mc < two_mc) ? -1 : 1;
        else if (one_has_l != two_has_l)
            cmp = one_has_l ? 1 : -1;
        else if (one_day != two_day)
            cmp = (one_day < two_day) ? -1 : 1;
        else
            cmp = 0;
    } else {
        cmp = compare_calendar_dates(cal, one_year, one_month, one_day,
                                      two_year, two_month, two_day);
    }
    if (cmp == 0)
        return;

    int sign = (cmp > 0) ? -1 : 1;
    int64_t years = 0, months = 0, weeks = 0, days = 0;
    int cy = one_year, cm = one_month, cd = one_day;

    if (largestUnit == TI_UNIT_YEAR) {
        years = (int64_t)two_year - (int64_t)one_year;
        if (use_mc) {
            /* Compute monthCode comparison: sign of (target monthCode - source monthCode) */
            int monthCode_cmp = 0;
            if (two_mc > one_mc)
                monthCode_cmp = 1;
            else if (two_mc < one_mc)
                monthCode_cmp = -1;
            else {
                if (two_has_l > one_has_l)
                    monthCode_cmp = 1;
                else if (two_has_l < one_has_l)
                    monthCode_cmp = -1;
                else
                    monthCode_cmp = (two_day > one_day) ? 1 : (two_day < one_day) ? -1 : 0;
            }
            /* If monthCode ordering disagrees with overall ordering, adjust years */
            if (monthCode_cmp * sign < 0)
                years -= sign;

            /* Verify: add years to source and check if it overshoots target */
            if (years != 0) {
                int vy = one_year + (int)years;
                int vm = ti_calendar_month_code_to_month(cal, vy, one_mc, one_has_l);
                if (vm < 0)
                    vm = ti_calendar_month_code_to_month(cal, vy, one_mc, 0);
                int vd = one_day;
                int c = compare_calendar_dates(cal, vy, vm, vd, two_year, two_month, two_day);
                if (sign == 1 && c > 0)
                    years -= 1;
                else if (sign == -1 && c < 0)
                    years += 1;
            }
        } else {
            if (sign == 1 && (two_month < one_month || (two_month == one_month && two_day < one_day)))
                years -= 1;
            else if (sign == -1 && (two_month > one_month || (two_month == one_month && two_day > one_day)))
                years += 1;
        }
        cy = one_year + (int)years;
        if (use_mc) {
            cm = ti_calendar_month_code_to_month(cal, cy, one_mc, one_has_l);
            if (cm < 0)
                cm = ti_calendar_month_code_to_month(cal, cy, one_mc, 0);
        } else {
            cm = one_month;
        }
        cd = one_day;
    }

    if (largestUnit == TI_UNIT_YEAR || largestUnit == TI_UNIT_MONTH) {
        int64_t m = 0;
        int base_year = cy, base_month = cm, base_day = cd;
        while (1) {
            int candidate_year = base_year;
            int candidate_month = base_month + (int)((m + 1) * sign);
            int candidate_day = base_day;
            ti_balance_month(&candidate_year, &candidate_month, cal);

            int c, mc = 0, has_l = 0;
            if (use_mc) {
                ti_calendar_month_to_month_code(cal, candidate_year, candidate_month, &mc, &has_l);
                if (candidate_year != two_year)
                    c = (candidate_year < two_year) ? -1 : 1;
                else if (mc != two_mc)
                    c = (mc < two_mc) ? -1 : 1;
                else if (has_l != two_has_l)
                    c = has_l ? 1 : -1;
                else if (candidate_day != two_day)
                    c = (candidate_day < two_day) ? -1 : 1;
                else
                    c = 0;
            } else {
                c = compare_calendar_dates(cal, candidate_year, candidate_month, candidate_day,
                                            two_year, two_month, two_day);
            }
            if (sign > 0 && c > 0) break;
            if (sign < 0 && c < 0) break;
            m++;
        }
        months = m * sign;

        cy = base_year;
        cm = base_month + (int)months;
        cd = base_day;
        ti_balance_month(&cy, &cm, cal);
    }

    days = calendar_days_between(cal, cy, cm, cd, two_year, two_month, two_day);

    if (largestUnit == TI_UNIT_WEEK) {
        weeks = days / 7;
        days = days % 7;
    }

    if (out_years) *out_years = years;
    if (out_months) *out_months = months;
    if (out_weeks) *out_weeks = weeks;
    if (out_days) *out_days = days;
}

int ti_calendar_month_code_to_month(CalendarType cal, int year, int month_code, int has_l)
{
    if (cal == CAL_HEBREW) {
        if (ti_calendar_is_leap_year(cal, year)) {
            if (month_code == 5 && has_l)
                return 6;
            if (month_code <= 5)
                return month_code;
            return month_code + 1;
        }
        if (month_code == 5 && has_l)
            return 6;
        return month_code;
    }
    if (cal == CAL_CHINESE) {
        const ChineseCalendarEntry *e = lookup_chinese_year(year);
        if (!e || cce_leap_month(e) == 0) {
            if (has_l) return -1;
            return month_code;
        }
        int leapPos = cce_leap_month(e);
        if (month_code == leapPos && has_l)
            return leapPos + 1;
        if (month_code < leapPos) {
            if (has_l) return -1;
            return month_code;
        }
        if (month_code == leapPos && !has_l)
            return month_code;
        if (has_l) return -1;
        return month_code + 1;
    }
    if (cal == CAL_DANGI) {
        const ChineseCalendarEntry *e = lookup_dangi_year(year);
        if (!e || cce_leap_month(e) == 0) {
            if (has_l) return -1;
            return month_code;
        }
        int leapPos = cce_leap_month(e);
        if (month_code == leapPos && has_l)
            return leapPos + 1;
        if (month_code < leapPos) {
            if (has_l) return -1;
            return month_code;
        }
        if (month_code == leapPos && !has_l)
            return month_code;
        if (has_l) return -1;
        return month_code + 1;
    }
    return month_code;
}

static void month_to_month_code_with_leap(int month, int leapPos, int *out_month_code, int *out_has_l)
{
    if (month <= leapPos) {
        *out_month_code = month;
    } else if (month == leapPos + 1) {
        *out_month_code = leapPos;
        *out_has_l = 1;
    } else {
        *out_month_code = month - 1;
    }
}

void ti_calendar_month_to_month_code(CalendarType cal, int year, int month,
                                      int *out_month_code, int *out_has_l)
{
    *out_has_l = 0;
    switch (cal) {
    case CAL_HEBREW:
        if (ti_calendar_is_leap_year(cal, year))
            month_to_month_code_with_leap(month, 5, out_month_code, out_has_l);
        else
            *out_month_code = month;
        break;
    case CAL_CHINESE: {
        const ChineseCalendarEntry *e = lookup_chinese_year(year);
        if (!e || cce_leap_month(e) == 0)
            *out_month_code = month;
        else
            month_to_month_code_with_leap(month, cce_leap_month(e), out_month_code, out_has_l);
        break;
    }
    case CAL_DANGI: {
        const ChineseCalendarEntry *e = lookup_dangi_year(year);
        if (!e || cce_leap_month(e) == 0)
            *out_month_code = month;
        else
            month_to_month_code_with_leap(month, cce_leap_month(e), out_month_code, out_has_l);
        break;
    }
    default:
        *out_month_code = month;
        break;
    }
}
static TIEra ti_calendar_era(CalendarType cal, int iso_year)
{
    if (cal == CAL_GREGORY)
        return iso_year >= 1 ? TI_ERA_CE : TI_ERA_BCE;
    if (cal == CAL_BUDDHIST)
        return TI_ERA_BE;
    if (cal == CAL_COPTIC)
        return TI_ERA_AM;
    if (cal == CAL_ETHIOAA)
        return TI_ERA_AA;
    if (cal == CAL_ETHIOPIC)
        return iso_year >= 1 ? TI_ERA_AM : TI_ERA_AA;
    if (cal == CAL_HEBREW)
        return TI_ERA_AM;
    if (cal == CAL_INDIAN)
        return TI_ERA_SHAKA;
    if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA || cal == CAL_ISLAMIC_UMALQURA)
        return iso_year >= 1 ? TI_ERA_AH : TI_ERA_BH;
    if (cal == CAL_PERSIAN)
        return TI_ERA_AP;
    if (cal == CAL_ROC)
        return iso_year >= 1912 ? TI_ERA_ROC : TI_ERA_BROC;
    if (cal == CAL_JAPANESE) {
        if (iso_year <= 0)
            return TI_ERA_BCE;
        if (iso_year < 1868)
            return TI_ERA_CE;
        if (iso_year < 1912)
            return TI_ERA_MEIJI;
        if (iso_year < 1926)
            return TI_ERA_TAISHO;
        if (iso_year < 1989)
            return TI_ERA_SHOWA;
        if (iso_year < 2019)
            return TI_ERA_HEISEI;
        return TI_ERA_REIWA;
    }
    return TI_ERA_UNKNOWN;
}

int ti_calendar_requires_era(CalendarType cal)
{
    return ti_calendar_era(cal, 0) != TI_ERA_UNKNOWN;
}
static int ti_calendar_era_year(CalendarType cal, int iso_year)
{
    if (cal == CAL_GREGORY)
        return iso_year >= 1 ? iso_year : 1 - iso_year;
    if (cal == CAL_ETHIOPIC)
        return iso_year >= 1 ? iso_year : iso_year + 5500;
    if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA || cal == CAL_ISLAMIC_UMALQURA)
        return iso_year >= 1 ? iso_year : 1 - iso_year;
    if (cal == CAL_ROC) {
        int cal_year = iso_year - 1911;
        return cal_year >= 1 ? cal_year : 1 - cal_year;
    }
    if (cal == CAL_BUDDHIST)
        return iso_year + 543;
    if (cal == CAL_JAPANESE) {
        if (iso_year <= 0)
            return 1 - iso_year;
        if (iso_year < 1868)
            return iso_year;
        if (iso_year < 1912)
            return iso_year - 1867;
        if (iso_year < 1926)
            return iso_year - 1911;
        if (iso_year < 1989)
            return iso_year - 1925;
        if (iso_year < 2019)
            return iso_year - 1988;
        return iso_year - 2018;
    }
    /* For single-era calendars, eraYear equals ISO year */
    if (cal == CAL_BUDDHIST || cal == CAL_COPTIC || cal == CAL_ETHIOAA ||
        cal == CAL_HEBREW || cal == CAL_INDIAN ||
        cal == CAL_PERSIAN)
        return iso_year;
    return iso_year;
}
static const char *ti_japanese_era_from_date(int year, int month, int day)
{
    if (year <= 0)
        return "bce";
    if (year < 1873)
        return "ce";
    if (year < 1912)
        return "meiji";
    if (year == 1912 && (month < 7 || (month == 7 && day < 30)))
        return "meiji";
    if (year < 1926)
        return "taisho";
    if (year == 1926 && (month < 12 || (month == 12 && day < 25)))
        return "taisho";
    if (year < 1989)
        return "showa";
    if (year == 1989 && (month < 1 || (month == 1 && day < 8)))
        return "showa";
    if (year < 2019)
        return "heisei";
    if (year == 2019 && (month < 5 || (month == 5 && day < 1)))
        return "heisei";
    return "reiwa";
}
static int ti_japanese_era_year_from_date(int year, int month, int day)
{
    const char *era = ti_japanese_era_from_date(year, month, day);
    if (!strcmp(era, "bce")) return 1 - year;
    if (!strcmp(era, "meiji")) return year - 1867;
    if (!strcmp(era, "taisho")) return year - 1911;
    if (!strcmp(era, "showa")) return year - 1925;
    if (!strcmp(era, "heisei")) return year - 1988;
    if (!strcmp(era, "reiwa")) return year - 2018;
    return year;
}
void ti_calendar_era_and_era_year(CalendarType cal, int iso_year, int iso_month, int iso_day,
                                                int cal_year, TIEra *era, int *era_year)
{
    if (cal == CAL_JAPANESE) {
        *era = ti_string_to_era(ti_japanese_era_from_date(iso_year, iso_month, iso_day));
        *era_year = ti_japanese_era_year_from_date(iso_year, iso_month, iso_day);
    } else if (cal == CAL_ETHIOPIC) {
        if (cal_year >= 1) {
            *era = TI_ERA_AM;
            *era_year = cal_year;
        } else {
            *era = TI_ERA_AA;
            *era_year = cal_year + 5500;
        }
    } else if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA || cal == CAL_ISLAMIC_UMALQURA) {
        *era = cal_year >= 1 ? TI_ERA_AH : TI_ERA_BH;
        *era_year = cal_year >= 1 ? cal_year : 1 - cal_year;
    } else {
        *era = ti_calendar_era(cal, iso_year);
        if (*era != TI_ERA_UNKNOWN) {
            if (cal == CAL_GREGORY || cal == CAL_ROC)
                *era_year = ti_calendar_era_year(cal, iso_year);
            else
                *era_year = cal_year;
        } else {
            *era_year = cal_year;
        }
    }
}
/* Find the ISO reference date for a PlainMonthDay with the given monthCode and day.
   Returns 0 on success, -1 on failure (with exception already thrown if applicable).
   On success, sets *iso_y, *iso_m, *iso_d.
   overflow_year: user-provided year for overflow validation (0 if not provided). */
int ti_calendar_monthday_iso_reference_date(CalendarType cal, int month, int has_l, int day,
                                               int overflow_constrain, int overflow_year,
                                               int *iso_y, int *iso_m, int *iso_d)
{
    int ref_iso_year = 1972;
    int max_day = -1;
    int dim;

    /* ISO/Gregorian-based calendars (including Buddhist, ROC, Japanese) always
       use referenceISOYear = 1972. Overflow uses overflow_year if provided,
       otherwise 1972. */
    if (cal == CAL_ISO8601 || cal == CAL_GREGORY || cal == CAL_JAPANESE ||
        cal == CAL_BUDDHIST || cal == CAL_ROC) {
        int y = overflow_year ? overflow_year : 1972;
        if (month > 12) {
            if (!overflow_constrain)
                return -1;
            month = 12;
        }
        dim = pd_days_in_month(y, month);
        if (day > dim) {
            if (!overflow_constrain)
                return -1;
            day = dim;
        } else if (day < 1) {
            return -1;
        }
        *iso_y = 1972;
        *iso_m = month;
        *iso_d = day;
        return 0;
    }

    /* For non-ISO calendars, if overflow_year is provided, validate against that year */
    if (overflow_year != 0) {
        int resolved_month = ti_calendar_month_code_to_month(cal, overflow_year, month, has_l);
        if (resolved_month < 0) {
            /* monthCode doesn't exist in the specified year */
            if (!overflow_constrain)
                return -1;
            if (has_l) {
                /* Fall back to non-leap month */
                has_l = 0;
                resolved_month = ti_calendar_month_code_to_month(cal, overflow_year, month, 0);
            }
            if (resolved_month < 0) {
                /* Still doesn't exist, constrain to month 1 */
                month = 1;
                has_l = 0;
                resolved_month = ti_calendar_month_code_to_month(cal, overflow_year, 1, 0);
                if (resolved_month < 0) resolved_month = 1;
            }
        }
        dim = ti_calendar_days_in_month(cal, overflow_year, resolved_month);
        if (day > dim) {
            if (!overflow_constrain)
                return -1;
            day = dim;
        } else if (day < 1) {
            return -1;
        }
        /* After overflow, proceed with monthCode-based reference year search */
    } else {
        /* No overflow_year: use maxLengthOfAdjustedMonthCodeInAnyYear logic */
        if (cal == CAL_CHINESE || cal == CAL_DANGI) {
            if (has_l) {
                /* Search for leap month reference year per spec Table 6:
                   1. Latest year in [1900, 1972] with enough days
                   2. Earliest year >= 1973 with enough days
                   3. Latest year < 1900 with enough days */
                int search_year;
                int leap_found = 0;
                int leap_year = -1;
                int leap_max_dim = 0;
                int leap_max_dim_year = 1972;

                /* M01L and M12L are not known to have occurred in the calculable range */
                if (month == 1 || month == 12) {
                    if (!overflow_constrain)
                        return -1;
                    has_l = 0;
                } else if (day >= 30 && (month == 2 || month == 8 || month == 9 || month == 10 || month == 11)) {
                    /* M02L, M08L-M11L are not known to commonly have 30 days */
                    if (!overflow_constrain)
                        return -1;
                    has_l = 0;
                } else {
                    for (search_year = 1972; search_year >= 1900; search_year--) {
                        const ChineseCalendarEntry *e = (cal == CAL_CHINESE) ?
                            lookup_chinese_year(search_year) : lookup_dangi_year(search_year);
                        if (e && cce_leap_month(e) == month) {
                            int ordinal_month = month + 1;
                            int month_days = ti_calendar_days_in_month(cal, search_year, ordinal_month);
                            if (month_days >= day) {
                                leap_found = 1;
                                leap_year = search_year;
                                break;
                            }
                            if (month_days > leap_max_dim) {
                                leap_max_dim = month_days;
                                leap_max_dim_year = search_year;
                            }
                        }
                    }
                    if (!leap_found) {
                        for (search_year = 1973; ; search_year++) {
                            const ChineseCalendarEntry *e = (cal == CAL_CHINESE) ?
                                lookup_chinese_year(search_year) : lookup_dangi_year(search_year);
                            if (!e)
                                break;
                            if (cce_leap_month(e) == month) {
                                int ordinal_month = month + 1;
                                int month_days = ti_calendar_days_in_month(cal, search_year, ordinal_month);
                                if (month_days >= day) {
                                    leap_found = 1;
                                    leap_year = search_year;
                                    break;
                                }
                                if (month_days > leap_max_dim) {
                                    leap_max_dim = month_days;
                                    leap_max_dim_year = search_year;
                                }
                            }
                        }
                    }
                    if (!leap_found) {
                        for (search_year = 1899; search_year >= 1800; search_year--) {
                            const ChineseCalendarEntry *e = (cal == CAL_CHINESE) ?
                                lookup_chinese_year(search_year) : lookup_dangi_year(search_year);
                            if (e && cce_leap_month(e) == month) {
                                int ordinal_month = month + 1;
                                int month_days = ti_calendar_days_in_month(cal, search_year, ordinal_month);
                                if (month_days >= day) {
                                    leap_found = 1;
                                    leap_year = search_year;
                                    break;
                                }
                                if (month_days > leap_max_dim) {
                                    leap_max_dim = month_days;
                                    leap_max_dim_year = search_year;
                                }
                            }
                        }
                    }
                    if (leap_found) {
                        int ordinal_month = month + 1;
                        if (ti_calendar_date_to_iso(cal, leap_year, ordinal_month, day, iso_y, iso_m, iso_d) < 0)
                            return -1;
                        return 0;
                    } else if (leap_max_dim >= 30 && overflow_constrain) {
                        int ordinal_month = month + 1;
                        if (ti_calendar_date_to_iso(cal, leap_max_dim_year, ordinal_month, leap_max_dim, iso_y, iso_m, iso_d) < 0)
                            return -1;
                        return 0;
                    } else if (leap_max_dim > 0 && overflow_constrain) {
                        has_l = 0;
                    } else {
                        if (!overflow_constrain)
                            return -1;
                        has_l = 0;
                    }
                }
            }
            max_day = 30;
        } else if (cal == CAL_HEBREW) {
            if (has_l) {
                max_day = (month == 5) ? 30 : -1;
            } else {
                switch (month) {
                case 1: case 2: case 3: case 5: case 7: case 9: case 11: max_day = 30; break;
                case 4: case 6: case 8: case 10: case 12: max_day = 29; break;
                default: max_day = -1; break;
                }
            }
        } else if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA) {
            if (has_l) {
                max_day = -1;
            } else {
                max_day = (month == 12) ? 30 : ((month % 2 == 1) ? 30 : 29);
            }
        } else if (cal == CAL_ISLAMIC_UMALQURA) {
            if (has_l) max_day = -1;
            else max_day = 30;
        } else if (cal == CAL_PERSIAN) {
            if (has_l) max_day = -1;
            else max_day = (month <= 6) ? 31 : 30;
        } else if (cal == CAL_INDIAN) {
            if (has_l) max_day = -1;
            else max_day = (month >= 1 && month <= 6) ? 31 : 30;
        } else if (cal == CAL_COPTIC || cal == CAL_ETHIOPIC || cal == CAL_ETHIOAA) {
            if (has_l) max_day = -1;
            else max_day = (month == 13) ? 6 : 30;
        }
        if (max_day < 0) {
            if (!overflow_constrain)
                return -1;
            month = 1;
            has_l = 0;
            max_day = 30;
        }
        if (day > max_day) {
            if (!overflow_constrain)
                return -1;
            day = max_day;
        }
        if (day < 1)
            return -1;
    }

    /* Determine reference ISO year based on calendar */
    if (cal == CAL_HEBREW) {
        if (month == 5 && has_l)
            ref_iso_year = 1970;
        else if ((month == 2 || month == 3) && day == 30)
            ref_iso_year = 1971;
    } else if ((cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA) && month == 12 && day == 30) {
        ref_iso_year = 1971;
    }

    {
        int cal_y, cal_m, cal_d;
        int cal_mc, cal_has_l;
        int calendar_year;
        int test_y, test_m, test_d;
        int rt_y, rt_m, rt_d;
        int rt_mc, rt_has_l;

        /* Convert ISO (ref_iso_year, 12, 31) to calendar */
        ti_iso_to_calendar_date(cal, ref_iso_year, 12, 31, &cal_y, &cal_m, &cal_d);
        ti_calendar_month_to_month_code(cal, cal_y, cal_m, &cal_mc, &cal_has_l);

        /* Determine calendarYear based on monthCode comparison */
        if (cal_mc > month || (cal_mc == month && cal_has_l > has_l) ||
            (cal_mc == month && cal_has_l == has_l && cal_d >= day)) {
            calendar_year = cal_y;
        } else {
            calendar_year = cal_y - 1;
        }

        /* Search backwards from calendar_year until a valid reference year is found */
        {
            int ty;
            for (ty = calendar_year; ty >= calendar_year - 5000 && ty >= 1; ty--) {
                int tm = ti_calendar_month_code_to_month(cal, ty, month, has_l);
                if (tm < 0)
                    continue;
                if (ti_calendar_date_to_iso(cal, ty, tm, day, &test_y, &test_m, &test_d) < 0)
                    continue;
                ti_iso_to_calendar_date(cal, test_y, test_m, test_d, &rt_y, &rt_m, &rt_d);
                ti_calendar_month_to_month_code(cal, rt_y, rt_m, &rt_mc, &rt_has_l);
                if (rt_mc == month && rt_has_l == has_l && rt_d == day) {
                    *iso_y = test_y;
                    *iso_m = test_m;
                    *iso_d = test_d;
                    return 0;
                }
            }
        }
    }

    return -1;
}

static const char *era_strings[] = {
    NULL, "ce", "bce", "be", "am", "aa", "shaka", "ap", "ah", "bh",
    "roc", "broc", "meiji", "taisho", "showa", "heisei", "reiwa"
};

TIEra ti_string_to_era(const char *era)
{
    int i;
    for (i = TI_ERA_CE; i <= TI_ERA_REIWA; i++) {
        if (!strcmp(era, era_strings[i]))
            return i;
    }
    if (!strcmp(era, "ad"))
        return TI_ERA_CE;
    if (!strcmp(era, "bc"))
        return TI_ERA_BCE;
    return TI_ERA_UNKNOWN;
}

const char *ti_era_to_string(TIEra era)
{
    if (era >= TI_ERA_CE && era <= TI_ERA_REIWA)
        return era_strings[era];
    return NULL;
}
int ti_calendar_validate_month_code(CalendarType cal, int month_from_code, int has_l)
{
    int max_numeric;
    switch (cal) {
    case CAL_COPTIC:
    case CAL_ETHIOAA:
    case CAL_ETHIOPIC:
        max_numeric = 13;
        break;
    default:
        max_numeric = 12;
        break;
    }
    if (month_from_code < 1 || month_from_code > max_numeric)
        return 0;
    if (!has_l)
        return 1;
    /* Leap month codes: only certain calendars support them */
    if (cal == CAL_CHINESE || cal == CAL_DANGI) {
        /* Chinese/Dangi support M01L-M12L */
        return month_from_code >= 1 && month_from_code <= 12;
    }
    if (cal == CAL_HEBREW) {
        /* Hebrew only supports M05L (Adar I in leap years) */
        return month_from_code == 5;
    }
    /* No other calendars support leap month codes */
    return 0;
}
int ti_calendar_era_to_iso_year(CalendarType cal, TIEra era, int eraYear, int *iso_year)
{
    if (cal == CAL_GREGORY) {
        if (era == TI_ERA_CE) {
            *iso_year = eraYear;
            return 0;
        } else if (era == TI_ERA_BCE) {
            *iso_year = 1 - eraYear;
            return 0;
        }
        return -1;
    }
    if (cal == CAL_JAPANESE) {
        if (era == TI_ERA_CE) {
            *iso_year = eraYear;
            return 0;
        } else if (era == TI_ERA_BCE) {
            *iso_year = 1 - eraYear;
            return 0;
        } else if (era == TI_ERA_MEIJI) {
            *iso_year = 1867 + eraYear;
            return 0;
        } else if (era == TI_ERA_TAISHO) {
            *iso_year = 1911 + eraYear;
            return 0;
        } else if (era == TI_ERA_SHOWA) {
            *iso_year = 1925 + eraYear;
            return 0;
        } else if (era == TI_ERA_HEISEI) {
            *iso_year = 1988 + eraYear;
            return 0;
        } else if (era == TI_ERA_REIWA) {
            *iso_year = 2018 + eraYear;
            return 0;
        }
        return -1;
    }
    if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA || cal == CAL_ISLAMIC_UMALQURA) {
        if (era == TI_ERA_AH) {
            *iso_year = eraYear;
            return 0;
        } else if (era == TI_ERA_BH) {
            *iso_year = 1 - eraYear;
            return 0;
        }
        return -1;
    }
    if (cal == CAL_ROC) {
        if (era == TI_ERA_ROC) {
            *iso_year = eraYear + 1911;
            return 0;
        } else if (era == TI_ERA_BROC) {
            *iso_year = 1912 - eraYear;
            return 0;
        }
        return -1;
    }
    if (cal == CAL_ETHIOPIC) {
        if (era == TI_ERA_AM) {
            *iso_year = eraYear;
            return 0;
        } else if (era == TI_ERA_AA) {
            *iso_year = eraYear - 5500;
            return 0;
        }
        return -1;
    }
    /* Single-era calendars */
    if (cal == CAL_BUDDHIST && era == TI_ERA_BE) {
        *iso_year = eraYear - 543;
        return 0;
    } else if (cal == CAL_COPTIC && era == TI_ERA_AM) {
        *iso_year = eraYear;
        return 0;
    } else if (cal == CAL_ETHIOAA && era == TI_ERA_AA) {
        *iso_year = eraYear;
        return 0;
    } else if (cal == CAL_HEBREW && era == TI_ERA_AM) {
        *iso_year = eraYear;
        return 0;
    } else if (cal == CAL_INDIAN && era == TI_ERA_SHAKA) {
        *iso_year = eraYear;
        return 0;
    } else if (cal == CAL_PERSIAN && era == TI_ERA_AP) {
        *iso_year = eraYear;
        return 0;
    }
    return -1;
}
int ti_calendar_era_to_calendar_year(CalendarType cal, TIEra era, int eraYear, int *cal_year)
{
    if (cal == CAL_GREGORY) {
        if (era == TI_ERA_CE) { *cal_year = eraYear; return 0; }
        if (era == TI_ERA_BCE) { *cal_year = 1 - eraYear; return 0; }
        return -1;
    }
    if (cal == CAL_JAPANESE) {
        if (era == TI_ERA_CE) { *cal_year = eraYear; return 0; }
        if (era == TI_ERA_BCE) { *cal_year = 1 - eraYear; return 0; }
        if (era == TI_ERA_MEIJI) { *cal_year = 1867 + eraYear; return 0; }
        if (era == TI_ERA_TAISHO) { *cal_year = 1911 + eraYear; return 0; }
        if (era == TI_ERA_SHOWA) { *cal_year = 1925 + eraYear; return 0; }
        if (era == TI_ERA_HEISEI) { *cal_year = 1988 + eraYear; return 0; }
        if (era == TI_ERA_REIWA) { *cal_year = 2018 + eraYear; return 0; }
        return -1;
    }
    if (cal == CAL_ISLAMIC_CIVIL || cal == CAL_ISLAMIC_TBLA || cal == CAL_ISLAMIC_UMALQURA) {
        if (era == TI_ERA_AH) { *cal_year = eraYear; return 0; }
        if (era == TI_ERA_BH) { *cal_year = 1 - eraYear; return 0; }
        return -1;
    }
    if (cal == CAL_ROC) {
        if (era == TI_ERA_ROC) { *cal_year = eraYear; return 0; }
        if (era == TI_ERA_BROC) { *cal_year = 1 - eraYear; return 0; }
        return -1;
    }
    if (cal == CAL_ETHIOPIC) {
        if (era == TI_ERA_AM) { *cal_year = eraYear; return 0; }
        if (era == TI_ERA_AA) { *cal_year = eraYear - 5500; return 0; }
        return -1;
    }
    /* Single-era calendars: eraYear is the calendar year */
    if ((cal == CAL_BUDDHIST && era == TI_ERA_BE) ||
        (cal == CAL_COPTIC && era == TI_ERA_AM) ||
        (cal == CAL_ETHIOAA && era == TI_ERA_AA) ||
        (cal == CAL_HEBREW && era == TI_ERA_AM) ||
        (cal == CAL_INDIAN && era == TI_ERA_SHAKA) ||
        (cal == CAL_PERSIAN && era == TI_ERA_AP)) {
        *cal_year = eraYear;
        return 0;
    }
    return -1;
}
void pdt_balance_month_calendar(int *year, int *month, CalendarType cal)
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
void pdt_balance_date_calendar(int *year, int *month, int *day, CalendarType cal)
{
    int dim;
    pdt_balance_month_calendar(year, month, cal);
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
}
