#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>

#if defined(__APPLE__)
#include <sys/syslimits.h>
#include <malloc/malloc.h>
#include <sys/time.h>
#include <ftw.h>
#include <dirent.h>
#include <unistd.h>
#elif defined(__linux__)
#include <malloc.h>
#include <sys/time.h>
#include <dirent.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

#include "cutils.h"
#include "quickjs-port.h"

void qjs_abort() {
    abort();
}

 // From: https://stackoverflow.com/a/26085827
int qjs_gettimeofday(struct qjs_timeval * tp)
{
#if defined (_WIN32)
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTimeAsFileTime(&file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec = (int64_t)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)((time / 10) % 1000000L);
    return 0;
#else
    struct timeval tv;
    int result = gettimeofday(&tv, NULL);
    tp->tv_sec = tv.tv_sec;
    tp->tv_usec = tv.tv_usec;
    return result;
#endif
}


#ifdef _WIN32

static const LONGLONG UnixEpochInTicks = 116444736000000000LL; /* difference between 1970 and 1601 */
static const LONGLONG TicksPerMs = 10000LL;                    /* 1 tick is 100 nanoseconds */

/*
 * If you take the limit of SYSTEMTIME (last millisecond in 30827) then you end up with
 * a FILETIME of 0x7fff35f4f06c58f0 by using SystemTimeToFileTime(). However, if you put
 * 0x7fffffffffffffff into FileTimeToSystemTime() then you will end up in the year 30828,
 * although this date is invalid for SYSTEMTIME. Any larger value (0x8000000000000000 and above)
 * causes FileTimeToSystemTime() to fail.
 * https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-systemtime
 * https://docs.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime
 */
static const LONGLONG UnixEpochOfDate_1601_01_02 = -11644387200000LL;   /* unit: ms */
static const LONGLONG UnixEpochOfDate_30827_12_29 = 9106702560000000LL; /* unit: ms */

/* https://support.microsoft.com/en-us/help/167296/how-to-convert-a-unix-time-t-to-a-win32-filetime-or-systemtime */
static void UnixTimeMsToFileTime(double t, LPFILETIME pft)
{
    LONGLONG ll = (LONGLONG)t * TicksPerMs + UnixEpochInTicks;
    pft->dwLowDateTime = (DWORD)ll;
    pft->dwHighDateTime = (DWORD)(ll >> 32);
} /* UnixTimeMsToFileTime */

#endif /* _WIN32 */

int qjs_gettimezoneoffset(int64_t time)
{
#if defined(_WIN32)
    FILETIME utcFileTime, localFileTime;
    SYSTEMTIME utcSystemTime, localSystemTime;
    int timeConverted = 0;

    /*
    * If the time is earlier than the date 1601-01-02, then always using date 1601-01-02 to
    * query time zone adjustment. This date (1601-01-02) will make sure both UTC and local
    * time succeed with Win32 API. The date 1601-01-01 may lead to a win32 api failure, as
    * after converting between local time and utc time, the time may be earlier than 1601-01-01
    * in UTC time, that exceeds the FILETIME representation range.
    */
    if (time < (double)UnixEpochOfDate_1601_01_02)
    {
        time = (double)UnixEpochOfDate_1601_01_02;
    }

    /* Like above, do not use the last supported day */
    if (time > (double)UnixEpochOfDate_30827_12_29)
    {
        time = (double)UnixEpochOfDate_30827_12_29;
    }

    UnixTimeMsToFileTime (time, &utcFileTime);
    if (FileTimeToSystemTime (&utcFileTime, &utcSystemTime)
        && SystemTimeToTzSpecificLocalTime (0, &utcSystemTime, &localSystemTime)
        && SystemTimeToFileTime (&localSystemTime, &localFileTime))
    {
      timeConverted = 1;
    }
    if (timeConverted)
    {
        ULARGE_INTEGER utcTime, localTime;
        utcTime.LowPart = utcFileTime.dwLowDateTime;
        utcTime.HighPart = utcFileTime.dwHighDateTime;
        localTime.LowPart = localFileTime.dwLowDateTime;
        localTime.HighPart = localFileTime.dwHighDateTime;
        return (int)(((LONGLONG)localTime.QuadPart - (LONGLONG)utcTime.QuadPart) / TicksPerMs);
    }
    return 0.0;
#else
    time_t ti;
    struct tm tm;

    time /= 1000; /* convert to seconds */
    if (sizeof(time_t) == 4) {
        /* on 32-bit systems, we need to clamp the time value to the
           range of `time_t`. This is better than truncating values to
           32 bits and hopefully provides the same result as 64-bit
           implementation of localtime_r.
         */
        if ((time_t)-1 < 0) {
            if (time < INT32_MIN) {
                time = INT32_MIN;
            } else if (time > INT32_MAX) {
                time = INT32_MAX;
            }
        } else {
            if (time < 0) {
                time = 0;
            } else if (time > UINT32_MAX) {
                time = UINT32_MAX;
            }
        }
    }
    ti = time;
    localtime_r(&ti, &tm);
    return -tm.tm_gmtoff / 60;
#endif
}

