//-< PAGEPOOL.H >----------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:      6-Feb-99    K.A. Knizhnik  * / [] \ *
//                          Last update:  6-Feb-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// Page pool interface
//-------------------------------------------------------------------*--------*

#include "file.h"

class dbPageLruList {
public:
  int next;
  int prev;
};

class dbPageHeader : public dbPageLruList {
public:
  int    collisionChain;
  int    accessCount;
  offs_t offs;
  int    writeQueueIndex;
  int    state;
  enum PageState {  // flag in accessCount field
    psDirty = 0x01, // page has been modified
    psRaw   = 0x02, // page is loaded from the disk
    psWait  = 0x04  // other thread(s) wait load operation completion
  };
};

class dbGetTie;
class dbPutTie;
class dbDatabase;

class dbPagePool {
  friend class dbGetTie;
  friend class dbPutTie;
  friend class dbDatabase;

protected:
  dbPageHeader *pages;
  int *         hashTable;
  int           freePages;
  int           nPages;

  dbFile *    file;
  dbDatabase *db;
  length_t    hashBits;
  length_t    poolSize;
  byte *      buffer;
  length_t    bufferSize;
  offs_t      fileSize;

  int flushing;

  enum {
    initialWobArraySize = 8,
    minPoolSize         = 256,
    minHashSize         = 16 * 1024,
    maxUnusedMemory     = 64 * 1024 * 1024
  };

  length_t       nDirtyPages;
  dbPageHeader **dirtyPages;

  byte *find(offs_t addr, int state);

public:
  byte *get(offs_t addr) { return find(addr, 0); }
  byte *put(offs_t addr) { return find(addr, dbPageHeader::psDirty); }
  void  put(offs_t addr, byte *obj, length_t size);
  void  copy(offs_t dst, offs_t src, length_t size);
  void  unfix(void *ptr);
  void  unfixLIFO(void *ptr);
  void  fix(void *ptr);
  void  modify(void *ptr);
  bool  open(dbFile *file, offs_t fileSize);
  void  close();
  void  flush();

  bool destructed() { return pages == NULL; }

  dbPagePool(dbDatabase *dbs, length_t size) : db(dbs), poolSize(size) {}
};

class dbGetTie {
  friend class dbDatabase;
  friend class dbAnyCursor;
  dbPagePool *pool;
  byte *      obj;
  byte *      page;

  void set(dbPagePool &pool, offs_t pos);
  void reset();

public:
  byte *get() { return obj; }

  dbGetTie() { obj = NULL; }
  ~dbGetTie() { reset(); }
};

class dbPutTie {
  friend class dbDatabase;
  friend class dbBtree;

  dbPagePool *pool;
  byte *      obj;
  byte *      page;
  length_t    size;
  offs_t      pos;
  oid_t       oid;

  void set(dbPagePool &pool, oid_t oid, offs_t pos, length_t size);
  void reset();
  void unset() {
    if (obj != NULL) {
      if (page == NULL) { delete[] obj; }
      obj = NULL;
    }
  }

public:
  byte *get() { return obj; }

  dbPutTie() {
    obj = NULL;
    oid = 0;
  }
  ~dbPutTie() { reset(); }
};
