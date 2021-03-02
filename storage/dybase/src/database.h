//-< DATABASE.H >----------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:     20-Nov-98    K.A. Knizhnik  * / [] \ *
//                          Last update: 14-Feb-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// Database management
//-------------------------------------------------------------------*--------*

#ifndef __DATABASE_H__
#define __DATABASE_H__

#include "dybase.h"
#include "stdtp.h"

#define _LARGEFILE64_SOURCE 1 // access to files greater than 2Gb in Solaris
#define _LARGE_FILE_API 1     // access to files greater than 2Gb in AIX

#ifndef dbDatabaseOffsetBits
#define dbDatabaseOffsetBits 32 // 37 - up to 1 terabyte
#endif

typedef dybase_oid_t oid_t;
#define dbDatabaseOidBits                                                      \
  sizeof(oid_t) * 8 // can be greater than 32 only at 64-bit platforms

/**
 * Object offset in the file type
 */
#if dbDatabaseOffsetBits > 32
typedef db_nat8 offs_t;   // It will work only for 64-bit OS
typedef db_nat8 length_t; // It will work only for 64-bit OS
#define lengthof(T) sizeof(T)
#else
typedef db_nat4 offs_t;
typedef db_nat4 length_t;
#define lengthof(T) (length_t(sizeof(T)))
#endif

#include "buffer.h"
#include "pagepool.h"
#include "hashtab.h"
#include "sync.h"

/**
 * Default size of memory mapping object for the database (bytes)
 */
const length_t dbDefaultInitIndexSize =
    10 * 1024; // typical nr. of objects in db

/**
 * Default initial index size (number of objects)
 */
const length_t dbDefaultExtensionQuantum = 512 * 1024; // alloc per half meg.

/**
 * Default initial index size (in bytes)
 */
const length_t dbDefaultPagePoolSize = 8 * 1024 * 1024;

/**
 * Object handler falgs
 */
enum dbHandleFlags {
  dbPageObjectFlag = 0x1,
  dbModifiedFlag   = 0x2,
  dbFreeHandleFlag = 0x4,
  dbFlagsMask      = 0x7,
  dbFlagsBits      = 3
};

const length_t dbAllocationQuantumBits = 5;
const length_t dbAllocationQuantum     = 1 << dbAllocationQuantumBits;
const length_t dbPageBits              = 12;
const length_t dbPageSize              = 1 << dbPageBits;
const length_t dbIdsPerPage            = dbPageSize / sizeof(oid_t);
const length_t dbHandlesPerPage        = dbPageSize / sizeof(offs_t);
const length_t dbBitmapSegmentBits = dbPageBits + 3 + dbAllocationQuantumBits;
const length_t dbBitmapSegmentSize = 1 << dbBitmapSegmentBits;
const length_t dbBitmapPages       = 1
                               << (dbDatabaseOffsetBits - dbBitmapSegmentBits);
const length_t dbDirtyPageBitmapSize =
    1 << (dbDatabaseOidBits - dbPageBits + (1 + sizeof(offs_t) / 4) - 3);

const int dbMaxFileSegments = 64;

/**
 * Predefined object identifiers
 */
enum dbPredefinedIds {
  dbInvalidId,
  dbClassDescId,
  dbBtreeId,
  dbBitmapId,
  dbFirstUserId = dbBitmapId + dbBitmapPages
};

/*
  dybase_object_ref_type = 0, // object ref, oid
  dybase_array_ref_type  = 1,
  dybase_index_ref_type  = 2,

  dybase_bool_type   = 3,
  dybase_int_type    = 4,
  dybase_date_type   = 5,
  dybase_real_type   = 6,
  dybase_chars_type  = 7,  // literal string
  dybase_array_type  = 8,  // literal array
  dybase_map_type    = 9,  // literal key/value pairs map

  dybase_long_type   = 10,
  dybase_bytes_type  = 11,  // literal blob, max length 2^32
*/

