/*
 * C utilities
 * 
 * Copyright (c) 2017 Fabrice Bellard
 * Copyright (c) 2018 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#ifndef CUTILS_H
#define CUTILS_H

#include <stdlib.h>
#include <inttypes.h>

/* set if CPU is big endian */
#undef WORDS_BIGENDIAN

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>
#include <winsock.h>

#include "win32/stdatomic.h"
#define _Atomic(x)                     x

#include <pthread.h> /* needed for timespec */

#define likely(x)                      (x)
#define unlikely(x)                    (x)
#define force_inline                   __forceinline
#define no_inline                      __declspec(noinline)
#define __maybe_unused
#define util_packed(_declaration)      __pragma(pack(push, 1)) _declaration __pragma(pack(pop))
#define util_format(...)

#define js_popen                       _popen
#define js_pclose                      _pclose
#else

#define likely(x)                      __builtin_expect(!!(x), 1)
#define unlikely(x)                    __builtin_expect(!!(x), 0)
#define force_inline                   inline __attribute__((always_inline))
#define no_inline                      __attribute__((noinline))
#define __maybe_unused                 __attribute__((unused))
#define util_packed(_declaration)      _declaration __attribute__((packed))
#define util_format(...)               util_format(__VA_ARGS__))

#define js_popen                       popen
#define js_pclose                      pclose
#endif

#if !defined(_SSIZE_T_DEFINED)
#if defined(_WIN64)
typedef unsigned __int64    ssize_t;
#elif defined(_WIN32)
typedef _W64 unsigned int   ssize_t;
#endif
#define _SSIZE_T_DEFINED
#endif

#if defined(_WIN32)
static int js_gettimeofday(struct timeval* tv, void* tz)
{
    (void)tz;

    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970 
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

    SYSTEMTIME system_time;
    FILETIME file_time;
    uint64_t time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);

    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tv->tv_sec = (long)((time - EPOCH) / 10000000L);
    tv->tv_usec = (long)(system_time.wMilliseconds * 1000);

    return 0;
}

static int js_clock_getrealtime(struct timespec* ts)
{
    /* https://stackoverflow.com/a/5404467 */

    LARGE_INTEGER t;
    FILETIME f;
    double microseconds;
    static LARGE_INTEGER offset;
    static double frequencyToMicroseconds;
    static int initialized = 0;
    static BOOL usePerformanceCounter = 0;

    if (!initialized)
    {
        LARGE_INTEGER performanceFrequency;
        initialized = 1;
        usePerformanceCounter = QueryPerformanceFrequency(&performanceFrequency);
        if (usePerformanceCounter)
        {
            QueryPerformanceCounter(&offset);
            frequencyToMicroseconds = (double)performanceFrequency.QuadPart / 1000000.;
        }
        else
        {
            SYSTEMTIME s;
            s.wYear = 1970;
            s.wMonth = 1;
            s.wDay = 1;
            s.wHour = 0;
            s.wMinute = 0;
            s.wSecond = 0;
            s.wMilliseconds = 0;
            SystemTimeToFileTime(&s, &f);

            offset.QuadPart = f.dwHighDateTime;
            offset.QuadPart <<= 32;
            offset.QuadPart |= f.dwLowDateTime;

            frequencyToMicroseconds = 10.0;
        }
    }

    if (usePerformanceCounter)
    {
        QueryPerformanceCounter(&t);
    }
    else
    {
        GetSystemTimeAsFileTime(&f);
        t.QuadPart = f.dwHighDateTime;
        t.QuadPart <<= 32;
        t.QuadPart |= f.dwLowDateTime;
    }

    t.QuadPart -= offset.QuadPart;
    microseconds = (double)t.QuadPart / frequencyToMicroseconds;
    t.QuadPart = microseconds;

    ts->tv_sec = t.QuadPart / 1000000;
    ts->tv_nsec = t.QuadPart % 1000000;

    return 0;
}

static int js_clock_getmonotonic(struct timespec* ts)
{
    return js_clock_getrealtime(ts);
}
#else
#define js_gettimeofday gettimeofday

static int js_clock_getrealtime(struct timespec* ts)
{
    return clock_gettime(CLOCK_REALTIME, ts);
}

static int js_clock_getmonotonic(struct timespec* ts)
{
    return clock_gettime(CLOCK_MONOTONIC, ts);
}

#define js_popen popen
#endif

#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)
#define stringify(s)    tostring(s)
#define tostring(s)     #s

#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif
#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef int BOOL;

#ifndef FALSE
enum {
    FALSE = 0,
    TRUE = 1,
};
#endif

void pstrcpy(char *buf, int buf_size, const char *str);
char *pstrcat(char *buf, int buf_size, const char *s);
int strstart(const char *str, const char *val, const char **ptr);
int has_suffix(const char *str, const char *suffix);