int64_t qjs_get_time_ms(void)
{
#if defined(__linux__) || defined(__APPLE__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
#else
    struct qjs_timeval tv;
    qjs_gettimeofday(&tv);
    return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
#endif
}

void qjs_usleep(int32_t us)
{
#if defined(_WIN32)
    Sleep(us / 1000);
#else
    usleep(us);
#endif    
}

#if !defined(_WIN32)
static int qjs__fs_scandir_filter(struct dirent *dent) {
  return strcmp(dent->d_name, ".") != 0 && strcmp(dent->d_name, "..") != 0;
}
#endif

int qjs_listdir(void* context, const char* path, int recurse, qjs_listdir_callback_t callback)
{
#if defined(_WIN32)
    HANDLE hFind;
    WIN32_FIND_DATA wfd;
    BOOL cont = TRUE;
    char search_path[MAX_PATH];

    sprintf(search_path, "%s\\*", path);

    if ((hFind = FindFirstFile(search_path, &wfd)) == INVALID_HANDLE_VALUE)
    {
        return -1;
    }

    while (cont == TRUE)
    {
        if ((strncmp(".", wfd.cFileName, 1) != 0) && (strncmp("..", wfd.cFileName, 2) != 0))
        {
            sprintf(search_path, "%s\\%s", path, wfd.cFileName);
            if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (callback(context, search_path, 1) != 0)
                {
                    goto done;
                }
                if (recurse)
                {
                    if (qjs_listdir(context, search_path, recurse, callback) != 0)
                    {
                        goto done;
                    }
                }
            }
            else
            {
                if (callback(context, search_path, 0) != 0)
                {
                    goto done;
                }
            }
        }
        cont = FindNextFile(hFind, &wfd);
    }
done:
    if (GetLastError() != ERROR_NO_MORE_FILES)
    {
        FindClose(hFind);
        return -1;
    }
    if (FindClose(hFind) == FALSE)
    {
        return -1;
    }
    return 0;
#else
    DIR *dir = NULL;
    struct dirent *dent = NULL;
    char search_path[PATH_MAX];
    int result = -1;

    dir = opendir(path);
    if (dir == NULL)
    {
        return -1;
    }

    while ((dent = readdir(dir)) != NULL)
    {
        if (qjs__fs_scandir_filter(dent)) {
            sprintf(search_path, "%s/%s", path, dent->d_name);
            if (dent->d_type & DT_DIR) {
                if (callback(context, search_path, 1) != 0)
                {
                    goto done;
                }
                if (recurse)
                {
                    if (qjs_listdir(context, search_path, recurse, callback) != 0)
                    {
                        goto done;
                    }
                }
            } else {
                if (callback(context, search_path, 0) != 0)
                {
                    goto done;
                }
            }
        }
    }
    result = 0;
done:
    closedir(dir);
    return result;
#endif
}

int qjs_realpath(const char* from_path, char* buf)
{
#if defined(_WIN32)
    {
        char* lppPart= NULL;
        SetLastError(0);
        GetFullPathNameA(from_path, sizeof(buf), buf, &lppPart);
        return GetLastError();
    }
#else
    {
        char *res = realpath(from_path, buf);
        if (!res) {
            buf[0] = '\0';
            return errno;
        } else {
            return 0;
        }
    }
#endif
}

extern force_inline void *qjs_malloc(size_t __size)
{
    return malloc(__size);
}

extern force_inline void *qjs_realloc(void *__ptr, size_t __size)
{
    return realloc(__ptr, __size);
}

extern force_inline void qjs_free(void *__ptr)
{
    free(__ptr);
}

extern force_inline size_t qjs_malloc_usable_size(const void *ptr)
{
#if defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(_WIN32)
    return _msize((void *)ptr);
#elif defined(EMSCRIPTEN)
    return 0;
#elif defined(__linux__)
    return malloc_usable_size((void*)ptr);
#else
    /* change this to `return 0;` if compilation fails */
    return malloc_usable_size(ptr);
#endif
}

#if defined(_WIN32)

typedef struct _internal_parameters
{
    qjs_thread_method i_method;
    void*             i_data;
}t_internal_parameters;

static DWORD WINAPI internal_method_ptr(LPVOID arg)
{
    t_internal_parameters *params = (t_internal_parameters *)arg;
    params->i_method(params->i_data);
    free(params);
    return 0;
}