static const int dbSizeofType[] = {
    sizeof(oid_t),    // dybase_object_type
    sizeof(oid_t),    // dybase_object_type
    sizeof(oid_t),    // dybase_object_type
    sizeof(db_int1),  // dybase_bool_type
    sizeof(db_int4),  // dybase_int_type
    sizeof(db_int8),  // dybase_date_type
    sizeof(db_real8), // dybase_real_type
    0, // dybase_chars_type
    0, // dybase_array_type
    0, // dybase_map_type
    sizeof(db_int8),  // dybase_long_type
    0,  // dybase_bytes_type
};

//static const int dbFieldTypeBits = 3;

class dbException {
  char const *msg;
  int         error;

public:
  /**
   * Get error code
   * @return error code as defined in <code>dbErrorClass</code> enum in
   * database.h
   */
  int getErrCode() const { return error; }

  /**
   * Get message text
   */
  char *getMsg() const { return (char *)msg; }

  dbException(int error, char const *msg) {
    this->error = error;
    this->msg   = msg;
  }
};

/**
 * Database header
 */
class dbHeader {
public:
  db_int4 curr;        // current root
  db_int4 dirty;       // database was not closed normally
  db_int4 initialized; // database is initilaized
  struct {
    offs_t size;            // database file size
    offs_t index;           // offset to object index
    offs_t shadowIndex;     // offset to shadow index
    oid_t  indexSize;       // size of object index
    oid_t  shadowIndexSize; // size of object index
    oid_t  indexUsed;       // used part of the index
    oid_t  freeList;        // L1 list of free descriptors
    oid_t  bitmapEnd;       // index of last allocated bitmap page
    oid_t  rootObject;      // storage root
    oid_t  classDescList;   // list of class descriptors
  } root[2];

  bool isInitialized() {
    return initialized == 1 && (dirty == 1 || dirty == 0) &&
           (curr == 1 || curr == 0) && root[curr].size > root[curr].index &&
           root[curr].size > root[curr].shadowIndex &&
           root[curr].size > root[curr].indexSize * sizeof(offs_t) +
                                 root[curr].shadowIndexSize * sizeof(offs_t) &&
           root[curr].indexSize >= root[curr].indexUsed &&
           root[curr].indexUsed >= dbFirstUserId &&
           root[curr].bitmapEnd > dbBitmapId;
  }
};

class dbObject {
public:
  oid_t   cid;
  db_nat4 size;
};

class dbClass : public dbObject {
public:
  oid_t next;
  char  signature[1];

  static dbClass *create(char *signature, length_t signatureSize) {
    length_t size = sizeof(dbObject) + sizeof(oid_t) + signatureSize;
    dbClass *cls  = (dbClass *)new char[size];
    cls->size     = (db_nat4)size;
    cls->cid      = dbClassDescId;
    memcpy(cls->signature, signature, signatureSize);
    return cls;
  }

  dbClass *clone() {
    dbClass *cls = create(signature, getSignatureSize());
    cls->size    = size;
    cls->next    = next;
    cls->cid     = cid;
    return cls;
  }

  void remove() { delete[](char *) this; }

  length_t getSignatureSize() {
    return size - sizeof(dbObject) - sizeof(oid_t);
  }
};

class dbClassDescriptor {

  dbClassDescriptor(const dbClassDescriptor &);
  dbClassDescriptor &operator=(const dbClassDescriptor &);

public:
  oid_t              oid;
  dbClass *          cls;
  char *             name;
  char **            field;
  length_t           signatureSize;
  dbClassDescriptor *next;

  dbClassDescriptor(dbClass *cls, oid_t oid) {
    char *p;
    this->oid     = oid;
    this->cls     = cls;
    name          = cls->signature;
    signatureSize = cls->getSignatureSize();
    char *start   = name + strlen(name) + 1;
    char *end     = name + signatureSize;
    int   n       = 0;
    for (p = start; p < end; p += strlen(p) + 1) {
      n += 1;
    }
    field = new char *[n];
    for (p = start, n = 0; p < end; p += strlen(p) + 1) {
      field[n++] = p;
    }
  }

