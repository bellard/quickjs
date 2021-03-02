//-< BTREE.CPP >-----------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:      1-Jan-99    K.A. Knizhnik  * / [] \ *
//                          Last update: 25-Oct-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// B-Tree interface
//-------------------------------------------------------------------*--------*

#ifndef __BTREE_H__
#define __BTREE_H__

#include "buffer.h"

class dbSearchContext {
public:
  void *          low;
  length_t        lowSize;
  int             lowInclusive;
  void *          high;
  length_t        highSize;
  int             highInclusive;
  int             keyType;
  dbBuffer<oid_t> selection;
};

class dbBtreePage {
public:
  db_nat4 nItems;
  db_nat4 size;

  struct str {
    oid_t   oid;
    db_nat2 size;
    db_nat2 offs;
  };

  enum { dbMaxKeyLen = (dbPageSize - sizeof(str) * 2) / sizeof(char) / 2 };

  struct item {
    oid_t oid;
    int   keyLen;
    union {
      db_int1  boolKey;
      db_int4  intKey;
      db_int8  longKey;
      oid_t    refKey;
      db_real8 realKey;
      char     charKey[dbMaxKeyLen];
    };
  };
  enum { maxItems = (dbPageSize - 8) / sizeof(oid_t) };

  union {
    oid_t    record[maxItems];
    oid_t    refKey[(dbPageSize - 8) / sizeof(oid_t)];
    db_int4  intKey[(dbPageSize - 8) / sizeof(db_int4)];
    db_int8  longKey[(dbPageSize - 8) / sizeof(db_int8)];
    db_real8 realKey[(dbPageSize - 8) / sizeof(db_real8)];
    db_int1  boolKey[dbPageSize - 8];
    char     charKey[dbPageSize - 8];
    str      strKey[1];
  };

  static oid_t allocate(dbDatabase *db, oid_t root, int type, item &ins);

  static int insert(dbDatabase *db, oid_t pageId, int type, item &ins,
                    bool unique, bool replace, int height);
  static int remove(dbDatabase *db, oid_t pageId, int type, item &rem,
                    int height);

  static void markPage(dbDatabase *db, oid_t pageId, int type, int height);

  static void purge(dbDatabase *db, oid_t pageId, int type, int height);

  int  insertStrKey(dbDatabase *db, int r, item &ins, int height);
  int  replaceStrKey(dbDatabase *db, int r, item &ins, int height);
  int  removeStrKey(int r);
  void compactify(int m);

  int handlePageUnderflow(dbDatabase *db, int r, int type, item &rem,
                          int height);

  bool find(dbDatabase *db, dbSearchContext &sc, int height);
};

class dbBtree : public dbObject {
  friend class dbDatabase;
  friend class dbBtreeIterator;

protected:
  oid_t   root;
  db_int4 height;
  db_int4 type;
  db_int4 flags;
  db_int4 unique;

  static bool packItem(dbDatabase *db, dbBtree *tree, dbBtreePage::item &it,
                       void *key, int keyType, length_t keySize, oid_t oid);

  static void _drop(dbDatabase *db, oid_t treeId);
  static void _clear(dbDatabase *db, oid_t treeId);

public:
  enum OperationEffect { done, overflow, underflow, duplicate, not_found };

  static oid_t allocate(dbDatabase *db, int type, bool unique);
  static void  find(dbDatabase *db, oid_t treeId, dbSearchContext &sc);
  static bool  insert(dbDatabase *db, oid_t treeId, void *key, int keyType,
                      length_t keySize, oid_t oid, bool replace);
  static bool  remove(dbDatabase *db, oid_t treeId, void *key, int keyType,
                      length_t keySize, oid_t oid);
  static void  drop(dbDatabase *db, oid_t treeId);
  static void  clear(dbDatabase *db, oid_t treeId);
  static bool  is_unique(dbDatabase *db, oid_t treeId);
  static int   get_type(dbDatabase *db, oid_t treeId);

  void markTree(dbDatabase *db) {
    if (root != 0) { dbBtreePage::markPage(db, root, type, height); }
  }
};

class dbBtreeIterator {
public:
  dbBtreeIterator(dbDatabase *db, oid_t treeId, int keyType, void *from,
                  length_t fromLength, int fromInclusion, void *till,
                  length_t tillLength, int tillInclusion, bool ascent);
  oid_t next();

private:
  void       gotoNextItem(dbBtreePage *pg, int pos);
  static int compare(void *key, int keyType, dbBtreePage *pg, int pos);
  static int compareStr(void *key, length_t keyLength, dbBtreePage *pg,
                        int pos);

  enum { MaxTreeHeight = 8 };

  dbDatabase *db;
  int         height;
  int         type;
  int         sp;
  int         end;
  union {
    db_int1  boolKey;
    db_int4  intKey;
    db_int8  longKey;
    oid_t    refKey;
    db_real8 realKey;
  } from_val, till_val;
  void *   from;
  void *   till;
  length_t fromLength;
  length_t tillLength;
  int      fromInclusion;
  int      tillInclusion;
  bool     ascent;
  oid_t    pageStack[MaxTreeHeight];
  int      posStack[MaxTreeHeight];
};

#endif
