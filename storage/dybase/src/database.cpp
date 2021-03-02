//-< DATABASE.CPP >--------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-1998  K.A. Knizhnik  * / [] \ *
//                          Last update: 23-Nov-2001  K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// Database memory management, query execution, scheme evaluation
//-------------------------------------------------------------------*--------*

#include "stdtp.h"
#include "database.h"
#include "btree.h"

void dbDatabase::handleError(int error, char const *msg) {
  if (errorHandler != NULL) {
    (*errorHandler)(error, msg);
  } else {
    fprintf(stderr, "Error %d: %s\n", error, msg);
  }
}

void dbDatabase::throwException(int error, char const *msg) {
  handleError(error, msg);
  throw dbException(error, msg);
}

//bool dbDatabase::open(const wchar_t *name, int openAttr) {
  //return open(cvt::w2a(name), openAttr);
//}

bool dbDatabase::open(const char *name, int openAttr) {
  dbCriticalSection cs(mutex);
  int               rc;
  opened = false;

  length_t indexSize =
      initIndexSize < dbFirstUserId ? length_t(dbFirstUserId) : initIndexSize;
  indexSize = DOALIGN(indexSize, dbHandlesPerPage);

  memset(dirtyPagesMap, 0, dbDirtyPageBitmapSize + 4);

  for (int i = dbBitmapId + dbBitmapPages; --i >= 0;) {
    bitmapPageAvailableSpace[i] = INT_MAX;
  }
  currRBitmapPage = currPBitmapPage = dbBitmapId;
  currRBitmapOffs = currPBitmapOffs = 0;
  reservedChain                     = NULL;
  classDescList                     = NULL;
  gcThreshold                       = 0;
  allocatedDelta                    = 0;
  gcDone                            = false;
  modified                          = false;

  if (accessType == dbReadOnly) { openAttr |= dbFile::read_only; }
  if (*name == L'@') {
    // char fn[1024];
    // wcstombs ( fn, name+1, 1024 );
    FILE *f = fopen(name, "r");
    if (f == NULL) {
      handleError(dybase_open_error,
                  "Failed to open database configuration file");
      return false;
    }
    dbMultiFile::dbSegment segment[dbMaxFileSegments];
    const int              maxFileNameLen = 1024;
    char                   fileName[maxFileNameLen];
    int                    i, n;
    db_int8                size;
    bool                   raid          = false;
    length_t               raidBlockSize = dbDefaultRaidBlockSize;
    for (i = 0; (n = fscanf(f, "%s" INT8_FORMAT, fileName, &size)) >= 1; i++) {
      if (i == dbMaxFileSegments) {
        while (--i >= 0)
          delete[] segment[i].name;
        fclose(f);
        handleError(dybase_open_error, "Too much segments");
        return false;
      }

      if (n == 1) {
        if (i == 0) {
          raid = true;
        } else if (!raid && segment[i - 1].size == 0) {
          while (--i >= 0)
            delete[] segment[i].name;
          fclose(f);
          handleError(dybase_open_error, "Segment size was not specified");
          return false;
        }
        size = 0;
      } else if (size == 0 || raid) {
        while (--i >= 0)
          delete[] segment[i].name;
        fclose(f);
        handleError(dybase_open_error,
                    size == 0
                        ? "Invalid segment size"
                        : "segment size should not be specified for raid");
        return false;
      }

      if (strcmp(fileName, ".RaidBlockSize") == 0) {
        raidBlockSize = (length_t)size;
        raid          = true;
        i -= 1;
        continue;
      }
      segment[i].size = offs_t(size);
      char *  suffix  = strchr(fileName, '[');
      db_int8 offs    = 0;
      if (suffix != NULL) {
        *suffix = '\0';
        sscanf(suffix + 1, INT8_FORMAT, &offs);
      }
      segment[i].name = new char[strlen(fileName) + 1];
      strcpy(segment[i].name, fileName);
      segment[i].offs = offs_t(offs);
    }
    fclose(f);
    if (i == 0) {
      // fclose(f);
      handleError(dybase_open_error, "File should have at least one segment");
      return false;
    }
    if (i == 1 && raid) { raid = false; }
    dbMultiFile *mfile;
    if (raid) {
      mfile = new dbRaidFile(raidBlockSize);
    } else {
      mfile = new dbMultiFile;
    }
    rc = mfile->open(i, segment, openAttr);
    while (--i >= 0)
      delete[] segment[i].name;
    if (rc != dbFile::ok) {
      delete mfile;
      handleError(dybase_open_error, "Failed to create database file");
      return false;
    }
    file = mfile;
  } else {
    file = new dbFile;
    if (file->open(name, openAttr) != dbFile::ok) {
      delete file;
      handleError(dybase_open_error, "Failed to create database file");
      return false;
    }
  }
  memset(header, 0, sizeof(dbHeader));
  rc = file->read(0, header, dbPageSize);
  if (rc != dbFile::ok && rc != dbFile::eof) {
    delete file;
    handleError(dybase_open_error, "Failed to read file header");
    return false;
  }

  if ((unsigned)header->curr > 1) {
    delete file;
    handleError(dybase_open_error,
                "Database file was corrupted: invalid root index");
    return false;
  }
  if (!header->isInitialized()) {
    if (accessType == dbReadOnly) {
      delete file;
      handleError(dybase_open_error,
                  "Can not open uninitialized file in read only mode");
      return false;
    }
    curr = header->curr           = 0;
    length_t used                 = dbPageSize;
    header->root[0].index         = used;
    header->root[0].indexSize     = indexSize;
    header->root[0].indexUsed     = dbFirstUserId;
    header->root[0].freeList      = 0;
    header->root[0].classDescList = 0;
    header->root[0].rootObject    = 0;
    used += indexSize * sizeof(offs_t);
    header->root[1].index         = used;
    header->root[1].indexSize     = indexSize;
    header->root[1].indexUsed     = dbFirstUserId;
    header->root[1].freeList      = 0;
    header->root[1].classDescList = 0;
    header->root[1].rootObject    = 0;
    used += indexSize * sizeof(offs_t);

    header->root[0].shadowIndex     = header->root[1].index;
    header->root[1].shadowIndex     = header->root[0].index;
    header->root[0].shadowIndexSize = indexSize;
    header->root[1].shadowIndexSize = indexSize;

    length_t bitmapPages =
        (used + dbPageSize * (dbAllocationQuantum * 8 - 1) - 1) /
        (dbPageSize * (dbAllocationQuantum * 8 - 1));
    length_t bitmapSize     = bitmapPages * dbPageSize;
    length_t usedBitmapSize = (used + bitmapSize) / (dbAllocationQuantum * 8);
    byte *   bitmap         = (byte *)dbFile::allocateBuffer(bitmapSize);
    memset(bitmap, 0xFF, usedBitmapSize);
    memset(bitmap + usedBitmapSize, 0, bitmapSize - usedBitmapSize);
    rc = file->write(used, bitmap, bitmapSize);
    dbFile::deallocateBuffer(bitmap);
    if (rc != dbFile::ok) {
      delete file;
      handleError(dybase_open_error, "Failed to write to the file");
      return false;
    }
    length_t bitmapIndexSize =
        DOALIGN((dbBitmapId + dbBitmapPages) * sizeof(offs_t), dbPageSize);
    offs_t *index      = (offs_t *)dbFile::allocateBuffer(bitmapIndexSize);
    index[dbInvalidId] = dbFreeHandleFlag;
    length_t i;
    for (i = 0; i < bitmapPages; i++) {
      index[dbBitmapId + i] = used | dbPageObjectFlag | dbModifiedFlag;
      used += dbPageSize;
    }
    header->root[0].bitmapEnd = dbBitmapId + i;
    header->root[1].bitmapEnd = dbBitmapId + i;
    while (i < dbBitmapPages) {
      index[dbBitmapId + i] = dbFreeHandleFlag;
      i += 1;
    }
    rc = file->write(header->root[1].index, index, bitmapIndexSize);
    dbFile::deallocateBuffer(index);
    if (rc != dbFile::ok) {
      delete file;
      handleError(dybase_open_error, "Failed to write index to the file");
      return false;
    }
    header->root[0].size = used;
    header->root[1].size = used;
    currIndexSize        = dbFirstUserId;
    if (!pool.open(file, used)) {
      delete file;
      handleError(dybase_open_error, "Failed to allocate page pool");
      return false;
    }
    if (dbFileExtensionQuantum != 0) {
      file->setSize(DOALIGN(used, dbFileExtensionQuantum));
    }
    offs_t indexPage = header->root[1].index;
    offs_t lastIndexPage =
        indexPage + header->root[1].bitmapEnd * sizeof(offs_t);
    while (indexPage < lastIndexPage) {
      offs_t *p = (offs_t *)pool.put(indexPage);
      for (i = 0; i < dbHandlesPerPage; i++) {
        p[i] &= ~dbModifiedFlag;
      }
      pool.unfix(p);
      indexPage += dbPageSize;
    }
    pool.copy(header->root[0].index, header->root[1].index,
              currIndexSize * sizeof(offs_t));
    header->dirty        = true;
    header->root[0].size = header->root[1].size;
    if (file->write(0, header, dbPageSize) != dbFile::ok) {
      pool.close();
      delete file;
      handleError(dybase_open_error, "Failed to write to the file");
      return false;
    }
    pool.flush();
    header->initialized = true;
    if (file->write(0, header, dbPageSize) != dbFile::ok ||
        file->flush() != dbFile::ok) {
      pool.close();
      delete file;
      handleError(dybase_open_error, "Failed to complete file initialization");
      return false;
    }
  } else {
    int curr   = header->curr;
    this->curr = curr;
    if (header->root[curr].indexSize != header->root[curr].shadowIndexSize) {
      delete file;
      handleError(dybase_open_error, "Header of database file is corrupted");
      return false;
    }

    if (rc != dbFile::ok) {
      delete file;
      handleError(dybase_open_error, "Failed to read object index");
      return false;
    }
    pool.open(file, header->root[curr].size);
    if (header->dirty) {
      TRACE_MSG(("Database was not normally closed: start recovery\n"));
      if (accessType == dbReadOnly) {
        pool.close();
        delete file;
        handleError(dybase_open_error,
                    "Can not open dirty file in read only mode");
        return false;
      }
      header->root[1 - curr].size        = header->root[curr].size;
      header->root[1 - curr].indexUsed   = header->root[curr].indexUsed;
      header->root[1 - curr].freeList    = header->root[curr].freeList;
      header->root[1 - curr].index       = header->root[curr].shadowIndex;
      header->root[1 - curr].indexSize   = header->root[curr].shadowIndexSize;
      header->root[1 - curr].shadowIndex = header->root[curr].index;
      header->root[1 - curr].shadowIndexSize = header->root[curr].indexSize;
      header->root[1 - curr].bitmapEnd       = header->root[curr].bitmapEnd;
      header->root[1 - curr].rootObject      = header->root[curr].rootObject;
      header->root[1 - curr].classDescList   = header->root[curr].classDescList;

      pool.copy(
          header->root[1 - curr].index, header->root[curr].index,
          DOALIGN(header->root[curr].indexUsed * sizeof(offs_t), dbPageSize));
      TRACE_MSG(("Recovery completed\n"));
    }
    currIndexSize = header->root[1 - curr].indexUsed;
  }
  committedIndexSize = currIndexSize;

  loadScheme();
  opened = true;
  return true;
}