  ~dbClassDescriptor() {
    cls->remove();
    delete[] field;
  }
};

class dbLoadHandle {
  friend class dbDatabase;

private:
  dbGetTie tie;
  byte *   curr;
  byte *   end;
  union {
    byte     bval;
    oid_t    oval;
    db_int8  lval;
    db_int4  ival;
    db_real8 dval;
  } u;
  unsigned           type;
  int                fieldNo;
  void *             ptr;
  dbClassDescriptor *desc;

public:
  dbLoadHandle() { fieldNo = -1; }

  char *getClassName() { return desc->name; }

  char *getFieldName() { return desc->field[fieldNo]; }

  bool hasNextField() {
    bool success = hasNext();
    if (success) { fieldNo += 1; }
    return success;
  }

  bool hasNext() {
    if (curr == end) { return false; }
    ptr  = (byte *)&u;
    type = *curr++;
    switch (type & 0xF) {
    case dybase_object_ref_type:
    case dybase_array_ref_type:
    case dybase_index_ref_type:
      memcpy(&u, curr, sizeof(oid_t));
      curr += sizeof(oid_t);
      break;
    case dybase_bool_type: u.bval = *curr++; break;
    case dybase_int_type:
      memcpy(&u, curr, sizeof(db_int4));
      curr += sizeof(db_int4);
      break;
    case dybase_date_type:
    case dybase_long_type:
    case dybase_real_type:
      memcpy(&u, curr, sizeof(db_int8));
      curr += sizeof(db_int8);
      break;
    case dybase_chars_type:
      if (type != dybase_chars_type) {
        // small string
        u.ival = type >> 4;
        type   = dybase_chars_type;
        ptr    = curr;
        curr += u.ival;
      } else {
        memcpy(&u.ival, curr, sizeof(db_int4));
        curr += sizeof(db_int4);
        ptr = curr;
        curr += u.ival;
      }
      break;
    case dybase_bytes_type:
      if (type != dybase_bytes_type) {
        // small string
        u.ival = type >> 4;
        type = dybase_bytes_type;
        ptr = curr;
        curr += u.ival;
      }
      else {
        memcpy(&u.ival, curr, sizeof(db_int4));
        curr += sizeof(db_int4);
        ptr = curr;
        curr += u.ival;
      }
      break;
    case dybase_array_type:
      if (type != dybase_array_type) {
        // small array
        u.ival = type >> 4;
        type   = dybase_array_type;
      } else {
        memcpy(&u.ival, curr, sizeof(db_int4));
        curr += sizeof(db_int4);
      }
      break;
    case dybase_map_type:
      if (type != dybase_map_type) {
        // small array
        u.ival = type >> 4;
        type   = dybase_map_type;
      } else {
        memcpy(&u.ival, curr, sizeof(db_int4));
        curr += sizeof(db_int4);
      }
      break;
    }
    return true;
  }

  int getType() { return type; }

  void *getValue() { return ptr; }

  int getLength() { return u.ival; }
};

class dbStoreHandle {
private:
  friend class dbDatabase;
  dbSmallBuffer<char, 256> signature;
  dbSmallBuffer<char, 128> body;
  oid_t                    oid;

public:
  dbDatabase *db;

  dbStoreHandle(dbDatabase *db, oid_t oid, char const *className) {
    this->db  = db;
    this->oid = oid;
    body.append(sizeof(dbObject));
    strcpy(signature.append((int)strlen(className) + 1), className);
  }

  void setFieldValue(char const *fieldName, int type, void *value, int length) {
    strcpy(signature.append((int)strlen(fieldName) + 1), fieldName);
    setElement(type, value, length);
  }

