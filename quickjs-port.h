/*
 * QuickJS C library
 * 
 * Copyright (c) 2017-2018 Fabrice Bellard
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
#ifndef QUICKJS_PORT_H
#define QUICKJS_PORT_H

#include <stdlib.h>
#include <stdint.h>
#if defined(_MSC_VER)
#include <intrin.h>
#include "win/stdatomic.h"
#else
#include <pthread.h>
#include <stdatomic.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct qjs_timeval {
    int64_t tv_sec;         /* seconds */
    long    tv_usec;        /* and microseconds */
};

//! @brief The thread method.
typedef void (*qjs_thread_method)(void *);

#if defined(_WIN32)
typedef void* qjs_thread;
typedef void* qjs_mutex;
typedef struct qjs_condition_s {
    void* Ptr;
} qjs_condition;
#define QJS_MUTEX_INITIALIZER NULL
#else
typedef pthread_t qjs_thread;
typedef pthread_mutex_t qjs_mutex;
typedef pthread_cond_t qjs_condition;
#define QJS_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#endif

void qjs_abort();
int qjs_gettimeofday(struct qjs_timeval * tp);
int qjs_gettimezoneoffset(int64_t time);
int64_t qjs_get_time_ms(void);
void qjs_usleep(int32_t us);

typedef int (*qjs_listdir_callback_t)(void* context, const char* path, int is_dir);
int qjs_listdir(void* context, const char* path, int recurse, qjs_listdir_callback_t callback);
int qjs_realpath(const char* from_path, char* buf);

/* Memory relate functions */
void *qjs_malloc(size_t __size);
void *qjs_realloc(void *__ptr, size_t __size);
void qjs_free(void *__ptr);
size_t qjs_malloc_usable_size(const void *ptr);

#if defined(EMSCRIPTEN)

static inline uint8_t *qjs_get_stack_pointer(void)
{
    return NULL;
}

static inline size_t qjs_stack_size(const uint8_t* stack_top)
{
    return stack_top - stack_top;
}
#else

/* Note: OS and CPU dependent */
static inline uint8_t *qjs_get_stack_pointer(void)
{
#if defined(_MSC_VER)
    return _AddressOfReturnAddress();
#else
    return __builtin_frame_address(0);
#endif
}

static inline size_t qjs_stack_size(const uint8_t* stack_top)
{
    return stack_top - qjs_get_stack_pointer();
}

#endif

//! @brief Detaches a thread.
int qjs_thread_create(qjs_thread* thread, qjs_thread_method method, void* data, int detached);

//! @brief Joins a thread.
int qjs_thread_join(qjs_thread* thread);

/* Mutex relaed functions */
int qjs_mutex_init(qjs_mutex* mutex);

//! @brief Locks a mutex.
int qjs_mutex_lock(qjs_mutex* mutex);

//! @brief Tries to locks a mutex.
int qjs_mutex_trylock(qjs_mutex* mutex);

//! @brief Unlocks a mutex.
int qjs_mutex_unlock(qjs_mutex* mutex);

//! @brief Destroy a mutex.
int qjs_mutex_destroy(qjs_mutex* mutex);

//! @brief Initializes a condition.
int qjs_condition_init(qjs_condition* cond);

//! @brief Restarts one of the threads that are waiting on the condition.
int qjs_condition_signal(qjs_condition* cond);

//! @brief Shall unblock all threads currently blocked on the specified condition variable cond.
int qjs_condition_broadcast(qjs_condition* cond);

//! @brief Unlocks the mutex and waits for the condition to be signalled.
int qjs_condition_wait(qjs_condition* cond, qjs_mutex* mutex);

//! @brief Unlocks the mutex and waits for the condition to be signalled or reach abstime.
int qjs_condition_timedwait(qjs_condition* cond, qjs_mutex* mutex, int64_t time_ns);

//! @brief Destroy a condition.
int qjs_condition_destroy(qjs_condition* cond);

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_PORT_H */
