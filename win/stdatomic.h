#ifndef __STDATOMIC_H__
#define __STDATOMIC_H__

/*
 *  Copyright (c) 2015-2021 Christoph Schreiber
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (http://www.boost.org/LICENSE_1_0.txt)
 */

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L) \
    && !defined(__STDC_NO_ATOMICS__)
#   include <stdatomic.h>
#elif defined(_MSC_VER) && (_MSC_VER >= 1800) /* Visual Studio 2013 */ \
    && (defined(_M_X64) || defined(_M_IX86))
#   define USE_TEMPORARY_MSVC_WORKAROUND 1
#else
#   error Atomic operations are not supported on your platform
#endif /* defined(__STDC_VERSION__) ... */

#if defined(USE_TEMPORARY_MSVC_WORKAROUND)

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <intrin.h>

#pragma intrinsic(_ReadWriteBarrier)
#pragma intrinsic(_InterlockedIncrement)
#pragma intrinsic(_InterlockedExchange8)
#pragma intrinsic(_InterlockedExchange16)
#pragma intrinsic(_InterlockedExchange)
#if defined(_M_X64)
#   pragma intrinsic(_InterlockedExchange64)
#endif /* defined(_M_X64) */
#pragma intrinsic(_InterlockedCompareExchange8)
#pragma intrinsic(_InterlockedCompareExchange16)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)
#pragma intrinsic(_InterlockedExchangeAdd8)
#pragma intrinsic(_InterlockedExchangeAdd16)
#pragma intrinsic(_InterlockedExchangeAdd)
#if defined(_M_X64)
#   pragma intrinsic(_InterlockedExchangeAdd64)
#endif /* defined(_M_X64) */
#pragma intrinsic(_InterlockedOr8)
#pragma intrinsic(_InterlockedOr16)
#pragma intrinsic(_InterlockedOr)
#if defined(_M_X64)
#   pragma intrinsic(_InterlockedOr64)
#endif /* defined(_M_X64) */
#pragma intrinsic(_InterlockedXor8)
#pragma intrinsic(_InterlockedXor16)
#pragma intrinsic(_InterlockedXor)
#if defined(_M_X64)
#   pragma intrinsic(_InterlockedXor64)
#endif /* defined(_M_X64) */
#pragma intrinsic(_InterlockedAnd8)
#pragma intrinsic(_InterlockedAnd16)
#pragma intrinsic(_InterlockedAnd)
#if defined(_M_X64)
#   pragma intrinsic(_InterlockedAnd64)
#endif /* defined(_M_X64) */

/*
 *  7.17.1 Atomic lock-free macros
 */

#define ATOMIC_BOOL_LOCK_FREE 2
#define ATOMIC_CHAR_LOCK_FREE 2
#define ATOMIC_CHAR16_T_LOCK_FREE 2
#define ATOMIC_CHAR32_T_LOCK_FREE 2
#define ATOMIC_WCHAR_T_LOCK_FREE 2
#define ATOMIC_SHORT_LOCK_FREE 2
#define ATOMIC_INT_LOCK_FREE 2
#define ATOMIC_LONG_LOCK_FREE 2
#define ATOMIC_LLONG_LOCK_FREE 2
#define ATOMIC_POINTER_LOCK_FREE 2

/*
 *  7.17.2 Initialization
 */

#define ATOMIC_VAR_INIT(value) (value)

#define atomic_init(obj, desired) (void)(*(obj) = (desired))

/*
 *  7.17.3 Order and consistency
 */

typedef enum memory_order
{
    memory_order_relaxed,
    memory_order_consume,
    memory_order_acquire,
    memory_order_release,
    memory_order_acq_rel,
    memory_order_seq_cst
} memory_order;

#define kill_dependency(y) (y)

/*
 *  7.17.4 Fences
 */

static __forceinline void atomic_thread_fence(memory_order order)
{
    if (order == memory_order_seq_cst)
    {
        long addend = 0;
        _InterlockedIncrement(&addend);
    }
    else if (order != memory_order_relaxed)
    {
        _ReadWriteBarrier();
    }
}

static __forceinline void atomic_signal_fence(memory_order order)
{
    if (order != memory_order_relaxed)
        _ReadWriteBarrier();
}

/*
 *  7.17.5 Lock-free property
 */

#define	atomic_is_lock_free(obj) \
    (sizeof((obj)->val) <= sizeof(__int64))

/*
 *  7.17.6 Atomic integer types
 */

/*
 *  _Atomic - Starting in Visual Studio 2019 version 16.8, this
 *  keyword is recognized but not supported by the compiler in
 *  code compiled as C when the /std:c11 or /std:c17 compiler
 *  options are specified.
 *
 *  MSDN: "You can't redefine keywords. However, you can specify
 *  text to replace keywords before compilation by using
 *  C preprocessor directives."
 */

#define _Atomic(T) T