  void setElement(int type, void *value, int length) {
    switch (type) {
    case dybase_object_ref_type:
    case dybase_array_ref_type:
    case dybase_index_ref_type:
      *body.append(1) = char(type);
      memcpy(body.append(sizeof(oid_t)), value, sizeof(oid_t));
      break;
    case dybase_bool_type:
      *body.append(1) = char(type);
      *body.append(1) = *(byte *)value;
      break;
    case dybase_int_type:
      *body.append(1) = char(type);
      memcpy(body.append(sizeof(db_int4)), value, sizeof(db_int4));
      break;
    case dybase_date_type:
    case dybase_long_type:
    case dybase_real_type:
      *body.append(1) = char(type);
      memcpy(body.append(sizeof(db_int8)), value, sizeof(db_int8));
      break;
    case dybase_chars_type:
      if ((unsigned)(length - 1) < 15) {
        *body.append(1) = (byte)(dybase_chars_type | (length << 4));
      } else {
        *body.append(1) = char(type);
        memcpy(body.append(sizeof(db_int4)), &length, sizeof(db_int4));
      }
      memcpy(body.append(length), value, length);
      break;
    case dybase_bytes_type:
      if ((unsigned)(length - 1) < 15) {
        *body.append(1) = (byte)(dybase_bytes_type | (length << 4));
      }
      else {
        *body.append(1) = char(type);
        memcpy(body.append(sizeof(db_int4)), &length, sizeof(db_int4));
      }
      memcpy(body.append(length), value, length);
      break;
    case dybase_array_type:
      if ((unsigned)(length - 1) < 15) {
        *body.append(1) = (byte)(dybase_array_type | (length << 4));
      } else {
        *body.append(1) = char(type);
        memcpy(body.append(sizeof(db_int4)), &length, sizeof(db_int4));
      }
      break;
    case dybase_map_type:
      if ((unsigned)(length - 1) < 15) {
        *body.append(1) = (byte)(dybase_map_type | (length << 4));
      } else {
        *body.append(1) = char(type);
        memcpy(body.append(sizeof(db_int4)), &length, sizeof(db_int4));
      }
      break;
    default: assert(false);
    }
  }
};

/**
 * Database class
 */
class dbDatabase {
  friend class dbBtree;
  friend class dbBtreePage;
  friend class dbBtreeIterator;
  friend class dbBtreeLeafPage;
  friend class dbPagePool;

  friend class dbGetTie;
  friend class dbPutTie;

  dbDatabase(const dbDatabase &);
  dbDatabase &operator=(const dbDatabase &);

public:
  typedef void (*dbErrorHandler)(int error, char const *msg);

  bool open(char const *databaseName, int openAttr = dbFile::no_buffering);
  //bool open(wchar_t const *databaseName, int openAttr = dbFile::no_buffering);

  /**
   * Close database
   */
  void close();

  /**
   * Commit transaction
   */
  void commit();

  /**
   * Rollback transaction
   */
  void rollback();

  /**
   * Allocate object identifier
   * @return allocated object identifier
   */
  oid_t allocate();

  /**
   * Free object
   * @param oif object identifier of deallocated object
   */
  void freeObject(oid_t oid);

  dbStoreHandle *getStoreHandle(oid_t oid, char const *className) {
    return new dbStoreHandle(this, oid, className);
  }

  dbLoadHandle *getLoadHandle(oid_t oid);

  void storeObject(dbStoreHandle *handle);

  oid_t getRoot();

  void setRoot(oid_t oid);

  void setGcThreshold(long maxAllocatedDelta) {
    gcThreshold = maxAllocatedDelta;
  }

  void gc();

  void handleError(int error, char const *msg = NULL);
  void throwException(int error, char const *msg = NULL);

  enum dbAccessType { dbReadOnly = 0, dbAllAccess = 1 };
  const dbAccessType accessType;
  const length_t     extensionQuantum;
  const length_t     initIndexSize;

