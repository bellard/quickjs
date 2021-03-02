//-< FILE.CPP >------------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-98    K.A. Knizhnik  * / [] \ *
//                          Last update: 30-Jan-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// System independent intrface to operating system file
//-------------------------------------------------------------------*--------*

#ifndef __FILE_H__
#define __FILE_H__

#include "sync.h"

const length_t dbDefaultRaidBlockSize = 1024 * 1024;

/**
 * Internal implementation of file
 */
class dbFile {
protected:
#if defined(_WIN32)
  HANDLE fh;
#else
  int fd;
#endif
  dbMutex mutex;

public:
  enum ReturnStatus {
    ok  = 0,
    eof = -1 // number of read/written bytes is smaller than requested
  };
  enum OpenAttributes {
    read_only    = 0x01,
    truncate     = 0x02,
    sequential   = 0x04,
    no_buffering = 0x08
  };
  int open(char const *fileName, int attr);
  int open(wchar_t const *fileName, int attr);
  int write(void const *ptr, length_t size);
  int read(void *ptr, length_t size);

  dbFile();
  virtual ~dbFile();

  virtual int flush();
  virtual int close();

  virtual int setSize(offs_t offs);

  virtual int write(offs_t pos, void const *ptr, length_t size);
  virtual int read(offs_t pos, void *ptr, length_t size);

  static void *allocateBuffer(length_t bufferSize);
  static void  deallocateBuffer(void *buffer, length_t size = 0);
  static void  protectBuffer(void *buf, length_t bufSize, bool readonly);

  static length_t ramSize();

  static char *errorText(int code, char *buf, length_t bufSize);
};

/**
 * File consisting of multiple segments
 */
class dbMultiFile : public dbFile {
public:
  struct dbSegment {
    char * name;
    offs_t size;
    offs_t offs;
  };

  int open(int nSegments, dbSegment *segments, int attr);

  virtual int setSize(offs_t offs);

  virtual int flush();
  virtual int close();

  virtual int write(offs_t pos, void const *ptr, length_t size);
  virtual int read(offs_t pos, void *ptr, length_t size);

  dbMultiFile() { segment = NULL; }
  ~dbMultiFile() {}

protected:
  class dbFileSegment : public dbFile {
  public:
    offs_t size;
    offs_t offs;
  };
  int            nSegments;
  dbFileSegment *segment;
};

/*
 * RAID-1 file. Scattern file blocks between several physical segments
 */
class dbRaidFile : public dbMultiFile {
  length_t raidBlockSize;

public:
  dbRaidFile(length_t blockSize) { raidBlockSize = blockSize; }

  virtual int setSize(offs_t offs);

  virtual int write(offs_t pos, void const *ptr, length_t size);
  virtual int read(offs_t pos, void *ptr, length_t size);
};

#endif