void dbDatabase::loadScheme() {
  dbGetTie            tie;
  dbClassDescriptor **cpp = &classDescList;
  int                 cid = header->root[1 - curr].classDescList;
  while (cid != 0) {
    dbClass *          cls  = ((dbClass *)getObject(tie, cid))->clone();
    dbClassDescriptor *desc = new dbClassDescriptor(cls, cid);
    classOidHash.put(&desc->oid, sizeof(desc->oid), desc);
    classSignatureHash.put(cls->signature, desc->signatureSize, desc);
    *cpp = desc;
    cpp  = &desc->next;
    cid  = cls->next;
  }
  *cpp = NULL;
}

void dbDatabase::close() {
  dbCriticalSection cs(mutex);
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return;
  }
  if (modified) { commitTransaction(); }
  dbClassDescriptor *desc, *next;
  for (desc = classDescList; desc != NULL; desc = next) {
    next = desc->next;
    delete desc;
  }
  classDescList = NULL;
  classOidHash.clear();
  classSignatureHash.clear();

  opened = false;
  if (header->dirty) {
    int rc = file->write(0, header, dbPageSize);
    if (rc != dbFile::ok) {
      throwException(dybase_file_error, "Failed to write header to the disk");
    }
    pool.flush();
    header->dirty = false;
    rc            = file->write(0, header, dbPageSize);
    if (rc != dbFile::ok) {
      throwException(dybase_file_error, "Failed to write header to the disk");
    }
  }
  pool.close();
  file->close();
  delete file;
}

dbObject *dbDatabase::putObject(dbPutTie &tie, oid_t oid) {
  offs_t    pos  = getPos(oid);
  int       offs = (int)pos & (dbPageSize - 1);
  byte *    p    = pool.get(pos - offs);
  dbObject *obj  = (dbObject *)(p + (offs & ~dbFlagsMask));
  if (!(offs & dbModifiedFlag)) {
    dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
        1 << int(oid / dbHandlesPerPage & 31);
    cloneBitmap(pos & ~dbFlagsMask, obj->size);
    allocate(obj->size, oid);
    pos = getPos(oid);
  }
  tie.set(pool, oid, pos & ~dbFlagsMask, obj->size);
  pool.unfix(p);
  return (dbObject *)tie.get();
}