typedef _Atomic(bool)               atomic_bool;
typedef _Atomic(char)               atomic_char;
typedef _Atomic(unsigned char)      atomic_uchar;
typedef _Atomic(short)              atomic_short;
typedef _Atomic(unsigned short)     atomic_ushort;
typedef _Atomic(int)                atomic_int;
typedef _Atomic(unsigned int)       atomic_uint;
typedef _Atomic(long)               atomic_long;
typedef _Atomic(unsigned long)      atomic_ulong;
typedef _Atomic(__int64)            atomic_llong;
typedef _Atomic(unsigned __int64)   atomic_ullong;
typedef _Atomic(uint_least16_t)     atomic_wchar_t;
typedef _Atomic(uint_least16_t)     atomic_char16_t;
typedef _Atomic(uint_least32_t)     atomic_char32_t;
typedef _Atomic(int_least8_t)       atomic_int_least8_t;
typedef _Atomic(uint_least8_t)      atomic_uint_least8_t;
typedef _Atomic(int_least16_t)      atomic_int_least16_t;
typedef _Atomic(uint_least16_t)     atomic_uint_least16_t;
typedef _Atomic(int_least32_t)      atomic_int_least32_t;
typedef _Atomic(uint_least32_t)     atomic_uint_least32_t;
typedef _Atomic(int_least64_t)      atomic_int_least64_t;
typedef _Atomic(uint_least64_t)     atomic_uint_least64_t;
typedef _Atomic(int_fast8_t)        atomic_int_fast8_t;
typedef _Atomic(uint_fast8_t)       atomic_uint_fast8_t;
typedef _Atomic(int_fast16_t)       atomic_int_fast16_t;
typedef _Atomic(uint_fast16_t)      atomic_uint_fast16_t;
typedef _Atomic(int_fast32_t)       atomic_int_fast32_t;
typedef _Atomic(uint_fast32_t)      atomic_uint_fast32_t;
typedef _Atomic(int_fast64_t)       atomic_int_fast64_t;
typedef _Atomic(uint_fast64_t)      atomic_uint_fast64_t;
typedef _Atomic(intptr_t)           atomic_intptr_t;
typedef _Atomic(uintptr_t)          atomic_uintptr_t;
typedef _Atomic(size_t)             atomic_size_t;
typedef _Atomic(ptrdiff_t)          atomic_ptrdiff_t;
typedef _Atomic(intmax_t)           atomic_intmax_t;
typedef _Atomic(uintmax_t)          atomic_uintmax_t;

/*
 *  7.17.7 Operations on atomic types
 */

#define atomic_store(obj, desired) \
    atomic_store_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_load(obj) \
    atomic_load_explicit((obj), memory_order_seq_cst)

#define atomic_exchange(obj, desired) \
    atomic_exchange_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_compare_exchange_strong(obj, expected, desired) \
    atomic_compare_exchange_strong_explicit((obj), (expected) \
        , (desired), memory_order_seq_cst, memory_order_seq_cst)

#define atomic_compare_exchange_weak(obj, expected, desired) \
    atomic_compare_exchange_weak_explicit((obj), (expected) \
        , (desired), memory_order_seq_cst, memory_order_seq_cst)

#define atomic_fetch_add(obj, desired) \
    atomic_fetch_add_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_fetch_sub(obj, desired) \
    atomic_fetch_sub_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_fetch_or(obj, desired) \
    atomic_fetch_or_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_fetch_xor(obj, desired) \
    atomic_fetch_xor_explicit((obj), (desired), memory_order_seq_cst)

#define atomic_fetch_and(obj, desired) \
    atomic_fetch_and_explicit((obj), (desired), memory_order_seq_cst)

#if (_MSC_VER >= 1928)

/*
 *  Visual Studio 2019 version 16.8
 *
 *  _Generic - Starting in Visual Studio 2019 version 16.8, this
 *  keyword is supported in code compiled as C when the /std:c11
 *  or /std:c17 compiler options are specified.
 */

#define atomic_store_explicit(obj, desired, order) \
    _Generic(*(obj), \
        char: atomic_store_char, \
        unsigned char: atomic_store_uchar, \
        bool: atomic_store_bool, \
        short: atomic_store_short, \
        unsigned short: atomic_store_ushort, \
        int: atomic_store_int, \
        unsigned int: atomic_store_uint, \
        long: atomic_store_long, \
        unsigned long: atomic_store_ulong, \
        __int64: atomic_store_llong, \
        unsigned __int64: atomic_store_ullong, \
        default: atomic_store_ptr) \
    ((obj), (desired), (order))

#define atomic_load_explicit(obj, order) \
    _Generic(*(obj), \
        char: atomic_load_char, \
        unsigned char: atomic_load_uchar, \
        bool: atomic_load_bool, \
        short: atomic_load_short, \
        unsigned short: atomic_load_ushort, \
        int: atomic_load_int, \
        unsigned int: atomic_load_uint, \
        long: atomic_load_long, \
        unsigned long: atomic_load_ulong, \
        __int64: atomic_load_llong, \
        unsigned __int64: atomic_load_ullong, \
        default: atomic_load_ptr) \
    ((obj), (order))

#define atomic_exchange_explicit(obj, desired, order) \
    _Generic(*(obj), \
        char: atomic_exchange_char, \
        unsigned char: atomic_exchange_uchar, \
        bool: atomic_exchange_bool, \
        short: atomic_exchange_short, \
        unsigned short: atomic_exchange_ushort, \
        int: atomic_exchange_int, \
        unsigned int: atomic_exchange_uint, \
        long: atomic_exchange_long, \
        unsigned long: atomic_exchange_ulong, \
        __int64: atomic_exchange_llong, \
        unsigned __int64: atomic_exchange_ullong, \
        default: atomic_exchange_ptr) \
    ((obj), (desired), (order))

#define atomic_compare_exchange_strong_explicit(obj, expected \
    , desired, success, failure) \
    _Generic(*(obj), \
        char: atomic_compare_exchange_char, \
        unsigned char: atomic_compare_exchange_uchar, \
        short: atomic_compare_exchange_short, \
        unsigned short: atomic_compare_exchange_ushort, \
        long: atomic_compare_exchange_long, \
        unsigned long: atomic_compare_exchange_ulong, \
        int: atomic_compare_exchange_int, \
        unsigned int: atomic_compare_exchange_uint, \
        __int64: atomic_compare_exchange_llong, \
        unsigned __int64: atomic_compare_exchange_ullong, \
        default: atomic_compare_exchange_ptr) \
    ((obj), (expected), (desired), (success), (failure))

#define atomic_compare_exchange_weak_explicit \
    atomic_compare_exchange_strong_explicit