  dbDatabase(dbAccessType type = dbAllAccess, dbErrorHandler hnd = NULL,
             length_t poolSize           = dbDefaultPagePoolSize,
             length_t dbExtensionQuantum = dbDefaultExtensionQuantum,
             length_t dbInitIndexSize    = dbDefaultInitIndexSize);

  /**
   * Database detructor
   */
  ~dbDatabase();

protected:
  dbHeader *header;        // base address of database file mapping
  db_int4 * dirtyPagesMap; // bitmap of changed pages in current index
  bool      modified;

  int curr; // copy of header->root, used to allow read access to the database
            // during transaction commit

  offs_t dbFileExtensionQuantum;
  offs_t dbFileSizeLimit;

  length_t currRBitmapPage; // current bitmap page for allocating records
  length_t currRBitmapOffs; // offset in current bitmap page for allocating
                            // unaligned records
  length_t currPBitmapPage; // current bitmap page for allocating page objects
  length_t currPBitmapOffs; // offset in current bitmap page for allocating
                            // page objects

  struct dbLocation {
    offs_t      pos;
    length_t    size;
    dbLocation *next;
  };
  dbLocation *reservedChain;

  length_t committedIndexSize;
  length_t currIndexSize;

  dbClassDescriptor *classDescList;

  dbFile *   file;
  dbMutex    mutex;
  dbPagePool pool;

  int *bitmapPageAvailableSpace;
  bool opened;

  dbHashtable classOidHash;
  dbHashtable classSignatureHash;

  db_int4 *greyBitmap;  // bitmap of visited during GC but not yet marked object
  db_int4 *blackBitmap; // bitmap of objects marked during GC
  long     gcThreshold;
  long     allocatedDelta;
  bool     gcDone;

  dbErrorHandler errorHandler;

  /**
   * Get position of the object in the database file
   * @param oid object identifier
   * @param offset of the object in database file
   */
  offs_t getPos(oid_t oid) {
    byte * p   = pool.get(header->root[1 - curr].index +
                       oid / dbHandlesPerPage * dbPageSize);
    offs_t pos = *((offs_t *)p + oid % dbHandlesPerPage);
    pool.unfix(p);
    return pos;
  }

  offs_t getGCPos(oid_t oid) {
    byte * p   = pool.get(header->root[curr].index +
                       oid / dbHandlesPerPage * dbPageSize);
    offs_t pos = *((offs_t *)p + oid % dbHandlesPerPage);
    pool.unfix(p);
    return pos;
  }

  void markOid(oid_t oid) {
    if (oid != 0) {
      offs_t   pos = getGCPos(oid);
      unsigned bit = (unsigned)(pos >> dbAllocationQuantumBits);
      if ((blackBitmap[bit >> 5] & (1 << (bit & 31))) == 0) {
        greyBitmap[bit >> 5] |= 1 << (bit & 31);
      }
    }
  }

  void  markObject(dbObject *obj);
  byte *markField(byte *p);
  void  startGC();

  /**
   * Set position of the object
   * @param  oid object identifier
   * @param pos offset of the object in database file
   */
  void setPos(oid_t oid, offs_t pos) {
    byte *p = pool.put(header->root[1 - curr].index +
                       oid / dbHandlesPerPage * dbPageSize);
    *((offs_t *)p + oid % dbHandlesPerPage) = pos;
    pool.unfix(p);
  }

  /**
   * Get object
   * @param tie get tie used to pin accessed object
   * @param oid object indentifier
   * @return object with this oid
   */
  dbObject *getObject(dbGetTie &tie, oid_t oid) {
    offs_t pos = getPos(oid);
    assert(!(pos & (dbFreeHandleFlag | dbPageObjectFlag)));
    tie.set(pool, pos & ~dbFlagsMask);
    return (dbObject *)tie.get();
  }

