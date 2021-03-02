//-< FILE.CPP >------------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-98    K.A. Knizhnik  * / [] \ *
//                          Last update: 10-Dec-98    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// System dependent implementation of mapped on memory file
//-------------------------------------------------------------------*--------*

#include "stdtp.h"
#include "database.h"

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

dbFile::~dbFile() { close(); }

#if defined(_WIN32)

  // class OS_info : public OSVERSIONINFO {
  //  public:
  //    OS_info() {
  //        dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
  //        GetVersionEx(this);
  //    }
  //};
  //
  // static OS_info osinfo;

#define BAD_POS 0xFFFFFFFF // returned by SetFilePointer and GetFileSize

dbFile::dbFile() { fh = INVALID_HANDLE_VALUE; }

int dbFile::open(char const *fileName, int attr) {
#ifdef UNICODE
  WCHAR buffer[MAX_PATH] = {0};
  //utf8_to_wstr(fileName, strlen(fileName), buffer, MAX_PATH);
  MultiByteToWideChar(CP_UTF8, 0, fileName,-1, buffer, MAX_PATH);
  fh = CreateFile(buffer,
                  (attr & read_only)
#else
  fh = CreateFile(fileName,
                  (attr & read_only)
#endif
                      ? GENERIC_READ
                      : GENERIC_READ | GENERIC_WRITE,
                  (attr & read_only) ? FILE_SHARE_READ : 0, NULL,
                  (attr & read_only)
                      ? OPEN_EXISTING
                      : (attr & truncate) ? CREATE_ALWAYS : OPEN_ALWAYS,
#ifdef _WINCE
                  FILE_ATTRIBUTE_NORMAL,
#else
                  ((attr & no_buffering) ? FILE_FLAG_NO_BUFFERING : 0) |
                      ((attr & sequential) ? FILE_FLAG_SEQUENTIAL_SCAN
                                           : FILE_FLAG_RANDOM_ACCESS),
#endif
                  NULL);
  if (fh == INVALID_HANDLE_VALUE) { return GetLastError(); }
  return ok;
}

int dbFile::open(wchar_t const *fileName, int attr) {
  fh = CreateFileW(
      fileName,
      (attr & read_only) ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
      (attr & read_only) ? FILE_SHARE_READ : 0, NULL,
      (attr & read_only) ? OPEN_EXISTING
                         : (attr & truncate) ? CREATE_ALWAYS : OPEN_ALWAYS,
#ifdef _WINCE
      FILE_ATTRIBUTE_NORMAL,
#else
      ((attr & no_buffering) ? FILE_FLAG_NO_BUFFERING : 0) |
          ((attr & sequential) ? FILE_FLAG_SEQUENTIAL_SCAN
                               : FILE_FLAG_RANDOM_ACCESS),
#endif
      NULL);
  if (fh == INVALID_HANDLE_VALUE) { return GetLastError(); }
  return ok;
}

int dbFile::read(offs_t pos, void *buf, length_t size) {
  DWORD readBytes;
  // if (osinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
  OVERLAPPED Overlapped;
  Overlapped.Offset     = nat8_low_part(pos);
  Overlapped.OffsetHigh = nat8_high_part(pos);
  Overlapped.hEvent     = NULL;
  if (ReadFile(fh, buf, size, &readBytes, &Overlapped)) {
    return readBytes == size ? ok : eof;
  } else {
    int rc = GetLastError();
    return (rc == ERROR_HANDLE_EOF) ? eof : rc;
  }
  //} else {
  //    LONG high_pos = nat8_high_part(pos);
  //    LONG low_pos = nat8_low_part(pos);
  //    dbCriticalSection cs(mutex);
  //    if (SetFilePointer(fh, low_pos,
  //                       &high_pos, FILE_BEGIN) != BAD_POS
  //        && ReadFile(fh, buf, size, &readBytes, NULL))
  //    {
  //        return (readBytes == size) ? ok : eof;
  //    } else {
  //        int rc = GetLastError();
  //        return rc == ERROR_HANDLE_EOF ? eof : rc;
  //    }
  //}
}

int dbFile::read(void *buf, length_t size) {
  DWORD readBytes;
  if (ReadFile(fh, buf, size, &readBytes, NULL)) {
    return (readBytes == size) ? ok : eof;
  } else {
    int rc = GetLastError();
    return rc == ERROR_HANDLE_EOF ? eof : rc;
  }
}

int dbFile::setSize(offs_t size) {
  LONG low_pos  = nat8_low_part(size);
  LONG high_pos = nat8_high_part(size);
  if (SetFilePointer(fh, low_pos, &high_pos, FILE_BEGIN) == BAD_POS ||
      !SetEndOfFile(fh)) {
    return GetLastError();
  }
  return ok;
}

int dbFile::write(void const *buf, length_t size) {
  DWORD writtenBytes;
  return !WriteFile(fh, buf, size, &writtenBytes, NULL)
             ? GetLastError()
             : (writtenBytes == size) ? int(ok) : int(eof);
}

int dbFile::write(offs_t pos, void const *buf, length_t size) {
  DWORD writtenBytes;
  // if (osinfo.dwPlatformId == VER_PLATFORM_WIN32_NT) {
  OVERLAPPED Overlapped;
  Overlapped.Offset     = nat8_low_part(pos);
  Overlapped.OffsetHigh = nat8_high_part(pos);
  Overlapped.hEvent     = NULL;
  return WriteFile(fh, buf, size, &writtenBytes, &Overlapped)
             ? writtenBytes == size ? int(ok) : int(eof)
             : GetLastError();
  //} else {
  //    LONG high_pos = nat8_high_part(pos);
  //    LONG low_pos = nat8_low_part(pos);
  //    dbCriticalSection cs(mutex);
  //    return SetFilePointer(fh, low_pos, &high_pos, FILE_BEGIN)
  //        == BAD_POS ||
  //        !WriteFile(fh, buf, size, &writtenBytes, NULL)
  //        ? GetLastError()
  //        : (writtenBytes == size) ? int(ok) : int(eof);
  //}
}

int dbFile::flush() { return FlushFileBuffers(fh) ? int(ok) : GetLastError(); }

int dbFile::close() {
  if (fh != INVALID_HANDLE_VALUE) {
    if (CloseHandle(fh)) {
      fh = INVALID_HANDLE_VALUE;
      return ok;
    } else {
      return GetLastError();
    }
  } else {
    return ok;
  }
}

void *dbFile::allocateBuffer(length_t size) {
  return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void dbFile::protectBuffer(void *buf, length_t size, bool readonly) {
  DWORD oldProt;
  VirtualProtect(buf, size, readonly ? PAGE_READONLY : PAGE_READWRITE,
                 &oldProt);
}

void dbFile::deallocateBuffer(void *buffer, length_t /*size*/) {
  VirtualFree(buffer, 0, MEM_RELEASE);
}

char *dbFile::errorText(int code, char *buf, length_t bufSize) {
  int len = 0;

  switch (code) {
  case ok: strncpy(buf, "No error", bufSize); break;
  case eof: strncpy(buf, "Transfer less bytes than specified", bufSize); break;
  default:
#ifndef UNDER_CE
    len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, code, 0, buf,
                         bufSize, NULL);
#endif
    if (len == 0) {
      char errcode[64];
      sprintf(errcode, "unknown error %u", code);
      strncpy(buf, errcode, bufSize);
    }
  }
  return buf;
}

#else // Unix

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifdef __linux__
#define lseek(fd, offs, whence) lseek64(fd, offs, whence)
#endif

dbFile::dbFile() { fd = -1; }

int dbFile::open(char const *fileName, int attr) {
  char *name = (char *)fileName;
#if defined(__sun)
  fd         = ::open64(name,
                ((attr & read_only) ? O_RDONLY : O_CREAT | O_RDWR) |
                    ((attr & truncate) ? O_TRUNC : 0),
                0666);
  if (fd >= 0) { directio(fd, DIRECTIO_ON); }
#elif defined(_AIX)
#if defined(_AIX43)
  fd =
      ::open64(name,
               ((attr & read_only) ? O_RDONLY
                                   : O_CREAT | O_RDWR | O_LARGEFILE | O_DSYNC |
                                         ((attr & no_buffering) ? O_DIRECT : 0))
#else
     fd = ::open64(name, ((attr & read_only) ? O_RDONLY : O_CREAT|O_RDWR|O_LARGEFILE
#endif /* _AIX43 */
                   | ((attr & truncate) ? O_TRUNC : 0),
               0666);
#else
  fd = ::open(name,
              O_LARGEFILE | ((attr & read_only) ? O_RDONLY : O_CREAT | O_RDWR) |
                  ((attr & truncate) ? O_TRUNC : 0),
              0666);
#endif
  if (fd < 0) { return errno; }
  return ok;
}

//int dbFile::open(wchar_t const *fileName, int attr) {
//  return open(cvt::w2a(fileName), attr);
//}

int dbFile::setSize(offs_t size) { return ftruncate(fd, size); }

int dbFile::read(offs_t pos, void *buf, length_t size) {
  ssize_t rc;
#if defined(__sun) || defined(_AIX43)
  rc = pread64(fd, buf, size, pos);
#else
  {
    dbCriticalSection cs(mutex);
    if (offs_t(lseek(fd, pos, SEEK_SET)) != pos) { return errno; }
    rc = ::read(fd, buf, size);
  }
#endif
  if (rc == -1) {
    return errno;
  } else if (length_t(rc) != size) {
    return eof;
  } else {
    return ok;
  }
}

int dbFile::read(void *buf, length_t size) {
  ssize_t rc = ::read(fd, buf, size);
  if (rc == -1) {
    return errno;
  } else if (length_t(rc) != size) {
    return eof;
  } else {
    return ok;
  }
}

int dbFile::write(void const *buf, length_t size) {
  ssize_t rc = ::write(fd, buf, size);
  if (rc == -1) {
    return errno;
  } else if (length_t(rc) != size) {
    return eof;
  } else {
    return ok;
  }
}

int dbFile::write(offs_t pos, void const *buf, length_t size) {
  ssize_t rc;
#if defined(__sun) || defined(_AIX43)
  rc = pwrite64(fd, buf, size, pos);
#else
  {
    dbCriticalSection cs(mutex);
    if (offs_t(lseek(fd, pos, SEEK_SET)) != pos) { return errno; }
    rc = ::write(fd, buf, size);
  }
#endif
  if (rc == -1) {
    return errno;
  } else if (length_t(rc) != size) {
    return eof;
  } else {
    return ok;
  }
}

int dbFile::flush() {
#if defined(_AIX43)
  return ok; // direct IO is used: no need in flush
#else
  return fsync(fd) != ok ? errno : ok;
#endif
}

int dbFile::close() {
  if (fd != -1) {
    if (::close(fd) == ok) {
      fd = -1;
      return ok;
    } else {
      return errno;
    }
  } else {
    return ok;
  }
}

void *dbFile::allocateBuffer(length_t size) {
//#if defined(__MINGW__) || defined(__CYGWIN__)
  return malloc(size);
//#else
//  return memalign(sizeof(void *),size);
//#endif
}

void dbFile::deallocateBuffer(void *buffer, length_t) { free(buffer); }

char *dbFile::errorText(int code, char *buf, length_t bufSize) {
  switch (code) {
  case ok: strncpy(buf, "No error", bufSize); break;
  case eof: strncpy(buf, "Transfer less bytes than specified", bufSize); break;
  default: strncpy(buf, strerror(code), bufSize);
  }
  return buf;
}

#endif

int dbMultiFile::open(int n, dbSegment *seg, int attr) {
  segment   = new dbFileSegment[n];
  nSegments = n;
  while (--n >= 0) {
    segment[n].size = seg[n].size * dbPageSize;
    segment[n].offs = seg[n].offs;
    int rc          = segment[n].open(seg[n].name, attr);
    if (rc != ok) {
      while (++n < nSegments) {
        segment[n].close();
      }
      return rc;
    }
  }
  return ok;
}

int dbMultiFile::close() {
  if (segment != NULL) {
    for (int i = nSegments; --i >= 0;) {
      int rc = segment[i].close();
      if (rc != ok) { return rc; }
    }
    delete[] segment;
    segment = NULL;
  }
  return ok;
}

int dbMultiFile::setSize(offs_t) { return ok; }

int dbMultiFile::flush() {
  for (int i = nSegments; --i >= 0;) {
    int rc = segment[i].flush();
    if (rc != ok) { return rc; }
  }
  return ok;
}

int dbMultiFile::write(offs_t pos, void const *ptr, length_t size) {
  int         n   = nSegments - 1;
  char const *src = (char const *)ptr;
  for (int i = 0; i < n; i++) {
    if (pos < segment[i].size) {
      if (pos + size > segment[i].size) {
        int rc = segment[i].write(segment[i].offs + pos, src,
                                  length_t(segment[i].size - pos));
        if (rc != ok) { return rc; }
        size -= length_t(segment[i].size - pos);
        src += length_t(segment[i].size - pos);
        pos = 0;
      } else {
        return segment[i].write(segment[i].offs + pos, src, size);
      }
    } else {
      pos -= segment[i].size;
    }
  }
  return segment[n].write(segment[n].offs + pos, src, size);
}

int dbMultiFile::read(offs_t pos, void *ptr, length_t size) {
  int   n   = nSegments - 1;
  char *dst = (char *)ptr;
  for (int i = 0; i < n; i++) {
    if (pos < segment[i].size) {
      if (pos + size > segment[i].size) {
        int rc = segment[i].read(segment[i].offs + pos, dst,
                                 length_t(segment[i].size - pos));
        if (rc != ok) { return rc; }
        size -= length_t(segment[i].size - pos);
        dst += length_t(segment[i].size - pos);
        pos = 0;
      } else {
        return segment[i].read(segment[i].offs + pos, dst, size);
      }
    } else {
      pos -= segment[i].size;
    }
  }
  return segment[n].read(segment[n].offs + pos, dst, size);
}

int dbRaidFile::setSize(offs_t) { return ok; }

int dbRaidFile::write(offs_t pos, void const *ptr, length_t size) {
  char const *src = (char const *)ptr;
  for (;;) {
    int      i         = (int)(pos / raidBlockSize % nSegments);
    int      offs      = (unsigned)pos % raidBlockSize;
    length_t available = raidBlockSize - offs;
    if (available >= size) {
      return segment[i].write(
          segment[i].offs + pos / (raidBlockSize * nSegments) * raidBlockSize +
              offs,
          src, size);
    }
    int rc = segment[i].write(
        segment[i].offs + pos / (raidBlockSize * nSegments) * raidBlockSize +
            offs,
        src, available);
    if (rc != ok) { return rc; }
    src += available;
    pos += available;
    size -= available;
  }
}

int dbRaidFile::read(offs_t pos, void *ptr, length_t size) {
  char *dst = (char *)ptr;
  for (;;) {
    int      i         = (int)(pos / raidBlockSize % nSegments);
    int      offs      = (unsigned)pos % raidBlockSize;
    length_t available = raidBlockSize - offs;
    if (available >= size) {
      return segment[i].read(
          segment[i].offs + pos / (raidBlockSize * nSegments) * raidBlockSize +
              offs,
          dst, size);
    }
    int rc = segment[i].read(
        segment[i].offs + pos / (raidBlockSize * nSegments) * raidBlockSize +
            offs,
        dst, available);
    if (rc != ok) { return rc; }
    dst += available;
    pos += available;
    size -= available;
  }
}