#define atomic_fetch_add_explicit(obj, op, order) \
    _Generic(*(obj), \
        char: atomic_fetch_add_char, \
        unsigned char: atomic_fetch_add_uchar, \
        short: atomic_fetch_add_short, \
        unsigned short: atomic_fetch_add_ushort, \
        int: atomic_fetch_add_int, \
        unsigned int: atomic_fetch_add_uint, \
        long: atomic_fetch_add_long, \
        unsigned long: atomic_fetch_add_ulong, \
        __int64: atomic_fetch_add_llong, \
        unsigned __int64: atomic_fetch_add_ullong) \
    ((obj), (op), (order))

#define atomic_fetch_sub_explicit(obj, op, order) \
    _Generic(*(obj), \
        char: atomic_fetch_sub_char, \
        unsigned char: atomic_fetch_sub_uchar, \
        short: atomic_fetch_sub_short, \
        unsigned short: atomic_fetch_sub_ushort, \
        int: atomic_fetch_sub_int, \
        unsigned int: atomic_fetch_sub_uint, \
        long: atomic_fetch_sub_long, \
        unsigned long: atomic_fetch_sub_ulong, \
        __int64: atomic_fetch_sub_llong, \
        unsigned __int64: atomic_fetch_sub_ullong) \
    ((obj), (op), (order))

#define atomic_fetch_or_explicit(obj, op, order) \
    _Generic(*(obj), \
        char: atomic_fetch_or_char, \
        unsigned char: atomic_fetch_or_uchar, \
        short: atomic_fetch_or_short, \
        unsigned short: atomic_fetch_or_ushort, \
        int: atomic_fetch_or_int, \
        unsigned int: atomic_fetch_or_uint, \
        long: atomic_fetch_or_long, \
        unsigned long: atomic_fetch_or_ulong, \
        __int64: atomic_fetch_or_llong, \
        unsigned __int64: atomic_fetch_or_ullong) \
    ((obj), (op), (order))

#define atomic_fetch_xor_explicit(obj, op, order) \
    _Generic(*(obj), \
        char: atomic_fetch_xor_char, \
        unsigned char: atomic_fetch_xor_uchar, \
        short: atomic_fetch_xor_short, \
        unsigned short: atomic_fetch_xor_ushort, \
        int: atomic_fetch_xor_int, \
        unsigned int: atomic_fetch_xor_uint, \
        long: atomic_fetch_xor_long, \
        unsigned long: atomic_fetch_xor_ulong, \
        __int64: atomic_fetch_xor_llong, \
        unsigned __int64: atomic_fetch_xor_ullong) \
    ((obj), (op), (order))

#define atomic_fetch_and_explicit(obj, op, order) \
    _Generic(*(obj), \
        char: atomic_fetch_and_char, \
        unsigned char: atomic_fetch_and_uchar, \
        short: atomic_fetch_and_short, \
        unsigned short: atomic_fetch_and_ushort, \
        int: atomic_fetch_and_int, \
        unsigned int: atomic_fetch_and_uint, \
        long: atomic_fetch_and_long, \
        unsigned long: atomic_fetch_and_ulong, \
        __int64: atomic_fetch_and_llong, \
        unsigned __int64: atomic_fetch_and_ullong) \
    ((obj), (op), (order))

#else /* (_MSC_VER < 1928) */

/*
 *  C4047: type1 differs in levels of indirection from type2
 *  C4310: cast truncates constant value
 */

