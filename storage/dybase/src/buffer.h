
#ifndef __BUFFER_H__
#define __BUFFER_H__

template <class T, int initSize> class dbSmallBuffer {
private:
  T        buf[initSize];
  length_t used;
  T *      ptr;
  length_t allocated;

public:
  dbSmallBuffer() {
    ptr       = buf;
    used      = 0;
    allocated = initSize;
  }

  ~dbSmallBuffer() {
    if (ptr != buf) { delete[] ptr; }
  }

  T *base() { return ptr; }

  length_t size() { return used; }

  T *append(int n) {
    if (n + used > allocated) {
      length_t newSize = n + used > allocated * 2 ? n + used : allocated * 2;
      T *      newBuf  = new T[newSize];
      for (int i = int(used); --i >= 0;) {
        newBuf[i] = ptr[i];
      }
      if (ptr != buf) { delete[] ptr; }
      ptr       = newBuf;
      allocated = newSize;
    }
    T *p = ptr + used;
    used += n;
    return p;
  }
};

template <class T> class dbBuffer {
private:
  length_t used;
  T *      ptr;
  length_t allocated;

public:
  dbBuffer() {
    ptr       = NULL;
    used      = 0;
    allocated = 0;
  }

  ~dbBuffer() { delete[] ptr; }

  T *base() { return ptr; }

  T *grab() {
    T *p = ptr;
    ptr  = NULL;
    return p;
  }

  length_t size() { return used; }

  void add(T val) { *append(1) = val; }

  T *append(int n) {
    if (n + used > allocated) {
      length_t newSize = n + used > allocated * 2 ? n + used : allocated * 2;
      T *      newBuf  = new T[newSize];
      for (int i = int(used); --i >= 0;) {
        newBuf[i] = ptr[i];
      }
      delete[] ptr;
      ptr       = newBuf;
      allocated = newSize;
    }
    T *p = ptr + used;
    used += n;
    return p;
  }
};

#endif
