/*
 * MSVC compatibility shims for QuickJS
 *
 * This header centralizes the POSIX declarations/functions that the
 * MSVC C runtime does not provide (unlike MinGW-w64, which already
 * ships POSIX-ish headers such as <unistd.h>, <dirent.h>, <sys/time.h>,
 * ...). It is only ever included when _MSC_VER is defined, so it has
 * zero effect on GCC/Clang/MinGW/Linux/macOS builds.
 *
 * None of this is meant to be a complete POSIX emulation layer - it
 * only covers what QuickJS itself needs to compile and run reasonably
 * under MSVC.
 */
#ifndef QUICKJS_MSVC_COMPAT_H
#define QUICKJS_MSVC_COMPAT_H

#ifdef _MSC_VER

#define WIN32_LEAN_AND_MEAN
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifndef _CRT_NONSTDC_NO_DEPRECATE
#define _CRT_NONSTDC_NO_DEPRECATE
#endif

#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- struct timeval / gettimeofday ------------------------------- */
/* <winsock2.h> is included above (before <windows.h>, which is the
   only reliable way to stop <windows.h> from pulling in the legacy
   <winsock.h> and causing duplicate-definition errors) and already
   declares struct timeval itself, unconditionally - so just use that
   instead of trying to (re-)declare it here ourselves. gettimeofday()
   itself is a BSD/POSIX function that WinSock does not provide. */

static __inline int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        FILETIME ft;
        ULARGE_INTEGER t;
        /* 100ns intervals since 1601-01-01, converted to usecs since
           1970-01-01 (11644473600 seconds between the two epochs). */
        GetSystemTimeAsFileTime(&ft);
        t.LowPart = ft.dwLowDateTime;
        t.HighPart = ft.dwHighDateTime;
        {
            unsigned long long usec = t.QuadPart / 10ULL - 11644473600000000ULL;
            tv->tv_sec = (long)(usec / 1000000ULL);
            tv->tv_usec = (long)(usec % 1000000ULL);
        }
    }
    return 0;
}

/* ---- misc string/posix name shims --------------------------------- */
#define strcasecmp  _stricmp
#define strncasecmp _strnicmp

/* ssize_t: guarded by the same _SSIZE_T_DEFINED macro cutils.h uses, so
   whichever of cutils.h / msvc_compat.h is included first "wins" and
   the second is a no-op instead of causing a duplicate typedef error. */
#if !defined(_SSIZE_T_DEFINED)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

/* ---- file / io shims ----------------------------------------------- */
#define fileno   _fileno
#ifndef isatty
#define isatty   _isatty
#endif
#define dup      _dup
#define dup2     _dup2
#define unlink   _unlink
#define getcwd   _getcwd
#define chdir    _chdir
/* quickjs-libc.c calls mkdir(path) with a single argument on _WIN32
   (matching the traditional MinGW <io.h> mkdir()), so alias it directly
   to _mkdir rather than using a fixed-arity function-like macro. */
#define mkdir    _mkdir
#define popen    _popen
#define pclose   _pclose

#ifndef S_ISREG
#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#endif

/* MSVC's <sys/stat.h> only defines S_IFMT/S_IFDIR/S_IFCHR/S_IFREG (and
   S_IFIFO as of some SDK versions, inconsistently) - it never defines
   S_IFBLK, S_IFSOCK, or S_IFLNK, since Windows has no block-device,
   socket, or symlink bits in st_mode the way POSIX does. quickjs-libc.c
   exposes these as os.S_IF* constants regardless of whether a given
   platform can ever actually report them in practice, so provide the
   standard POSIX values here for source compatibility. */
#ifndef S_IFIFO
#define S_IFIFO  0010000
#endif
#ifndef S_IFBLK
#define S_IFBLK  0060000
#endif

/* ---- read()/write()/open()/close()/lseek() -------------------------
   MSVC's CRT only exposes the underscore-prefixed POSIX-ish names by
   default. Alias the plain names quickjs-libc.c uses to them. */
#ifndef open
#define open     _open
#endif
#ifndef close
#define close    _close
#endif
#ifndef read
#define read     _read
#endif
#ifndef write
#define write    _write
#endif
#ifndef lseek
#define lseek    _lseeki64
#endif

/* ---- minimal dirent.h (opendir/readdir/closedir) shim --------------- */
struct dirent {
    char d_name[MAX_PATH];
};

typedef struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAA find_data;
    int first;
    struct dirent ent;
} DIR;

static __inline DIR *opendir(const char *name)
{
    DIR *d;
    char pattern[MAX_PATH];

    d = (DIR *)calloc(1, sizeof(DIR));
    if (!d) {
        errno = ENOMEM;
        return NULL;
    }
    snprintf(pattern, sizeof(pattern), "%s\\*", name);
    pattern[sizeof(pattern) - 1] = '\0';
    d->handle = FindFirstFileA(pattern, &d->find_data);
    if (d->handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        free(d);
        errno = (err == ERROR_FILE_NOT_FOUND) ? ENOENT : EIO;
        return NULL;
    }
    d->first = 1;
    return d;
}

static __inline struct dirent *readdir(DIR *d)
{
    if (!d)
        return NULL;
    if (d->first) {
        d->first = 0;
    } else {
        if (!FindNextFileA(d->handle, &d->find_data))
            return NULL;
    }
    strncpy(d->ent.d_name, d->find_data.cFileName, sizeof(d->ent.d_name) - 1);
    d->ent.d_name[sizeof(d->ent.d_name) - 1] = '\0';
    return &d->ent;
}

static __inline int closedir(DIR *d)
{
    if (!d)
        return -1;
    if (d->handle != INVALID_HANDLE_VALUE)
        FindClose(d->handle);
    free(d);
    return 0;
}

/* ---- sleep ----------------------------------------------------------- */
static __inline unsigned int js_msvc_sleep_sec(unsigned int seconds)
{
    Sleep(seconds * 1000);
    return 0;
}
#define sleep(s)   js_msvc_sleep_sec(s)

static __inline int usleep(unsigned int usec)
{
    Sleep((usec + 999) / 1000);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* _MSC_VER */

#endif /* QUICKJS_MSVC_COMPAT_H */