#define atomic_store_explicit(obj, desired, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? (atomic_store_char((atomic_char*)(obj) \
        , (char)(intptr_t)(desired), (order)), 0) \
    : ((sizeof(*(obj)) == 2U) \
    ? (atomic_store_short((atomic_short*)(obj) \
        , (short)(intptr_t)(desired), (order)), 0) \
    : ((sizeof(*(obj)) == 4U) \
    ? (atomic_store_long((atomic_long*)(obj) \
        , (long)(intptr_t)(desired), (order)), 0) \
    : ((sizeof(*(obj)) == 8U) \
    ? (atomic_store_llong((atomic_llong*)(obj) \
        , (__int64)(desired), (order)), 0) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_load_explicit(obj, order) \
    __pragma(warning(suppress: 4047)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_load_char((atomic_char*)(obj), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_load_short((atomic_short*)(obj), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_load_long((atomic_long*)(obj), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_load_llong((atomic_llong*)(obj), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_exchange_explicit(obj, desired, order) \
    __pragma(warning(suppress: 4047 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_exchange_char((atomic_char*)(obj) \
        , (char)(intptr_t)(desired), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_exchange_short((atomic_short*)(obj) \
        , (short)(intptr_t)(desired), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_exchange_long((atomic_long*)(obj) \
        , (long)(intptr_t)(desired), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_exchange_llong((atomic_llong*)(obj) \
        , (__int64)(desired), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_compare_exchange_strong_explicit(obj, expected \
    , desired, success, failure) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_compare_exchange_char( \
        (atomic_char*)(obj), (char*)(expected) \
        , (char)(intptr_t)(desired), (success), (failure)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_compare_exchange_short( \
        (atomic_short*)(obj), (short*)(expected) \
        , (short)(intptr_t)(desired), (success), (failure)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_compare_exchange_long( \
        (atomic_long*)(obj), (long*)(expected) \
        , (long)(intptr_t)(desired), (success), (failure)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_compare_exchange_llong( \
        (atomic_llong*)(obj), (__int64*)(expected) \
        , (__int64)(desired), (success), (failure)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_compare_exchange_weak_explicit \
    atomic_compare_exchange_strong_explicit

#define atomic_fetch_add_explicit(obj, op, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_fetch_add_char((atomic_char*)(obj) \
        , (char)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_fetch_add_short((atomic_short*)(obj) \
        , (short)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_fetch_add_long((atomic_long*)(obj) \
        , (long)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_fetch_add_llong((atomic_llong*)(obj) \
        , (__int64)(intptr_t)(op), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_fetch_sub_explicit(obj, op, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_fetch_sub_char((atomic_char*)(obj) \
        , (char)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_fetch_sub_short((atomic_short*)(obj) \
        , (short)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_fetch_sub_long((atomic_long*)(obj) \
        , (long)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_fetch_sub_llong((atomic_llong*)(obj) \
        , (__int64)(intptr_t)(op), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_fetch_or_explicit(obj, op, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_fetch_or_char((atomic_char*)(obj) \
        , (char)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_fetch_or_short((atomic_short*)(obj) \
        , (short)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_fetch_or_long((atomic_long*)(obj) \
        , (long)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_fetch_or_llong((atomic_llong*)(obj) \
        , (__int64)(intptr_t)(op), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_fetch_xor_explicit(obj, op, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_fetch_xor_char((atomic_char*)(obj) \
        , (char)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_fetch_xor_short((atomic_short*)(obj) \
        , (short)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_fetch_xor_long((atomic_long*)(obj) \
        , (long)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_fetch_xor_llong((atomic_llong*)(obj) \
        , (__int64)(intptr_t)(op), (order)) \
    : (assert(!"Invalid type"), 0)))))

#define atomic_fetch_and_explicit(obj, op, order) \
    __pragma(warning(suppress: 4310)) \
    ((sizeof(*(obj)) == 1U) \
    ? atomic_fetch_and_char((atomic_char*)(obj) \
        , (char)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 2U) \
    ? atomic_fetch_and_short((atomic_short*)(obj) \
        , (short)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 4U) \
    ? atomic_fetch_and_long((atomic_long*)(obj) \
        , (long)(intptr_t)(op), (order)) \
    : ((sizeof(*(obj)) == 8U) \
    ? atomic_fetch_and_llong((atomic_llong*)(obj) \
        , (__int64)(intptr_t)(op), (order)) \
    : (assert(!"Invalid type"), 0)))))

#endif /* (_MSC_VER >= 1928) */

/*
 *  7.17.8 Atomic flag type and operations
 */

typedef atomic_bool atomic_flag;

#define	ATOMIC_FLAG_INIT false

#define atomic_flag_test_and_set(obj) \
    atomic_flag_test_and_set_explicit((obj), memory_order_seq_cst)

#define atomic_flag_clear(obj) \
    atomic_flag_clear_explicit((obj), memory_order_seq_cst)

#define atomic_flag_test_and_set_explicit(obj, order) \
    atomic_exchange_bool((obj), true, (order))

#define atomic_flag_clear_explicit(obj, order) \
    atomic_store_bool((obj), false, (order))

/*
 *  Microsoft Visual C++ (MSVC) specific operations
 *
 *  Based on code by Helge Bahmann, Tim Blechmann and Andrey Semashev.
 *  See /boost/atomic/detail/ops_msvc_x86.hpp.
 *
 *  Copyright (c) 2009 Helge Bahmann
 *  Copyright (c) 2012 Tim Blechmann
 *  Copyright (c) 2014 Andrey Semashev
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (http://www.boost.org/LICENSE_1_0.txt)
 */

/*
 *  atomic_store_explicit
 */

static __forceinline void
atomic_store_char(volatile atomic_char* obj, char desired
    , memory_order order)
{
    if (order == memory_order_seq_cst)
    {
        _InterlockedExchange8(obj, desired);
    }
    else
    {
        _ReadWriteBarrier();
        *obj = desired;
        _ReadWriteBarrier();
    }
}

static __forceinline void
atomic_store_uchar(volatile atomic_uchar* obj, unsigned char desired
    , memory_order order)
{
    atomic_store_char((volatile atomic_char*)obj, (char)desired, order);
}

static __forceinline void
atomic_store_bool(volatile atomic_bool* obj, bool desired
    , memory_order order)
{
    atomic_store_char((volatile atomic_char*)obj, !!desired, order);
}

static __forceinline void
atomic_store_short(volatile atomic_short* obj, short desired
    , memory_order order)
{
    if (order == memory_order_seq_cst)
    {
        _InterlockedExchange16(obj, desired);
    }
    else
    {
        _ReadWriteBarrier();
        *obj = desired;
        _ReadWriteBarrier();
    }
}

static __forceinline void
atomic_store_ushort(volatile atomic_ushort* obj, unsigned short desired
    , memory_order order)
{
    atomic_store_short((volatile atomic_short*)obj, (short)desired, order);
}

static __forceinline void
atomic_store_long(volatile atomic_long* obj, long desired
    , memory_order order)
{
    if (order == memory_order_seq_cst)
    {
        _InterlockedExchange(obj, desired);
    }
    else
    {
        _ReadWriteBarrier();
        *obj = desired;
        _ReadWriteBarrier();
    }
}

static __forceinline void
atomic_store_ulong(volatile atomic_ulong* obj, unsigned long desired
    , memory_order order)
{
    atomic_store_long((volatile atomic_long*)obj, (long)desired, order);
}

static __forceinline void
atomic_store_int(volatile atomic_int* obj, int desired, memory_order order)
{
    atomic_store_long((volatile atomic_long*)obj, (long)desired, order);
}

static __forceinline void
atomic_store_uint(volatile atomic_uint* obj, unsigned int desired
    , memory_order order)
{
    atomic_store_long((volatile atomic_long*)obj, (long)desired, order);
}

static __forceinline void
atomic_store_llong(volatile atomic_llong* obj, __int64 desired
    , memory_order order)
{
#if defined(_M_IX86)
    (void)order;
    _ReadWriteBarrier();
    volatile __int64* p = obj;
    if (((unsigned int)p & 0x00000007) == 0)
    {
#if defined(_M_IX86_FP) && (_M_IX86_FP >= 2)
#if defined(__AVX__)
        __asm
        {
            mov edx, p
            vmovq xmm4, desired
            vmovq qword ptr[edx], xmm4
        };
#else
        __asm
        {
            mov edx, p
            movq xmm4, desired
            movq qword ptr[edx], xmm4
        };
#endif /* defined(__AVX__) */
#else
        __asm
        {
            mov edx, p
            fild desired
            fistp qword ptr[edx]
        };
#endif /* defined(_M_IX86_FP) ... */
    }
    else
    {
        unsigned int backup = 0;
        __asm
        {
            mov backup, ebx
            mov edi, p
            mov ebx, dword ptr [desired]
            mov ecx, dword ptr [desired + 4]
            mov eax, dword ptr [edi]
            mov edx, dword ptr [edi + 4]
            align 16
        again:
            lock cmpxchg8b qword ptr [edi]
            jne again
            mov ebx, backup
        };
    }
    _ReadWriteBarrier();
#elif defined(_M_X64)
    if (order == memory_order_seq_cst)
    {
        _InterlockedExchange64(obj, desired);
    }
    else
    {
        _ReadWriteBarrier();
        *obj = desired;
        _ReadWriteBarrier();
    }
#endif /* defined(_M_IX86) */
}

static __forceinline void
atomic_store_ullong(volatile atomic_ullong* obj, unsigned __int64 desired
    , memory_order order)
{
    atomic_store_llong((volatile atomic_llong*)obj, (__int64)desired
        , order);
}

static __forceinline void
atomic_store_ptr(volatile void* obj, void* desired, memory_order order)
{
#if defined(_M_IX86)
    atomic_store_long((volatile atomic_long*)obj, (long)desired, order);
#elif defined(_M_X64)
    atomic_store_llong((volatile atomic_llong*)obj, (__int64)desired
        , order);
#endif /* defined(_M_IX86) */
}

/*
 *  atomic_load_explicit
 */

static __forceinline char
atomic_load_char(const volatile atomic_char* obj, memory_order order)
{
    (void)order;
    char value = *obj;
    _ReadWriteBarrier();
    return value;
}

static __forceinline unsigned char
atomic_load_uchar(const volatile atomic_uchar* obj, memory_order order)
{
    return atomic_load_char((const volatile atomic_char*)obj, order);
}

static __forceinline bool
atomic_load_bool(const volatile atomic_bool* obj, memory_order order)
{
    return !!atomic_load_char((const volatile atomic_char*)obj, order);
}

static __forceinline short
atomic_load_short(const volatile atomic_short* obj, memory_order order)
{
    (void)order;
    short value = *obj;
    _ReadWriteBarrier();
    return value;
}

static __forceinline unsigned short
atomic_load_ushort(const volatile atomic_ushort* obj, memory_order order)
{
    return atomic_load_short((const volatile atomic_short*)obj
        , order);
}

static __forceinline long
atomic_load_long(const volatile atomic_long* obj, memory_order order)
{
    (void)order;
    int value = *obj;
    _ReadWriteBarrier();
    return value;
}

static __forceinline unsigned long
atomic_load_ulong(const volatile atomic_ulong* obj, memory_order order)
{
    return atomic_load_long((const volatile atomic_long*)obj, order);
}

static __forceinline int
atomic_load_int(const volatile atomic_int* obj, memory_order order)
{
    return atomic_load_long((const volatile atomic_long*)obj, order);
}

static __forceinline unsigned int
atomic_load_uint(const volatile atomic_uint* obj, memory_order order)
{
    return atomic_load_long((const volatile atomic_long*)obj, order);
}

static __forceinline __int64
atomic_load_llong(const volatile atomic_llong* obj, memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    _ReadWriteBarrier();
    const volatile __int64* p = obj;
    __int64 value = 0;
    if (((unsigned int)p & 0x00000007) == 0)
    {
#if defined(_M_IX86_FP) && (_M_IX86_FP >= 2)
#if defined(__AVX__)
        __asm
        {
            mov edx, p
            vmovq xmm4, qword ptr[edx]
            vmovq value, xmm4
        };
#else
        __asm
        {
            mov edx, p
            movq xmm4, qword ptr[edx]
            movq value, xmm4
        };
#endif /* defined(__AVX__) */
#else
        __asm
        {
            mov edx, p
            fild qword ptr[edx]
            fistp value
        };
#endif /* defined(_M_IX86_FP) ... */
    }
    else
    {
        __asm
        {
            mov edi, p
            mov eax, ebx
            mov edx, ecx
            lock cmpxchg8b qword ptr[edi]
            mov dword ptr[value], eax
            mov dword ptr[value + 4], edx
        };
    }
    _ReadWriteBarrier();
#elif defined(_M_X64)
    __int64 value = *obj;
    _ReadWriteBarrier();
#endif /* defined(_M_IX86) */
    return value;
}

static __forceinline unsigned __int64
atomic_load_ullong(const volatile atomic_ullong* obj, memory_order order)
{
    return atomic_load_llong((const volatile atomic_llong*)obj, order);
}

static __forceinline void*
atomic_load_ptr(const volatile void* obj, memory_order order)
{
#if defined(_M_IX86)
    return (void*)atomic_load_long((const volatile atomic_long*)obj
        , order);
#elif defined(_M_X64)
    return (void*)atomic_load_llong((const volatile atomic_llong*)obj
        , order);
#endif /* defined(_M_IX86) */
}

/*
 *  atomic_exchange_explicit
 */

static __forceinline char
atomic_exchange_char(volatile atomic_char* obj, char desired
    , memory_order order)
{
    (void)order;
    return _InterlockedExchange8(obj, desired);
}

static __forceinline unsigned char
atomic_exchange_uchar(volatile atomic_uchar* obj, unsigned char desired
    , memory_order order)
{
    return atomic_exchange_char((volatile atomic_char*)obj, (char)desired
        , order);
}

static __forceinline bool
atomic_exchange_bool(volatile atomic_uchar* obj, bool desired
    , memory_order order)
{
    return (bool)atomic_exchange_char((volatile atomic_char*)obj
        , (char)desired, order);
}

static __forceinline short
atomic_exchange_short(volatile atomic_short* obj, short desired
    , memory_order order)
{
    (void)order;
    return _InterlockedExchange16(obj, desired);
}

static __forceinline unsigned short
atomic_exchange_ushort(volatile atomic_ushort* obj, unsigned short desired
    , memory_order order)
{
    return atomic_exchange_short((volatile atomic_short*)obj
        , (short)desired, order);
}

static __forceinline long
atomic_exchange_long(volatile atomic_long* obj, long desired
    , memory_order order)
{
    (void)order;
    return _InterlockedExchange(obj, desired);
}

static __forceinline unsigned long
atomic_exchange_ulong(volatile atomic_ulong* obj, unsigned long desired
    , memory_order order)
{
    return atomic_exchange_long((volatile atomic_long*)obj
        , (long)desired, order);
}

static __forceinline int
atomic_exchange_int(volatile atomic_int* obj, int desired
    , memory_order order)
{
    return atomic_exchange_long((volatile atomic_long*)obj
        , (long)desired, order);
}

static __forceinline unsigned int
atomic_exchange_uint(volatile atomic_uint* obj, unsigned int desired
    , memory_order order)
{
    return atomic_exchange_long((volatile atomic_long*)obj
        , (long)desired, order);
}

static __forceinline __int64
atomic_exchange_llong(volatile atomic_llong* obj, __int64 desired
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    _ReadWriteBarrier();
    volatile __int64* p = obj;
    unsigned int backup = 0;
    __asm
    {
        mov backup, ebx
        mov edi, p
        mov ebx, dword ptr[desired]
        mov ecx, dword ptr[desired + 4]
        mov eax, dword ptr[edi]
        mov edx, dword ptr[edi + 4]
        align 16
    again:
        lock cmpxchg8b qword ptr[edi]
        jne again
        mov ebx, backup
        mov dword ptr[desired], eax
        mov dword ptr[desired + 4], edx
    };
    _ReadWriteBarrier();
    return desired;
#elif defined(_M_X64)
    return _InterlockedExchange64(obj, desired);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_exchange_ullong(volatile atomic_ullong* obj
    , unsigned __int64 desired, memory_order order)
{
    return atomic_exchange_llong((volatile atomic_llong*)obj
        , (__int64)desired, order);
}

static __forceinline void*
atomic_exchange_ptr(volatile void* obj, void* desired, memory_order order)
{
#if defined(_M_IX86)
    return (void*)atomic_exchange_long((volatile atomic_long*)obj
        , (long)desired, order);
#elif defined(_M_X64)
    return (void*)atomic_exchange_llong((volatile atomic_llong*)obj
        , (__int64)desired, order);
#endif /* defined(_M_IX86) */
}

/*
 *  atomic_compare_exchange_strong_explicit
 */

static __forceinline bool
atomic_compare_exchange_char(volatile atomic_char* obj, char* expected
    , char desired, memory_order success, memory_order failure)
{
    (void)success;
    (void)failure;
    char old = *expected;
    *expected = _InterlockedCompareExchange8(obj, desired, old);
    return (old == *expected);
}

static __forceinline bool
atomic_compare_exchange_uchar(volatile atomic_uchar* obj
    , unsigned char* expected, unsigned char desired, memory_order success
    , memory_order failure)
{
    return atomic_compare_exchange_char((volatile atomic_char*)obj
        , (char*)expected, (char)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_short(volatile atomic_short* obj, short* expected
    , short desired, memory_order success, memory_order failure)
{
    (void)success;
    (void)failure;
    short old = *expected;
    *expected = _InterlockedCompareExchange16(obj, desired, old);
    return (old == *expected);
}

static __forceinline bool
atomic_compare_exchange_ushort(volatile atomic_ushort* obj
    , unsigned short* expected, unsigned short desired
    , memory_order success, memory_order failure)
{
    return atomic_compare_exchange_short((volatile atomic_short*)obj
        , (short*)expected, (short)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_long(volatile atomic_long* obj, long* expected
    , long desired, memory_order success, memory_order failure)
{
    (void)success;
    (void)failure;
    long old = *expected;
    *expected = _InterlockedCompareExchange(obj, desired, old);
    return (old == *expected);
}

static __forceinline bool
atomic_compare_exchange_ulong(volatile atomic_ulong* obj
    , unsigned long* expected, unsigned long desired, memory_order success
    , memory_order failure)
{
    return atomic_compare_exchange_long((volatile atomic_long*)obj
        , (long*)expected, (long)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_int(volatile atomic_int* obj, int* expected
    , int desired, memory_order success, memory_order failure)
{
    return atomic_compare_exchange_long((volatile atomic_long*)obj
        , (long*)expected, (long)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_uint(volatile atomic_uint* obj
    , unsigned int* expected, unsigned int desired, memory_order success
    , memory_order failure)
{
    return atomic_compare_exchange_long((volatile atomic_long*)obj
        , (long*)expected, (long)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_llong(volatile atomic_llong* obj
    , __int64* expected, __int64 desired, memory_order success
    , memory_order failure)
{
    (void)success;
    (void)failure;
    __int64 old = *expected;
    *expected = _InterlockedCompareExchange64(obj, desired, old);
    return (old == *expected);
}

static __forceinline bool
atomic_compare_exchange_ullong(volatile atomic_ullong* obj
    , unsigned __int64* expected, unsigned __int64 desired
    , memory_order success, memory_order failure)
{
    return atomic_compare_exchange_llong((volatile atomic_llong*)obj
        , (__int64*)expected, (__int64)desired, success, failure);
}

static __forceinline bool
atomic_compare_exchange_ptr(volatile void* obj, void* expected
    , void* desired, memory_order success, memory_order failure)
{
#if defined(_M_IX86)
    return atomic_compare_exchange_long((volatile atomic_long*)obj
        , (long*)expected, (long)desired, success, failure);
#elif defined(_M_X64)
    return atomic_compare_exchange_llong((volatile atomic_llong*)obj
        , (__int64*)expected, (__int64)desired, success, failure);
#endif /* defined(_M_IX86) */
}

/*
 *  atomic_fetch_add_explicit
 */

static __forceinline char
atomic_fetch_add_char(volatile atomic_char* obj, char op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd8(obj, op);
}

static __forceinline unsigned char
atomic_fetch_add_uchar(volatile atomic_uchar* obj, unsigned char op
, memory_order order)
{
    return atomic_fetch_add_char((volatile atomic_char*)obj, (char)op
        , order);
}

static __forceinline short
atomic_fetch_add_short(volatile atomic_short* obj, short op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd16(obj, op);
}

static __forceinline unsigned short
atomic_fetch_add_ushort(volatile atomic_ushort* obj, unsigned short op
    , memory_order order)
{
    return atomic_fetch_add_short((volatile atomic_short*)obj, (short)op
        , order);
}

static __forceinline long
atomic_fetch_add_long(volatile atomic_long* obj, long op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd(obj, op);
}

static __forceinline unsigned long
atomic_fetch_add_ulong(volatile atomic_ulong* obj, unsigned long op
    , memory_order order)
{
    return atomic_fetch_add_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline int
atomic_fetch_add_int(volatile atomic_int* obj, int op, memory_order order)
{
    return atomic_fetch_add_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline unsigned int
atomic_fetch_add_uint(volatile atomic_uint* obj, unsigned int op
    , memory_order order)
{
    return atomic_fetch_add_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline __int64
atomic_fetch_add_llong(volatile atomic_llong* obj, __int64 op
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    __int64 old = *obj;
    while (!atomic_compare_exchange_weak(obj, &old, old + op)) {}
    return old;
#elif defined(_M_X64)
    return _InterlockedExchangeAdd64(obj, op);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_fetch_add_ullong(volatile atomic_ullong* obj, unsigned __int64 op
    , memory_order order)
{
    return atomic_fetch_add_llong((volatile atomic_llong*)obj, (__int64)op
        , order);
}

/*
 *  atomic_fetch_sub_explicit
 */

static __forceinline char
atomic_fetch_sub_char(volatile atomic_char* obj, char op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd8(obj, -op);
}

static __forceinline unsigned char
atomic_fetch_sub_uchar(volatile atomic_uchar* obj, unsigned char op
    , memory_order order)
{
    return atomic_fetch_sub_char((volatile atomic_char*)obj, (char)op
        , order);
}

static __forceinline short
atomic_fetch_sub_short(volatile atomic_short* obj, short op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd16(obj, -op);
}

static __forceinline unsigned short
atomic_fetch_sub_ushort(volatile atomic_ushort* obj, unsigned short op
    , memory_order order)
{
    return atomic_fetch_sub_short((volatile atomic_short*)obj, (short)op
        , order);
}

static __forceinline long
atomic_fetch_sub_long(volatile atomic_long* obj, long op
    , memory_order order)
{
    (void)order;
    return _InterlockedExchangeAdd(obj, -op);
}

static __forceinline unsigned long
atomic_fetch_sub_ulong(volatile atomic_ulong* obj, unsigned long op
    , memory_order order)
{
    return atomic_fetch_sub_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline int
atomic_fetch_sub_int(volatile atomic_int* obj, int op, memory_order order)
{
    return atomic_fetch_sub_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline unsigned int
atomic_fetch_sub_uint(volatile atomic_uint* obj, unsigned int op
    , memory_order order)
{
    return atomic_fetch_sub_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline __int64
atomic_fetch_sub_llong(volatile atomic_llong* obj, __int64 op
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    __int64 old = *obj;
    while (!atomic_compare_exchange_weak(obj, &old, old - op)) {}
    return old;
#elif defined(_M_X64)
    return _InterlockedExchangeAdd64(obj, -op);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_fetch_sub_ullong(volatile atomic_ullong* obj, unsigned __int64 op
    , memory_order order)
{
    return atomic_fetch_sub_llong((volatile atomic_llong*)obj, (__int64)op
        , order);
}

/*
 *  atomic_fetch_or_explicit
 */

static __forceinline char
atomic_fetch_or_char(volatile atomic_char* obj, char op
    , memory_order order)
{
    (void)order;
    return _InterlockedOr8(obj, op);
}

static __forceinline unsigned char
atomic_fetch_or_uchar(volatile atomic_uchar* obj, unsigned char op
    , memory_order order)
{
    return atomic_fetch_or_char((volatile atomic_char*)obj, (char)op
        , order);
}

static __forceinline short
atomic_fetch_or_short(volatile atomic_short* obj, short op
    , memory_order order)
{
    (void)order;
    return _InterlockedOr16(obj, op);
}

static __forceinline unsigned short
atomic_fetch_or_ushort(volatile atomic_ushort* obj, unsigned short op
    , memory_order order)
{
    return atomic_fetch_or_short((volatile atomic_short*)obj, (short)op
        , order);
}

static __forceinline long
atomic_fetch_or_long(volatile atomic_long* obj, long op
    , memory_order order)
{
    (void)order;
    return _InterlockedOr(obj, op);
}

static __forceinline unsigned long
atomic_fetch_or_ulong(volatile atomic_ulong* obj, unsigned long op
    , memory_order order)
{
    return atomic_fetch_or_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline int
atomic_fetch_or_int(volatile atomic_int* obj, int op, memory_order order)
{
    return atomic_fetch_or_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline unsigned int
atomic_fetch_or_uint(volatile atomic_uint* obj, unsigned int op
    , memory_order order)
{
    return atomic_fetch_or_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline __int64
atomic_fetch_or_llong(volatile atomic_llong* obj, __int64 op
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    __int64 old = *obj;
    while (!atomic_compare_exchange_weak(obj, &old, old | op)) {}
    return old;
#elif defined(_M_X64)
    return _InterlockedOr64(obj, op);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_fetch_or_ullong(volatile atomic_ullong* obj, unsigned __int64 op
    , memory_order order)
{
    return atomic_fetch_or_llong((volatile atomic_llong*)obj, (__int64)op
        , order);
}

/*
 *  atomic_fetch_xor_explicit
 */

static __forceinline char
atomic_fetch_xor_char(volatile atomic_char* obj, char op
    , memory_order order)
{
    (void)order;
    return _InterlockedXor8(obj, op);
}

static __forceinline unsigned char
atomic_fetch_xor_uchar(volatile atomic_uchar* obj, unsigned char op
    , memory_order order)
{
    return atomic_fetch_xor_char((volatile atomic_char*)obj, (char)op
        , order);
}

static __forceinline short
atomic_fetch_xor_short(volatile atomic_short* obj, short op
    , memory_order order)
{
    (void)order;
    return _InterlockedXor16(obj, op);
}

static __forceinline unsigned short
atomic_fetch_xor_ushort(volatile atomic_ushort* obj, unsigned short op
    , memory_order order)
{
    return atomic_fetch_xor_short((volatile atomic_short*)obj, (short)op
        , order);
}

static __forceinline long
atomic_fetch_xor_long(volatile atomic_long* obj, long op
    , memory_order order)
{
    (void)order;
    return _InterlockedXor(obj, op);
}

static __forceinline unsigned long
atomic_fetch_xor_ulong(volatile atomic_ulong* obj, unsigned long op
    , memory_order order)
{
    return atomic_fetch_xor_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline int
atomic_fetch_xor_int(volatile atomic_int* obj, int op, memory_order order)
{
    return atomic_fetch_xor_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline unsigned int
atomic_fetch_xor_uint(volatile atomic_uint* obj, unsigned int op
    , memory_order order)
{
    return atomic_fetch_xor_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline __int64
atomic_fetch_xor_llong(volatile atomic_llong* obj, __int64 op
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    __int64 old = *obj;
    while (!atomic_compare_exchange_weak(obj, &old, old ^ op)) {}
    return old;
#elif defined(_M_X64)
    return _InterlockedXor64(obj, op);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_fetch_xor_ullong(volatile atomic_ullong* obj, unsigned __int64 op
    , memory_order order)
{
    return atomic_fetch_xor_llong((volatile atomic_llong*)obj, (__int64)op
        , order);
}

/*
 *  atomic_fetch_and_explicit
 */

static __forceinline char
atomic_fetch_and_char(volatile atomic_char* obj, char op
    , memory_order order)
{
    (void)order;
    return _InterlockedAnd8(obj, op);
}

static __forceinline unsigned char
atomic_fetch_and_uchar(volatile atomic_uchar* obj, unsigned char op
    , memory_order order)
{
    return atomic_fetch_and_char((volatile atomic_char*)obj, (char)op
        , order);
}

static __forceinline short
atomic_fetch_and_short(volatile atomic_short* obj, short op
    , memory_order order)
{
    (void)order;
    return _InterlockedAnd16(obj, op);
}

static __forceinline unsigned short
atomic_fetch_and_ushort(volatile atomic_ushort* obj, unsigned short op
    , memory_order order)
{
    return atomic_fetch_and_short((volatile atomic_short*)obj, (short)op
        , order);
}

static __forceinline long
atomic_fetch_and_long(volatile atomic_long* obj, long op
    , memory_order order)
{
    (void)order;
    return _InterlockedAnd(obj, op);
}

static __forceinline unsigned long
atomic_fetch_and_ulong(volatile atomic_ulong* obj, unsigned long op
    , memory_order order)
{
    return atomic_fetch_and_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline int
atomic_fetch_and_int(volatile atomic_int* obj, int op, memory_order order)
{
    return atomic_fetch_and_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline unsigned int
atomic_fetch_and_uint(volatile atomic_uint* obj, unsigned int op
    , memory_order order)
{
    return atomic_fetch_and_long((volatile atomic_long*)obj, (long)op
        , order);
}

static __forceinline __int64
atomic_fetch_and_llong(volatile atomic_llong* obj, __int64 op
    , memory_order order)
{
    (void)order;
#if defined(_M_IX86)
    __int64 old = *obj;
    while (!atomic_compare_exchange_weak(obj, &old, old & op)) {}
    return old;
#elif defined(_M_X64)
    return _InterlockedAnd64(obj, op);
#endif /* defined(_M_IX86) */
}

static __forceinline unsigned __int64
atomic_fetch_and_ullong(volatile atomic_ullong* obj, unsigned __int64 op
    , memory_order order)
{
    return atomic_fetch_and_llong((volatile atomic_llong*)obj, (__int64)op
        , order);
}

#endif /* defined(USE_TEMPORARY_MSVC_WORKAROUND) */

#undef USE_TEMPORARY_MSVC_WORKAROUND

#endif /* __STDATOMIC_H__ */