int qjs_thread_create(qjs_thread* thread, qjs_thread_method method, void* data, int detached)
{
    t_internal_parameters* params = (t_internal_parameters *)malloc(sizeof(t_internal_parameters));
    if(params)
    {
        params->i_method = method;
        params->i_data   = data;
        *thread = CreateThread(NULL, 0, internal_method_ptr, params, 0, NULL);
        if(*thread == NULL)
        {
            free(params);
            return 1;
        }
        return 0;
    }
    return 1;
}

int qjs_thread_join(qjs_thread* thread)
{
    if(WaitForSingleObject(*thread, INFINITE) != WAIT_FAILED)
    {
        if(CloseHandle(*thread))
        {
            return 0;
        }
    }
    return 1;
}
#else

int qjs_thread_create(qjs_thread* thread, qjs_thread_method method, void* data, int detached)
{
    int ret;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    /* no join at the end */
    if (detached) {
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    }
    ret = pthread_create(thread, &attr, (void *)method, data);
    pthread_attr_destroy(&attr);
    return ret;
}


int qjs_thread_join(qjs_thread* thread)
{
    return pthread_join(*thread, NULL);
}

#endif

/* https://github.com/pierreguillot/thread */
int qjs_mutex_init(qjs_mutex* mutex)
{
#if defined(_WIN32)
    *mutex = malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection((CRITICAL_SECTION*)*mutex);
#endif
    return 0;
}

//! @brief Locks a mutex.
int qjs_mutex_lock(qjs_mutex* mutex)
{
#if defined(_WIN32)
    EnterCriticalSection((CRITICAL_SECTION*)*mutex);
    return 0;
#else
    return pthread_mutex_lock(mutex);
#endif
}

//! @brief Tries to locks a mutex.
int qjs_mutex_trylock(qjs_mutex* mutex)
{
#if defined(_WIN32)
    return !TryEnterCriticalSection((CRITICAL_SECTION*)*mutex);
#else
    return pthread_mutex_trylock(mutex);
#endif
}

//! @brief Unlocks a mutex.
int qjs_mutex_unlock(qjs_mutex* mutex)
{
#if defined(_WIN32)
    LeaveCriticalSection((CRITICAL_SECTION*)*mutex);
    return 0;
#else
    return pthread_mutex_unlock(mutex);
#endif
}

//! @brief Destroy a mutex.
int qjs_mutex_destroy(qjs_mutex* mutex)
{
    int result = 0;
#if defined(_WIN32)
    DeleteCriticalSection((CRITICAL_SECTION*)*mutex);
    free(*mutex);
    *mutex = NULL;
#else
#endif
    return result;
}

#if defined(_WIN32)
int qjs_condition_init(qjs_condition* cond)
{
    cond->Ptr = 0;
    InitializeConditionVariable((CONDITION_VARIABLE*)cond);
    return 0;
}

int qjs_condition_signal(qjs_condition* cond)
{
    WakeConditionVariable((CONDITION_VARIABLE*)cond);
    return 0;
}

int qjs_condition_broadcast(qjs_condition* cond)
{
    WakeAllConditionVariable((CONDITION_VARIABLE*)cond);
    return 0;
}

int qjs_condition_wait(qjs_condition* cond, qjs_mutex* mutex)
{
    return !SleepConditionVariableCS((CONDITION_VARIABLE*)cond, (CRITICAL_SECTION*)*mutex, INFINITE);
}

int qjs_condition_timedwait(qjs_condition* cond, qjs_mutex* mutex, int64_t time_ns)
{
    if (SleepConditionVariableCS((CONDITION_VARIABLE*)cond, (CRITICAL_SECTION*)*mutex, time_ns / 1000000)) {
        return 0;
    }
    if (GetLastError() == ERROR_TIMEOUT) {
        return ETIMEDOUT;
    }
    return 1;
}

int qjs_condition_destroy(qjs_condition* cond)
{
    cond->Ptr = 0;
    return 0;
}

#else

int qjs_condition_init(qjs_condition* cond)
{
    return pthread_cond_init(cond, NULL);
}

int qjs_condition_signal(qjs_condition* cond)
{
    return pthread_cond_signal(cond);
}

int qjs_condition_broadcast(qjs_condition* cond)
{
    return pthread_cond_broadcast(cond);
}

int qjs_condition_wait(qjs_condition* cond, qjs_mutex* mutex)
{
    return pthread_cond_wait(cond, mutex);
}

int qjs_condition_timedwait(qjs_condition* cond, qjs_mutex* mutex, int64_t time_ns)
{
    /* XXX: use clock monotonic */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += time_ns / 1000000000;
    ts.tv_nsec += time_ns % 1000000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_nsec -= 1000000000;
        ts.tv_sec++;
    }
    return pthread_cond_timedwait(cond, mutex, &ts);
}

int qjs_condition_destroy(qjs_condition* cond)
{
    int result = pthread_cond_destroy(cond);
    return result;
}
#endif