  /**
   * Get object header
   * @param rec variable to receive object header
   * @param oid object identifier
   */
  void getHeader(dbObject &rec, oid_t oid) {
    offs_t pos  = getPos(oid);
    int    offs = (int)pos & (dbPageSize - 1);
    byte * p    = pool.get(pos - offs);
    rec         = *(dbObject *)(p + (offs & ~dbFlagsMask));
    pool.unfix(p);
  }

  /**
   * Get pointer to the body of page object which can be used to update this
   * object
   * @param oid page object identifier
   * @return pointer to the pinned object
   */
  byte *put(oid_t oid) {
    offs_t pos  = getPos(oid);
    int    offs = (int)pos & (dbPageSize - 1);
    return pool.put(pos - offs) + (offs & ~dbFlagsMask);
  }

  /**
   * Get readonly pointer to the body of page object
   * @param oid page object identifier
   * @return pointer to the pinned object
   */
  byte *get(oid_t oid) {
    offs_t pos  = getPos(oid);
    int    offs = (int)pos & (dbPageSize - 1);
    return pool.get(pos - offs) + (offs & ~dbFlagsMask);
  }

  /**
   * Get pointer to the record which can be used to uodate this record. Record
   * length is not changed.
   * @param tie put tie used to pin updated object
   * @param oid page object identifier
   * @return  pointer to the pinned object
   */
  dbObject *putObject(dbPutTie &tie, oid_t oid);

  /**
   * Get pointer to the page object which can be used to uodate this object
   * @param oid page object identifier
   * @return  pointer to the page object
   */
  byte *put(dbPutTie &tie, oid_t oid);

  oid_t allocateId();

  offs_t allocate(length_t size, oid_t oid = 0);

  /**
   * Free object
   * @param pos position of the object in database file
   * @param size size of the object
   */
  void free(offs_t pos, length_t size);

  /**
   * Check that allocated object fits in the database file and extend database
   * file if it is not true
   * @param size position of the allocated object + size of the object
   */
  void extend(offs_t size);

  /**
   * Clone bitmap page(s). Thisd method is used to clonepages of the bitmap (if
   * them were not already cloned within this transaction) which will ber
   * affected by free method at the end of transaction.
   * @param pos position of the object whcih will be deallocated
   * @param size size of  the object whcih will be deallocated
   */
  void cloneBitmap(offs_t pos, length_t size);

  /**
   * Release obejct identifier
   * @param oid deallocated object identifier
   */
  void freeId(oid_t oid);

  /**
   * Allocate and write object
   * @return object identifer of new object
   */
  oid_t allocateObject(dbObject *obj);

  /**
   * Allocate page object
   * @return object identifer of page object
   */
  oid_t allocatePage() {
    oid_t oid = allocateId();
    setPos(oid, allocate(dbPageSize) | dbPageObjectFlag | dbModifiedFlag);
    return oid;
  }

  /**
   * Deallocate page object
   * @param oid object identifer of page object
   */
  void freePage(oid_t oid);

  /**
   * Load database schema. This method loads table decriptors from database,
   * compare them with application classes, do necessary reformatting and save
   * update andnew table decriptor in database
   * @return <code>true</code> if schema was successfully loaded
   */
  void loadScheme();

  /**
   * Check if location is reserved
   * @param pos start position of the location
   * @param size location size
   * @return true id location was reserved
   */
  bool wasReserved(offs_t pos, length_t size);

  /**
   * Mark location as reserved. This method is used by allocator to protect hole
   * located in memory allocation bitmap, from been used by recursuve call of
   * allocator (needed to clone bitmap pages).
   * @param location [out] local structure describing location.
   * @param pos start position of the location
   * @param size location size
   */
  void reserveLocation(dbLocation &location, offs_t pos, length_t size);

  /**
   * Remove location from list of reserved locations. It is done after location
   * is marked as occupied in bitmap.
   */
  void commitLocation();

  void commitTransaction();
  void setDirty();
};

#endif