byte *dbDatabase::put(dbPutTie &tie, oid_t oid) {
  offs_t pos = getPos(oid);
  if (!(pos & dbModifiedFlag)) {
    dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
        1 << int(oid / dbHandlesPerPage & 31);
    allocate(dbPageSize, oid);
    cloneBitmap(pos & ~dbFlagsMask, dbPageSize);
    pos = getPos(oid);
  }
  tie.set(pool, oid, pos & ~dbFlagsMask, dbPageSize);
  return tie.get();
}

oid_t dbDatabase::getRoot() { return header->root[1 - curr].rootObject; }

void dbDatabase::setRoot(oid_t oid) {
  header->root[1 - curr].rootObject = oid;
  modified                          = true;
}

dbLoadHandle *dbDatabase::getLoadHandle(oid_t oid) {
  dbCriticalSection cs(mutex);
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return NULL;
  }
  dbLoadHandle *hnd = new dbLoadHandle();
  dbObject *    obj = getObject(hnd->tie, oid);
  hnd->curr         = (byte *)(obj + 1);
  hnd->end          = (byte *)obj + obj->size;
  hnd->desc =
      (dbClassDescriptor *)classOidHash.get(&obj->cid, sizeof(obj->cid));
  // assert(hnd->desc != NULL);
  if (hnd->desc == NULL)
    handleError(dybase_bad_key_type, "Bad object descriptor");
  return hnd;
}

void dbDatabase::storeObject(dbStoreHandle *handle) {
  dbCriticalSection cs(mutex);
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return;
  }
  dbObject *         obj  = (dbObject *)handle->body.base();
  dbClassDescriptor *desc = (dbClassDescriptor *)classSignatureHash.get(
      handle->signature.base(), handle->signature.size());
  if (desc == NULL) {
    dbClass *cls =
        dbClass::create(handle->signature.base(), handle->signature.size());
    cls->next = header->root[1 - curr].classDescList;
    desc      = new dbClassDescriptor(cls, allocateObject(cls));
    header->root[1 - curr].classDescList = desc->oid;
    classOidHash.put(&desc->oid, sizeof(desc->oid), desc);
    classSignatureHash.put(cls->signature, desc->signatureSize, desc);
    desc->next    = classDescList;
    classDescList = desc;
  }
  obj->size  = handle->body.size();
  obj->cid   = desc->oid;
  oid_t  oid = handle->oid;
  offs_t pos = getPos(oid);
  if (pos == 0) {
    pos = allocate(obj->size);
    setPos(oid, pos | dbModifiedFlag);
  } else {
    int      offs    = (int)pos & (dbPageSize - 1);
    byte *   p       = pool.get(pos - offs);
    length_t oldSize = ((dbObject *)(p + (offs & ~dbFlagsMask)))->size;
    pool.unfix(p);
    if (!(offs & dbModifiedFlag)) {
      dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
          1 << int(oid / dbHandlesPerPage & 31);
      cloneBitmap(pos, oldSize);
      pos = allocate(obj->size);
      setPos(oid, pos | dbModifiedFlag);
    } else {
      if (DOALIGN(oldSize, dbAllocationQuantum) !=
          DOALIGN(obj->size, dbAllocationQuantum)) {
        offs_t newPos = allocate(obj->size);
        cloneBitmap(pos & ~dbFlagsMask, oldSize);
        free(pos & ~dbFlagsMask, oldSize);
        pos = newPos;
        setPos(oid, pos | dbModifiedFlag);
      }
    }
  }
  pool.put(pos & ~dbFlagsMask, (byte *)obj, obj->size);
}

oid_t dbDatabase::allocateObject(dbObject *obj) {
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return 0;
  }
  oid_t  oid = allocateId();
  offs_t pos = allocate(obj->size);
  setPos(oid, pos | dbModifiedFlag);
  pool.put(pos, (byte *)obj, obj->size);
  return oid;
}

void dbDatabase::freeObject(oid_t oid) {
  dbCriticalSection cs(mutex);
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return;
  }
  dbObject hdr;
  getHeader(hdr, oid);
  offs_t pos = getPos(oid);
  if (pos & dbModifiedFlag) {
    free(pos & ~dbFlagsMask, hdr.size);
  } else {
    cloneBitmap(pos, hdr.size);
  }
  freeId(oid);
}

void dbDatabase::freePage(oid_t oid) {
  offs_t pos = getPos(oid);
  if (pos & dbModifiedFlag) {
    free(pos & ~dbFlagsMask, dbPageSize);
  } else {
    cloneBitmap(pos & ~dbFlagsMask, dbPageSize);
  }
  freeId(oid);
}

inline void dbDatabase::extend(offs_t size) {
  if (size > header->root[1 - curr].size) {
    if (dbFileExtensionQuantum != 0 &&
        DOALIGN(size, dbFileExtensionQuantum) !=
            DOALIGN(header->root[1 - curr].size, dbFileExtensionQuantum)) {
      file->setSize(DOALIGN(size, dbFileExtensionQuantum));
    }
    header->root[1 - curr].size = size;
  }
}

inline bool dbDatabase::wasReserved(offs_t pos, length_t size) {
  for (dbLocation *location = reservedChain; location != NULL;
       location             = location->next) {
    if (pos - location->pos < location->size || location->pos - pos < size) {
      return true;
    }
  }
  return false;
}

inline void dbDatabase::reserveLocation(dbLocation &location, offs_t pos,
                                        length_t size) {
  location.pos  = pos;
  location.size = size;
  location.next = reservedChain;
  reservedChain = &location;
}

inline void dbDatabase::commitLocation() {
  reservedChain = reservedChain->next;
}

void dbDatabase::setDirty() {
  modified = true;
  if (!header->dirty) {
    header->dirty = true;
    if (file->write(0, header, dbPageSize) != dbFile::ok) {
      throwException(dybase_file_error, "Failed to write header to the file");
    }
    pool.flush();
  }
}

