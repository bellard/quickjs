//-< STDTP.H >-------------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-98    K.A. Knizhnik  * / [] \ *
//                          Last update: 10-Dec-98    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// Standart type and macro definitions
//-------------------------------------------------------------------*--------*

#ifndef __STDTP_H__
#define __STDTP_H__

#if defined(_WIN32)
#include <windows.h>
#ifdef _MSC_VER
#pragma warning(disable : 4800 4355 4146 4251)
#pragma warning(disable : 4458 4456 4457 4459) // warning C4458 : declaration of
                                               // 'name' hides class member
#endif
#else
#ifdef _AIX
#define INT8_IS_DEFINED
#endif
#ifndef NO_PTHREADS
#ifndef _REENTRANT
#define _REENTRANT
#endif
#endif
#endif

#if defined(__VACPP_MULTI__) // IBM compiler produce a lot of stupid warnings
#pragma report(disable, "CPPC1608")
#pragma report(disable, "CPPC1281")
#endif /* __VACPP_MULTI__ */

#ifdef _WINCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include <tchar.h>
#include "wince.h"

#else

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <stdarg.h>
#include <time.h>

#endif

#if !defined(_WIN32)
#define __cdecl
#endif

#define DEBUG_NONE 0
#define DEBUG_CHECK 1
#define DEBUG_TRACE 2

#if GIGABASE_DEBUG == DEBUG_TRACE
#define TRACE_MSG(x) dbTrace x
#else
#define TRACE_MSG(x)
#endif

extern void dbTrace(char *message, ...);

// Align value 'x' to boundary 'b' which should be power of 2
#define DOALIGN(x, b) (((x) + (b)-1) & ~((b)-1))

typedef signed char   db_int1;
typedef unsigned char db_nat1;

typedef signed short   db_int2;
typedef unsigned short db_nat2;

typedef signed int   db_int4;
typedef unsigned int db_nat4;

typedef float  db_real4;
typedef double db_real8;

typedef unsigned char byte;

#if defined(_WIN32) && !defined(__MINGW32__)
typedef unsigned __int64 db_nat8;
typedef __int64          db_int8;
#if defined(__IBMCPP__)
#define INT8_FORMAT "%lld"
#define T_INT8_FORMAT _T("%lld")
#else
#define INT8_FORMAT "%I64d"
#define T_INT8_FORMAT _T("%I64d")
#endif
#define CONST64(c) c
#else
#if SIZEOF_LONG == 8
typedef unsigned long db_nat8;
typedef signed long   db_int8;
#define INT8_FORMAT "%ld"
#define T_INT8_FORMAT _T("%ld")
#define CONST64(c) c##L
#else
typedef unsigned long long db_nat8;
typedef signed long long   db_int8;
#define INT8_FORMAT "%lld"
#define T_INT8_FORMAT _T("%lld")
#define CONST64(c) c##LL
#endif
#endif

#if !defined(bool) && (defined(__SUNPRO_CC) || defined(__IBMCPP__))
#define bool char
#define true(1)
#define false(0)
#endif

#define nat8_low_part(x) ((db_nat4)(x))
#define int8_low_part(x) ((db_int4)(x))
#if defined(_MSC_VER) // bug in MVC 6.0
#define nat8_high_part(x) (sizeof(x) < 8 ? 0 : ((db_nat4)((db_nat8)(x) >> 32)))
#define int8_high_part(x) (sizeof(x) < 8 ? 0 : ((db_int4)((db_int8)(x) >> 32)))
#else
#define nat8_high_part(x) ((db_nat4)((db_nat8)(x) >> 32))
#define int8_high_part(x) ((db_int4)((db_int8)(x) >> 32))
#endif

#define cons_nat8(hi, lo) ((((db_nat8)(hi)) << 32) | (db_nat4)(lo))
#define cons_int8(hi, lo) ((((db_int8)(hi)) << 32) | (db_nat4)(lo))

#define MAX_NAT8 db_nat8(-1)

#define itemsof(array) (sizeof(array) / sizeof *(array))

#endif
