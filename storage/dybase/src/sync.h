//-< SYNC.H >--------------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-98    K.A. Knizhnik  * / [] \ *
//                          Last update:  8-Feb-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// Intertask synchonization primitives
//-------------------------------------------------------------------*--------*

#ifndef __SYNC_H__
#define __SYNC_H__

#if defined(_WIN32)
class dbMutex {
  CRITICAL_SECTION cs;

public:
  dbMutex() { InitializeCriticalSection(&cs); }
  ~dbMutex() { DeleteCriticalSection(&cs); }
  void lock() { EnterCriticalSection(&cs); }
  void unlock() { LeaveCriticalSection(&cs); }
};

#else // Unix

#ifndef NO_PTHREADS

#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

class dbMutex {
  friend class dbEvent;
  friend class dbSemaphore;
  pthread_mutex_t cs;

public:
  dbMutex() { pthread_mutex_init(&cs, NULL); }
  ~dbMutex() { pthread_mutex_destroy(&cs); }
  void lock() { pthread_mutex_lock(&cs); }
  void unlock() { pthread_mutex_unlock(&cs); }
};

#else

class dbMutex {
public:
  void lock() {}
  void unlock() {}
};

#endif

#endif

class dbCriticalSection {
private:
  dbMutex &mutex;
  dbCriticalSection(const dbCriticalSection &);
  dbCriticalSection &operator=(const dbCriticalSection &);

public:
  dbCriticalSection(dbMutex &guard) : mutex(guard) { mutex.lock(); }
  ~dbCriticalSection() { mutex.unlock(); }
};

#endif