offs_t dbDatabase::allocate(length_t size, oid_t oid) {
  static byte const firstHoleSize[] = {
      8, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0,
      3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6, 0, 1, 0, 2, 0, 1, 0,
      3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0,
      3, 0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0,
      3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 0, 2, 0, 1, 0,
      3, 0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
  static byte const lastHoleSize[] = {
      8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3,
      3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
      2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
      1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  static byte const maxHoleSize[] = {
      8, 7, 6, 6, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3,
      3, 3, 3, 3, 3, 3, 3, 3, 5, 4, 3, 3, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2,
      4, 3, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 6, 5, 4, 4, 3, 3, 3, 3,
      3, 2, 2, 2, 2, 2, 2, 2, 4, 3, 2, 2, 2, 1, 1, 1, 3, 2, 1, 1, 2, 1, 1, 1,
      5, 4, 3, 3, 2, 2, 2, 2, 3, 2, 1, 1, 2, 1, 1, 1, 4, 3, 2, 2, 2, 1, 1, 1,
      3, 2, 1, 1, 2, 1, 1, 1, 7, 6, 5, 5, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3,
      4, 3, 2, 2, 2, 2, 2, 2, 3, 2, 2, 2, 2, 2, 2, 2, 5, 4, 3, 3, 2, 2, 2, 2,
      3, 2, 1, 1, 2, 1, 1, 1, 4, 3, 2, 2, 2, 1, 1, 1, 3, 2, 1, 1, 2, 1, 1, 1,
      6, 5, 4, 4, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 4, 3, 2, 2, 2, 1, 1, 1,
      3, 2, 1, 1, 2, 1, 1, 1, 5, 4, 3, 3, 2, 2, 2, 2, 3, 2, 1, 1, 2, 1, 1, 1,
      4, 3, 2, 2, 2, 1, 1, 1, 3, 2, 1, 1, 2, 1, 1, 0};
  static byte const maxHoleOffset[] = {
      0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 0, 1, 5, 5, 5, 5, 5, 5,
      0, 5, 5, 5, 5, 5, 5, 5, 0, 1, 2, 2, 0, 3, 3, 3, 0, 1, 6, 6, 0, 6, 6, 6,
      0, 1, 2, 2, 0, 6, 6, 6, 0, 1, 6, 6, 0, 6, 6, 6, 0, 1, 2, 2, 3, 3, 3, 3,
      0, 1, 4, 4, 0, 4, 4, 4, 0, 1, 2, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
      0, 1, 2, 2, 0, 3, 3, 3, 0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 2, 2, 0, 1, 0, 3,
      0, 1, 0, 2, 0, 1, 0, 7, 0, 1, 2, 2, 3, 3, 3, 3, 0, 4, 4, 4, 4, 4, 4, 4,
      0, 1, 2, 2, 0, 5, 5, 5, 0, 1, 5, 5, 0, 5, 5, 5, 0, 1, 2, 2, 0, 3, 3, 3,
      0, 1, 0, 2, 0, 1, 0, 4, 0, 1, 2, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
      0, 1, 2, 2, 3, 3, 3, 3, 0, 1, 4, 4, 0, 4, 4, 4, 0, 1, 2, 2, 0, 1, 0, 3,
      0, 1, 0, 2, 0, 1, 0, 5, 0, 1, 2, 2, 0, 3, 3, 3, 0, 1, 0, 2, 0, 1, 0, 4,
      0, 1, 2, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 0};

  setDirty();
  size = DOALIGN(size, dbAllocationQuantum);
  allocatedDelta += size;
  if (gcThreshold != 0 && allocatedDelta > gcThreshold && !gcDone) {
    startGC();
  }

  int            objBitSize = size >> dbAllocationQuantumBits;
  offs_t         pos;
  int            holeBitSize = 0;
  int            alignment   = size & (dbPageSize - 1);
  length_t       offs;
  const int      pageBits = dbPageSize * 8;
  oid_t          firstPage, lastPage;
  int            holeBeforeFreePage = 0;
  oid_t          freeBitmapPage     = 0;
  dbLocation     location;
  dbPutTie       tie;
  oid_t          i;
  const length_t inc = dbPageSize / dbAllocationQuantum / 8;

  lastPage = header->root[1 - curr].bitmapEnd;
  if (alignment == 0) {
    firstPage = currPBitmapPage;
    offs      = DOALIGN(currPBitmapOffs, inc);
  } else {
    firstPage = currRBitmapPage;
    offs      = currRBitmapOffs;
  }

  for (;;) {
    if (alignment == 0) {
      // allocate page object
      for (i = firstPage; i < lastPage; i++) {
        int spaceNeeded = objBitSize - holeBitSize < pageBits
                              ? objBitSize - holeBitSize
                              : pageBits;
        if (bitmapPageAvailableSpace[i] <= spaceNeeded) {
          holeBitSize = 0;
          offs        = 0;
          continue;
        }
        byte *   begin     = get(i);
        length_t startOffs = offs;
        while (offs < dbPageSize) {
          if (begin[offs++] != 0) {
            offs        = DOALIGN(offs, inc);
            holeBitSize = 0;
          } else if ((holeBitSize += 8) == objBitSize) {
            pos =
                ((offs_t(i - dbBitmapId) * dbPageSize + offs) * 8 - holeBitSize)
                << dbAllocationQuantumBits;
            if (wasReserved(pos, size)) {
              offs += objBitSize >> 3;
              startOffs = offs = DOALIGN(offs, inc);
              holeBitSize      = 0;
              continue;
            }
            reserveLocation(location, pos, size);
            currPBitmapPage = i;
            currPBitmapOffs = offs;
            extend(pos + size);
            if (oid != 0) {
              offs_t prev   = getPos(oid);
              int    marker = (int)prev & dbFlagsMask;
              pool.copy(pos, prev - marker, size);
              setPos(oid, pos | marker | dbModifiedFlag);
            }
            pool.unfix(begin);
            begin              = put(tie, i);
            length_t holeBytes = holeBitSize >> 3;
            if (holeBytes > offs) {
              memset(begin, 0xFF, offs);
              holeBytes -= offs;
              begin = put(tie, --i);
              offs  = dbPageSize;
            }
            while (holeBytes > dbPageSize) {
              memset(begin, 0xFF, dbPageSize);
              holeBytes -= dbPageSize;
              bitmapPageAvailableSpace[i] = 0;
              begin                       = put(tie, --i);
            }
            memset(&begin[offs - holeBytes], 0xFF, holeBytes);
            commitLocation();
            return pos;
          }
        }
        if (startOffs == 0 && holeBitSize == 0 &&
            spaceNeeded < bitmapPageAvailableSpace[i]) {
          bitmapPageAvailableSpace[i] = spaceNeeded;
        }
        offs = 0;
        pool.unfix(begin);
      }
    } else {
      for (i = firstPage; i < lastPage; i++) {
        int spaceNeeded = objBitSize - holeBitSize < pageBits
                              ? objBitSize - holeBitSize
                              : pageBits;
        if (bitmapPageAvailableSpace[i] <= spaceNeeded) {
          holeBitSize = 0;
          offs        = 0;
          continue;
        }
        byte *   begin     = get(i);
        length_t startOffs = offs;

        while (offs < dbPageSize) {
          int mask = begin[offs];
          if (holeBitSize + firstHoleSize[mask] >= objBitSize) {
            pos =
                ((offs_t(i - dbBitmapId) * dbPageSize + offs) * 8 - holeBitSize)
                << dbAllocationQuantumBits;
            if (wasReserved(pos, size)) {
              startOffs   = offs += (objBitSize + 7) >> 3;
              holeBitSize = 0;
              continue;
            }
            reserveLocation(location, pos, size);
            currRBitmapPage = i;
            currRBitmapOffs = offs;
            extend(pos + size);
            if (oid != 0) {
              offs_t prev   = getPos(oid);
              int    marker = (int)prev & dbFlagsMask;
              pool.copy(pos, prev - marker, size);
              setPos(oid, pos | marker | dbModifiedFlag);
            }
            pool.unfix(begin);
            begin = put(tie, i);
            begin[offs] |= (1 << (objBitSize - holeBitSize)) - 1;
            if (holeBitSize != 0) {
              if (length_t(holeBitSize) > offs * 8) {
                memset(begin, 0xFF, offs);
                holeBitSize -= offs * 8;
                begin = put(tie, --i);
                offs  = dbPageSize;
              }
              while (holeBitSize > pageBits) {
                memset(begin, 0xFF, dbPageSize);
                holeBitSize -= pageBits;
                bitmapPageAvailableSpace[i] = 0;
                begin                       = put(tie, --i);
              }
              while ((holeBitSize -= 8) > 0) {
                begin[--offs] = 0xFF;
              }
              begin[offs - 1] |= ~((1 << -holeBitSize) - 1);
            }
            commitLocation();
            return pos;
          } else if (maxHoleSize[mask] >= objBitSize) {
            int holeBitOffset = maxHoleOffset[mask];
            pos = ((offs_t(i - dbBitmapId) * dbPageSize + offs) * 8 +
                   holeBitOffset)
                  << dbAllocationQuantumBits;
            if (wasReserved(pos, size)) {
              startOffs   = offs += (objBitSize + 7) >> 3;
              holeBitSize = 0;
              continue;
            }
            reserveLocation(location, pos, size);
            currRBitmapPage = i;
            currRBitmapOffs = offs;
            extend(pos + size);
            if (oid != 0) {
              offs_t prev   = getPos(oid);
              int    marker = (int)prev & dbFlagsMask;
              pool.copy(pos, prev - marker, size);
              setPos(oid, pos | marker | dbModifiedFlag);
            }
            pool.unfix(begin);
            begin = put(tie, i);
            begin[offs] |= ((1 << objBitSize) - 1) << holeBitOffset;
            commitLocation();
            return pos;
          }
          offs += 1;
          if (lastHoleSize[mask] == 8) {
            holeBitSize += 8;
          } else {
            holeBitSize = lastHoleSize[mask];
          }
        }
        if (startOffs == 0 && holeBitSize == 0 &&
            spaceNeeded < bitmapPageAvailableSpace[i]) {
          bitmapPageAvailableSpace[i] = spaceNeeded;
        }
        offs = 0;
        pool.unfix(begin);
      }
    }
    if (firstPage == dbBitmapId) {
      if (freeBitmapPage > i) {
        i           = freeBitmapPage;
        holeBitSize = holeBeforeFreePage;
      }
      if (i == dbBitmapId + dbBitmapPages) {
        throwException(dybase_out_of_memory_error, "Out of memory");
      }
      length_t extension = (size > extensionQuantum) ? size : extensionQuantum;
      int      morePages =
          (extension + dbPageSize * (dbAllocationQuantum * 8 - 1) - 1) /
          (dbPageSize * (dbAllocationQuantum * 8 - 1));

      if (length_t(i + morePages) > dbBitmapId + dbBitmapPages) {
        morePages = (size + dbPageSize * (dbAllocationQuantum * 8 - 1) - 1) /
                    (dbPageSize * (dbAllocationQuantum * 8 - 1));
        if (length_t(i + morePages) > dbBitmapId + dbBitmapPages) {
          throwException(dybase_out_of_memory_error, "Out of memory");
        }
      }
      objBitSize -= holeBitSize;
      int skip = DOALIGN(objBitSize, dbPageSize / dbAllocationQuantum);
      pos      = (offs_t(i - dbBitmapId)
             << (dbPageBits + dbAllocationQuantumBits + 3)) +
            (skip << dbAllocationQuantumBits);
      extend(pos + morePages * dbPageSize);
      length_t len = objBitSize >> 3;
      offs_t   adr = pos;
      byte *   p;
      while (len >= dbPageSize) {
        p = pool.put(adr);
        memset(p, 0xFF, dbPageSize);
        pool.unfix(p);
        adr += dbPageSize;
        len -= dbPageSize;
      }
      p = pool.put(adr);
      memset(p, 0xFF, len);
      p[len] = (1 << (objBitSize & 7)) - 1;
      pool.unfix(p);
      adr = pos + (skip >> 3);
      len = morePages * (dbPageSize / dbAllocationQuantum / 8);
      for (;;) {
        int off = (int)adr & (dbPageSize - 1);
        p       = pool.put(adr - off);
        if (dbPageSize - off >= len) {
          memset(p + off, 0xFF, len);
          pool.unfix(p);
          break;
        } else {
          memset(p + off, 0xFF, dbPageSize - off);
          pool.unfix(p);
          adr += dbPageSize - off;
          len -= dbPageSize - off;
        }
      }
      oid_t j = i;
      while (--morePages >= 0) {
        dirtyPagesMap[length_t(j / dbHandlesPerPage / 32)] |=
            1 << int(j / dbHandlesPerPage & 31);
        setPos(j++, pos | dbPageObjectFlag | dbModifiedFlag);
        pos += dbPageSize;
      }
      freeBitmapPage = header->root[1 - curr].bitmapEnd = j;
      j = i + objBitSize / pageBits;
      if (alignment != 0) {
        currRBitmapPage = j;
        currRBitmapOffs = 0;
      } else {
        currPBitmapPage = j;
        currPBitmapOffs = 0;
      }
      while (j > i) {
        bitmapPageAvailableSpace[length_t(--j)] = 0;
      }

      pos = (offs_t(i - dbBitmapId) * dbPageSize * 8 - holeBitSize)
            << dbAllocationQuantumBits;
      if (oid != 0) {
        offs_t prev   = getPos(oid);
        int    marker = (int)prev & dbFlagsMask;
        pool.copy(pos, prev - marker, size);
        setPos(oid, pos | marker | dbModifiedFlag);
      }
      if (holeBitSize != 0) {
        reserveLocation(location, pos, size);
        while (holeBitSize > pageBits) {
          holeBitSize -= pageBits;
          byte *p = put(tie, --i);
          memset(p, 0xFF, dbPageSize);
          bitmapPageAvailableSpace[i] = 0;
        }
        byte *cur = (byte *)put(tie, --i) + dbPageSize;
        while ((holeBitSize -= 8) > 0) {
          *--cur = 0xFF;
        }
        *(cur - 1) |= ~((1 << -holeBitSize) - 1);
        commitLocation();
      }
      return pos;
    }
    if (gcThreshold != 0 && !gcDone) {
      allocatedDelta -= size;
      startGC();
      currRBitmapPage = currPBitmapPage = dbBitmapId;
      currRBitmapOffs = currPBitmapOffs = 0;
      return allocate(size, oid);
    }
    freeBitmapPage     = i;
    holeBeforeFreePage = holeBitSize;
    holeBitSize        = 0;
    lastPage           = firstPage + 1;
    firstPage          = dbBitmapId;
    offs               = 0;
  }
}

void dbDatabase::free(offs_t pos, length_t size) {
  assert(pos != 0 && (pos & (dbAllocationQuantum - 1)) == 0);
  dbPutTie tie;
  offs_t   quantNo    = pos / dbAllocationQuantum;
  int      objBitSize = (size + dbAllocationQuantum - 1) / dbAllocationQuantum;
  oid_t    pageId     = dbBitmapId + oid_t(quantNo / (dbPageSize * 8));
  length_t offs       = (length_t(quantNo) & (dbPageSize * 8 - 1)) >> 3;
  byte *   p          = put(tie, pageId) + offs;
  int      bitOffs    = int(quantNo) & 7;

  allocatedDelta -= objBitSize * dbAllocationQuantum;

  if ((length_t(pos) & (dbPageSize - 1)) == 0 && size >= dbPageSize) {
    if (pageId == currPBitmapPage && offs < currPBitmapOffs) {
      currPBitmapOffs = offs;
    }
  } else {
    if (pageId == currRBitmapPage && offs < currRBitmapOffs) {
      currRBitmapOffs = offs;
    }
  }

  bitmapPageAvailableSpace[pageId] = INT_MAX;

  if (objBitSize > 8 - bitOffs) {
    objBitSize -= 8 - bitOffs;
    *p++ &= (1 << bitOffs) - 1;
    offs += 1;
    while (objBitSize + offs * 8 > dbPageSize * 8) {
      memset(p, 0, dbPageSize - offs);
      p                                = put(tie, ++pageId);
      bitmapPageAvailableSpace[pageId] = INT_MAX;
      objBitSize -= (dbPageSize - offs) * 8;
      offs = 0;
    }
    while ((objBitSize -= 8) > 0) {
      *p++ = 0;
    }
    *p &= ~((1 << (objBitSize + 8)) - 1);
  } else {
    *p &= ~(((1 << objBitSize) - 1) << bitOffs);
  }
}

void dbDatabase::gc() {
  dbCriticalSection cs(mutex);
  if (gcDone) { return; }
  startGC();
}

void dbDatabase::startGC() {
  int bitmapSize =
      (int)(header->root[curr].size >> (dbAllocationQuantumBits + 5)) + 1;
  bool   existsNotMarkedObjects;
  offs_t pos;
  int    i, j;

  // mark
  greyBitmap  = new db_int4[bitmapSize];
  blackBitmap = new db_int4[bitmapSize];
  memset(greyBitmap, 0, bitmapSize * sizeof(db_int4));
  memset(blackBitmap, 0, bitmapSize * sizeof(db_int4));
  int rootOid = header->root[curr].rootObject;
  if (rootOid != 0) {
    dbGetTie tie;
    markOid(rootOid);
    do {
      existsNotMarkedObjects = false;
      for (i = 0; i < bitmapSize; i++) {
        if (greyBitmap[i] != 0) {
          existsNotMarkedObjects = true;
          for (j = 0; j < 32; j++) {
            if ((greyBitmap[i] & (1 << j)) != 0) {
              pos = (((offs_t)i << 5) + j) << dbAllocationQuantumBits;
              greyBitmap[i] &= ~(1 << j);
              blackBitmap[i] |= 1 << j;
              int       offs = (int)pos & (dbPageSize - 1);
              byte *    pg   = pool.get(pos - offs);
              dbObject *obj  = (dbObject *)(pg + offs);
              if (obj->cid == dbBtreeId) {
                ((dbBtree *)obj)->markTree(this);
              } else if (obj->cid >= dbFirstUserId) {
                markOid(obj->cid);
                tie.set(pool, pos);
                markObject((dbObject *)tie.get());
              }
              pool.unfix(pg);
            }
          }
        }
      }
    } while (existsNotMarkedObjects);
  }

  // sweep
  gcDone = true;
  for (i = dbFirstUserId, j = committedIndexSize; i < j; i++) {
    pos = getGCPos(i);
    if (((int)pos & (dbPageObjectFlag | dbFreeHandleFlag)) == 0) {
      unsigned bit = (unsigned)(pos >> dbAllocationQuantumBits);
      if ((blackBitmap[bit >> 5] & (1 << (bit & 31))) == 0) {
        // object is not accessible
        assert(getPos(i) == pos);
        int       offs = (int)pos & (dbPageSize - 1);
        byte *    pg   = pool.get(pos - offs);
        dbObject *obj  = (dbObject *)(pg + offs);
        if (obj->cid == dbBtreeId) {
          dbBtree::_drop(this, i);
        } else if (obj->cid >= dbFirstUserId) {
          freeId(i);
          cloneBitmap(pos, obj->size);
        }
        pool.unfix(pg);
      }
    }
  }

  delete[] greyBitmap;
  delete[] blackBitmap;
  allocatedDelta = 0;
}

void dbDatabase::markObject(dbObject *obj) {
  byte *p   = (byte *)(obj + 1);
  byte *end = (byte *)obj + obj->size;
  while (p < end) {
    p = markField(p);
  }
}

byte *dbDatabase::markField(byte *p) {
  int     type = *p++;
  db_int4 len;
  oid_t   oid;
  int     i;

  switch (type & 0xF) {
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type:
    memcpy(&oid, p, sizeof(oid_t));
    markOid(oid);
    p += sizeof(oid_t);
    break;
  case dybase_bool_type: p += 1; break;
  case dybase_int_type: p += sizeof(db_int4); break;
  case dybase_date_type:
  case dybase_long_type:
  case dybase_real_type: p += sizeof(db_int8); break;
  case dybase_chars_type:
    if (type != dybase_chars_type) {
      // small string
      p += type >> 4;
    } else {
      memcpy(&len, p, sizeof(db_int4));
      p += sizeof(db_int4) + len;
    }
    break;
  case dybase_bytes_type:
    if (type != dybase_bytes_type) {
      // small blob
      p += type >> 4;
    }
    else {
      memcpy(&len, p, sizeof(db_int4));
      p += sizeof(db_int4) + len;
    }
    break;
  case dybase_array_type:
    if (type != dybase_array_type) {
      // small array
      for (i = type >> 4; --i >= 0;) {
        p = markField(p);
      }
    } else {
      memcpy(&len, p, sizeof(db_int4));
      p += sizeof(db_int4);
      for (i = len; --i >= 0;) {
        p = markField(p);
      }
    }
    break;
  case dybase_map_type:
    if (type != dybase_map_type) {
      // small map
      for (i = (type >> 4) << 1; --i >= 0;) {
        p = markField(p);
      }
    } else {
      memcpy(&len, p, sizeof(db_int4));
      p += sizeof(db_int4);
      for (i = len << 1; --i >= 0;) {
        p = markField(p);
      }
    }
  }
  return p;
}

void dbDatabase::cloneBitmap(offs_t pos, length_t size) {
  offs_t   quantNo    = pos / dbAllocationQuantum;
  int      objBitSize = (size + dbAllocationQuantum - 1) / dbAllocationQuantum;
  oid_t    pageId     = dbBitmapId + oid_t(quantNo / (dbPageSize * 8));
  length_t offs       = (length_t(quantNo) & (dbPageSize * 8 - 1)) >> 3;
  int      bitOffs    = int(quantNo) & 7;
  oid_t    oid        = pageId;
  pos                 = getPos(oid);
  if (!(pos & dbModifiedFlag)) {
    dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
        1 << (int(oid / dbHandlesPerPage) & 31);
    allocate(dbPageSize, oid);
    cloneBitmap(pos & ~dbFlagsMask, dbPageSize);
  }

  if (objBitSize > 8 - bitOffs) {
    objBitSize -= 8 - bitOffs;
    offs += 1;
    while (objBitSize + offs * 8 > dbPageSize * 8) {
      oid = ++pageId;
      pos = getPos(oid);
      if (!(pos & dbModifiedFlag)) {
        dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
            1 << (int(oid / dbHandlesPerPage) & 31);
        allocate(dbPageSize, oid);
        cloneBitmap(pos & ~dbFlagsMask, dbPageSize);
      }
      objBitSize -= (dbPageSize - offs) * 8;
      offs = 0;
    }
  }
}

oid_t dbDatabase::allocate() {
  dbCriticalSection cs(mutex);
  return allocateId();
}

oid_t dbDatabase::allocateId() {
  oid_t oid;
  int   curr = 1 - this->curr;
  setDirty();
  if ((oid = header->root[curr].freeList) != 0) {
    header->root[curr].freeList = oid_t(getPos(oid) >> dbFlagsBits);
    dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
        1 << (int(oid / dbHandlesPerPage) & 31);
  } else {
    if (currIndexSize + 1 > header->root[curr].indexSize) {
      length_t oldIndexSize = header->root[curr].indexSize;
      length_t newIndexSize = oldIndexSize * 2;
      while (newIndexSize < oldIndexSize + 1) {
        newIndexSize = newIndexSize * 2;
      }
      TRACE_MSG(
          ("Extend index size from %ld to %ld\n", oldIndexSize, newIndexSize));
      offs_t newIndex = allocate(newIndexSize * sizeof(offs_t));
      offs_t oldIndex = header->root[curr].index;
      pool.copy(newIndex, oldIndex, currIndexSize * sizeof(offs_t));
      header->root[curr].index     = newIndex;
      header->root[curr].indexSize = newIndexSize;
      free(oldIndex, oldIndexSize * sizeof(offs_t));
    }
    oid                          = currIndexSize;
    header->root[curr].indexUsed = ++currIndexSize;
  }
  setPos(oid, 0);
  return oid;
}

void dbDatabase::freeId(oid_t oid) {
  dirtyPagesMap[length_t(oid / dbHandlesPerPage / 32)] |=
      1 << (int(oid / dbHandlesPerPage) & 31);
  setPos(oid, (offs_t(header->root[1 - curr].freeList) << dbFlagsBits) |
                  dbFreeHandleFlag);
  header->root[1 - curr].freeList = oid;
}

void dbDatabase::commit() {
  dbCriticalSection cs(mutex);
  commitTransaction();
}

void dbDatabase::commitTransaction() {
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return;
  }
  if (!modified) { return; }
  //
  // Commit transaction
  //
  int      rc;
  int      n, curr = header->curr;
  oid_t    i;
  db_int4 *map                = dirtyPagesMap;
  length_t currIndexSize      = this->currIndexSize;
  length_t committedIndexSize = this->committedIndexSize;
  length_t oldIndexSize       = header->root[curr].indexSize;
  length_t newIndexSize       = header->root[1 - curr].indexSize;
  length_t nPages             = committedIndexSize / dbHandlesPerPage;
  if (newIndexSize > oldIndexSize) {
    offs_t newIndex = allocate(newIndexSize * sizeof(offs_t));
    header->root[1 - curr].shadowIndex     = newIndex;
    header->root[1 - curr].shadowIndexSize = newIndexSize;
    cloneBitmap(header->root[curr].index, oldIndexSize * sizeof(offs_t));
    free(header->root[curr].index, oldIndexSize * sizeof(offs_t));
  }

  for (i = 0; i < nPages; i++) {
    if (map[length_t(i >> 5)] & (1 << int(i & 31))) {
      offs_t *srcIndex =
          (offs_t *)pool.get(header->root[1 - curr].index + i * dbPageSize);
      offs_t *dstIndex =
          (offs_t *)pool.get(header->root[curr].index + i * dbPageSize);
      for (length_t j = 0; j < dbHandlesPerPage; j++) {
        offs_t pos = dstIndex[j];
        if (srcIndex[j] != pos) {
          if (!(pos & dbFreeHandleFlag)) {
            if (pos & dbPageObjectFlag) {
              free(pos & ~dbFlagsMask, dbPageSize);
            } else {
              int       offs = (int)pos & (dbPageSize - 1);
              dbObject *rec =
                  (dbObject *)(pool.get(pos - offs) + (offs & ~dbFlagsMask));
              free(pos, rec->size);
              pool.unfix(rec);
            }
          }
        }
      }
      pool.unfix(srcIndex);
      pool.unfix(dstIndex);
    }
  }
  if ((committedIndexSize % dbHandlesPerPage) != 0 &&
      (map[length_t(i >> 5)] & (1 << int(i & 31)))) {
    offs_t *srcIndex =
        (offs_t *)pool.get(header->root[1 - curr].index + i * dbPageSize);
    offs_t *dstIndex =
        (offs_t *)pool.get(header->root[curr].index + i * dbPageSize);
    n = committedIndexSize % dbHandlesPerPage;
    do {
      offs_t pos = *dstIndex;
      if (*srcIndex != pos) {
        if (!(pos & dbFreeHandleFlag)) {
          if (pos & dbPageObjectFlag) {
            free(pos & ~dbFlagsMask, dbPageSize);
          } else {
            int       offs = (int)pos & (dbPageSize - 1);
            dbObject *rec =
                (dbObject *)(pool.get(pos - offs) + (offs & ~dbFlagsMask));
            free(pos, rec->size);
            pool.unfix(rec);
          }
        }
      }
      dstIndex += 1;
      srcIndex += 1;
    } while (--n != 0);

    pool.unfix(srcIndex);
    pool.unfix(dstIndex);
  }

  for (i = 0; i <= nPages; i++) {
    if (map[length_t(i >> 5)] & (1 << int(i & 31))) {
      offs_t *p =
          (offs_t *)pool.put(header->root[1 - curr].index + i * dbPageSize);
      for (length_t j = 0; j < dbHandlesPerPage; j++) {
        p[j] &= ~dbModifiedFlag;
      }
      pool.unfix(p);
    }
  }
  if (currIndexSize > committedIndexSize) {
    offs_t page =
        (header->root[1 - curr].index + committedIndexSize * sizeof(offs_t)) &
        ~((offs_t)dbPageSize - 1);
    offs_t end = (header->root[1 - curr].index + dbPageSize - 1 +
                  currIndexSize * sizeof(offs_t)) &
                 ~((offs_t)dbPageSize - 1);
    while (page < end) {
      offs_t *p = (offs_t *)pool.put(page);
      for (length_t h = 0; h < dbHandlesPerPage; h++) {
        p[h] &= ~dbModifiedFlag;
      }
      pool.unfix(p);
      page += dbPageSize;
    }
  }

  if ((rc = file->write(0, header, dbPageSize)) != dbFile::ok) {
    throwException(dybase_file_error, "Failed to write header");
  }

  pool.flush();

  header->curr = curr ^= 1;

  if ((rc = file->write(0, header, dbPageSize)) != dbFile::ok ||
      (rc = file->flush()) != dbFile::ok) {
    throwException(dybase_file_error, "Failed to flush changes to the disk");
  }

  header->root[1 - curr].size          = header->root[curr].size;
  header->root[1 - curr].indexUsed     = currIndexSize;
  header->root[1 - curr].freeList      = header->root[curr].freeList;
  header->root[1 - curr].bitmapEnd     = header->root[curr].bitmapEnd;
  header->root[1 - curr].rootObject    = header->root[curr].rootObject;
  header->root[1 - curr].classDescList = header->root[curr].classDescList;

  if (newIndexSize != oldIndexSize) {
    header->root[1 - curr].index           = header->root[curr].shadowIndex;
    header->root[1 - curr].indexSize       = header->root[curr].shadowIndexSize;
    header->root[1 - curr].shadowIndex     = header->root[curr].index;
    header->root[1 - curr].shadowIndexSize = header->root[curr].indexSize;
    pool.copy(header->root[1 - curr].index, header->root[curr].index,
              currIndexSize * sizeof(offs_t));
    memset(map, 0,
           4 * ((currIndexSize + dbHandlesPerPage * 32 - 1) /
                (dbHandlesPerPage * 32)));
  } else {
    for (i = 0; i < nPages; i++) {
      if (map[length_t(i >> 5)] & (1 << int(i & 31))) {
        map[length_t(i >> 5)] -= (1 << int(i & 31));
        pool.copy(header->root[1 - curr].index + i * dbPageSize,
                  header->root[curr].index + i * dbPageSize, dbPageSize);
      }
    }
    if (currIndexSize > i * dbHandlesPerPage &&
        ((map[length_t(i >> 5)] & (1 << int(i & 31))) != 0 ||
         currIndexSize != committedIndexSize)) {
      pool.copy(header->root[1 - curr].index + i * dbPageSize,
                header->root[curr].index + i * dbPageSize,
                length_t(sizeof(offs_t) * currIndexSize - i * dbPageSize));
      memset(map + length_t(i >> 5), 0,
             length_t(((currIndexSize + dbHandlesPerPage * 32 - 1) /
                           (dbHandlesPerPage * 32) -
                       (i >> 5)) *
                      4));
    }
  }
  this->curr               = curr;
  this->committedIndexSize = currIndexSize;
  modified                 = false;
  gcDone                   = false;
}

void dbDatabase::rollback() {
  dbCriticalSection cs(mutex);
  if (!opened) {
    handleError(dybase_not_opened, "Database not opened");
    return;
  }
  if (!modified) { return; }
  int      curr = header->curr;
  length_t nPages =
      (committedIndexSize + dbHandlesPerPage - 1) / dbHandlesPerPage;
  db_int4 *map = dirtyPagesMap;
  if (header->root[1 - curr].index != header->root[curr].shadowIndex) {
    pool.copy(header->root[curr].shadowIndex, header->root[curr].index,
              dbPageSize * nPages);
  } else {
    for (oid_t i = 0; i < nPages; i++) {
      if (map[length_t(i >> 5)] & (1 << int(i & 31))) {
        pool.copy(header->root[curr].shadowIndex + i * dbPageSize,
                  header->root[curr].index + i * dbPageSize, dbPageSize);
      }
    }
  }
  memset(map, 0,
         length_t((currIndexSize + dbHandlesPerPage * 32 - 1) /
                  (dbHandlesPerPage * 32)) *
             4);
  header->root[1 - curr].indexSize     = header->root[curr].shadowIndexSize;
  header->root[1 - curr].indexUsed     = header->root[curr].indexUsed;
  header->root[1 - curr].freeList      = header->root[curr].freeList;
  header->root[1 - curr].index         = header->root[curr].shadowIndex;
  header->root[1 - curr].bitmapEnd     = header->root[curr].bitmapEnd;
  header->root[1 - curr].size          = header->root[curr].size;
  header->root[1 - curr].rootObject    = header->root[curr].rootObject;
  header->root[1 - curr].classDescList = header->root[curr].classDescList;

  currRBitmapPage = currPBitmapPage = dbBitmapId;
  currRBitmapOffs = currPBitmapOffs = 0;
  currIndexSize                     = committedIndexSize;

  modified = false;

  oid_t              cid  = header->root[curr].classDescList;
  dbClassDescriptor *desc = classDescList;
  while (desc->oid != cid) {
    classOidHash.remove(&desc->oid, sizeof(desc->oid));
    classSignatureHash.remove(desc->cls->signature, desc->signatureSize);
    dbClassDescriptor *next = desc->next;
    delete desc;
    desc = next;
  }
  classDescList = desc;
}

dbDatabase::dbDatabase(dbAccessType type, dbErrorHandler hnd, length_t poolSize,
                       length_t dbExtensionQuantum, length_t dbInitIndexSize)
    : accessType(type), extensionQuantum(dbExtensionQuantum),
      initIndexSize(dbInitIndexSize), pool(this, poolSize), errorHandler(hnd) {
  dirtyPagesMap            = new db_int4[dbDirtyPageBitmapSize / 4 + 1];
  bitmapPageAvailableSpace = new int[dbBitmapId + dbBitmapPages];
  classDescList            = NULL;
  opened                   = false;
  header                   = (dbHeader *)dbFile::allocateBuffer(dbPageSize);
  dbFileExtensionQuantum   = 0;
  dbFileSizeLimit          = 0;
}

dbDatabase::~dbDatabase() {
  delete[] dirtyPagesMap;
  delete[] bitmapPageAvailableSpace;
  dbFile::deallocateBuffer(header);
}

void dbTrace(char *message, ...) {
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
}