static inline int max_int(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int min_int(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline uint32_t max_uint32(uint32_t a, uint32_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline uint32_t min_uint32(uint32_t a, uint32_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline int64_t max_int64(int64_t a, int64_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int64_t min_int64(int64_t a, int64_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

/* WARNING: undefined if a = 0 */
static force_inline int clz32(unsigned int a)
{
#ifdef _WIN32
    uint32_t leading_zero = 0;
    /* failing to bit scan is undefined, returning 32 instead */
    return (_BitScanReverse(&leading_zero, a)) ? (31 - leading_zero) : 32;
#else
    return __builtin_clz(a);
#endif
}

/* WARNING: undefined if a = 0 */
static force_inline int clz64(uint64_t a)
{
#ifdef _WIN32
    uint32_t leading_zero = 0;
    /* failing to bit scan is undefined, returning 64 instead */
    return (_BitScanReverse64(&leading_zero, a)) ? (63 - leading_zero) : 32;
#else
    return __builtin_clzll(a);
#endif
}

/* WARNING: undefined if a = 0 */
static inline int ctz32(unsigned int a)
{
#ifdef _WIN32
    uint32_t trailing_zero = 0;
    /* failing to bit scan is undefined, returning 32 instead */
    return (_BitScanForward(&trailing_zero, a)) ? trailing_zero : 32;
#else
    return __builtin_ctz(a);
#endif
}

/* WARNING: undefined if a = 0 */
static inline int ctz64(uint64_t a)
{
#ifdef _WIN32
    uint32_t trailing_zero = 0;
    /* failing to bit scan is undefined, returning 64 instead */
    return (_BitScanForward64(&trailing_zero, a)) ? trailing_zero : 64;
#else
    return __builtin_ctzll(a);
#endif
}

util_packed(struct packed_u64 {
    uint64_t v;
});

util_packed(struct packed_u32 {
    uint32_t v;
});

util_packed(struct packed_u16 {
    uint16_t v;
});

static inline uint64_t get_u64(const uint8_t *tab)
{
    return ((const struct packed_u64 *)tab)->v;
}

static inline int64_t get_i64(const uint8_t *tab)
{
    return (int64_t)((const struct packed_u64 *)tab)->v;
}

static inline void put_u64(uint8_t *tab, uint64_t val)
{
    ((struct packed_u64 *)tab)->v = val;
}

static inline uint32_t get_u32(const uint8_t *tab)
{
    return ((const struct packed_u32 *)tab)->v;
}

static inline int32_t get_i32(const uint8_t *tab)
{
    return (int32_t)((const struct packed_u32 *)tab)->v;
}

static inline void put_u32(uint8_t *tab, uint32_t val)
{
    ((struct packed_u32 *)tab)->v = val;
}

static inline uint32_t get_u16(const uint8_t *tab)
{
    return ((const struct packed_u16 *)tab)->v;
}

static inline int32_t get_i16(const uint8_t *tab)
{
    return (int16_t)((const struct packed_u16 *)tab)->v;
}

static inline void put_u16(uint8_t *tab, uint16_t val)
{
    ((struct packed_u16 *)tab)->v = val;
}

static inline uint32_t get_u8(const uint8_t *tab)
{
    return *tab;
}

static inline int32_t get_i8(const uint8_t *tab)
{
    return (int8_t)*tab;
}

static inline void put_u8(uint8_t *tab, uint8_t val)
{
    *tab = val;
}

static inline uint16_t bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static inline uint32_t bswap32(uint32_t v)
{
    return ((v & 0xff000000) >> 24) | ((v & 0x00ff0000) >>  8) |
        ((v & 0x0000ff00) <<  8) | ((v & 0x000000ff) << 24);
}

static inline uint64_t bswap64(uint64_t v)
{
    return ((v & ((uint64_t)0xff << (7 * 8))) >> (7 * 8)) | 
        ((v & ((uint64_t)0xff << (6 * 8))) >> (5 * 8)) | 
        ((v & ((uint64_t)0xff << (5 * 8))) >> (3 * 8)) | 
        ((v & ((uint64_t)0xff << (4 * 8))) >> (1 * 8)) | 
        ((v & ((uint64_t)0xff << (3 * 8))) << (1 * 8)) | 
        ((v & ((uint64_t)0xff << (2 * 8))) << (3 * 8)) | 
        ((v & ((uint64_t)0xff << (1 * 8))) << (5 * 8)) | 
        ((v & ((uint64_t)0xff << (0 * 8))) << (7 * 8));
}

/* XXX: should take an extra argument to pass slack information to the caller */
typedef void *DynBufReallocFunc(void *opaque, void *ptr, size_t size);

typedef struct DynBuf {
    uint8_t *buf;
    size_t size;
    size_t allocated_size;
    BOOL error; /* true if a memory allocation error occurred */
    DynBufReallocFunc *realloc_func;
    void *opaque; /* for realloc_func */
} DynBuf;

void dbuf_init(DynBuf *s);
void dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func);
int dbuf_realloc(DynBuf *s, size_t new_size);
int dbuf_write(DynBuf *s, size_t offset, const uint8_t *data, size_t len);
int dbuf_put(DynBuf *s, const uint8_t *data, size_t len);
int dbuf_put_self(DynBuf *s, size_t offset, size_t len);
int dbuf_putc(DynBuf *s, uint8_t c);
int dbuf_putstr(DynBuf *s, const char *str);
static inline int dbuf_put_u16(DynBuf *s, uint16_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 2);
}
static inline int dbuf_put_u32(DynBuf *s, uint32_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 4);
}
static inline int dbuf_put_u64(DynBuf *s, uint64_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 8);
}
int util_format(printf, 2, 3) dbuf_printf(DynBuf *s,
                                                      const char *fmt, ...);
void dbuf_free(DynBuf *s);
static inline BOOL dbuf_error(DynBuf *s) {
    return s->error;
}
static inline void dbuf_set_error(DynBuf *s)
{
    s->error = TRUE;
}

#define UTF8_CHAR_LEN_MAX 6

int unicode_to_utf8(uint8_t *buf, unsigned int c);
int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);

static inline int from_hex(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
}

void rqsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *, void *),
            void *arg);

#endif  /* CUTILS_H */
