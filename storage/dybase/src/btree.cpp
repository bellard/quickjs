//-< BTREE.CPP >-----------------------------------------------------*--------*
// GigaBASE                  Version 1.0         (c) 1999  GARRET    *     ?  *
// (Post Relational Database Management System)                      *   /\|  *
//                                                                   *  /  \  *
//                          Created:      1-Jan-99    K.A. Knizhnik  * / [] \ *
//                          Last update: 25-Oct-99    K.A. Knizhnik  * GARRET *
//-------------------------------------------------------------------*--------*
// B-Tree implementation
//-------------------------------------------------------------------*--------*

#include "stdtp.h"
#include "database.h"
#include "btree.h"

#if defined(__sun) || defined(__SVR4)
#define NO_LARGE_LOCAL_ARRAYS
#endif

void dbBtree::find(dbDatabase *db, oid_t treeId, dbSearchContext &sc) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return;
  }
  dbGetTie tie;
  dbBtree *tree = (dbBtree *)db->getObject(tie, treeId);

  if (sc.keyType != tree->type) {
    if (sc.low != NULL || sc.high != NULL) {
      db->handleError(dybase_bad_key_type,
                      "Type of the key doesn't match index type");
      return;
    }
    sc.keyType = tree->type;
  }

  oid_t rootId = tree->root;
  int   height = tree->height;
  if (rootId != 0) {
    dbBtreePage *page = (dbBtreePage *)db->get(rootId);
    (void)page->find(db, sc, height);
    db->pool.unfix(page);
  }
}

oid_t dbBtree::allocate(dbDatabase *db, int type, bool unique) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return 0;
  }
  /*dbBtree* tree = new dbBtree();
  tree->size = sizeof(dbBtree);
  tree->root = 0;
  tree->height = 0;
  tree->type = type;
  tree->unique = unique;
  tree->cid = dbBtreeId;
  return db->allocateObject(tree);*/

  dbBtree tree;
  tree.size   = sizeof(dbBtree);
  tree.root   = 0;
  tree.height = 0;
  tree.type   = type;
  tree.unique = unique;
  tree.cid    = dbBtreeId;
  return db->allocateObject(&tree);
}

bool dbBtree::packItem(dbDatabase *db, dbBtree *tree, dbBtreePage::item &it,
                       void *key, int keyType, length_t keySize, oid_t oid) {

  if (keyType != tree->type) {
    db->handleError(dybase_bad_key_type,
                    "Type of the key doesn't match index type");
    return false;
  }
  it.oid = oid;
  switch (keyType) {
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type:
    it.keyLen = sizeof(oid_t);
    it.refKey = *(oid_t *)key;
    break;
  case dybase_bool_type:
    it.keyLen  = sizeof(db_int1);
    it.boolKey = *(db_int1 *)key;
    break;
  case dybase_int_type:
    it.keyLen = sizeof(db_int4);
    it.intKey = *(db_int4 *)key;
    break;
  case dybase_date_type:
  case dybase_long_type:
    it.keyLen  = sizeof(db_int8);
    it.longKey = *(db_int8 *)key;
    break;
  case dybase_real_type:
    it.keyLen  = sizeof(db_real8);
    it.realKey = *(db_real8 *)key;
    break;
  case dybase_chars_type:
    if (keySize > dbBtreePage::dbMaxKeyLen) {
      db->handleError(dybase_bad_key_type, "Size of string key is too large");
      return false;
    }
    it.keyLen = int(keySize);
    memcpy(it.charKey, key, keySize);
    break;
  case dybase_bytes_type:
    if (keySize > dbBtreePage::dbMaxKeyLen) {
      db->handleError(dybase_bad_key_type, "Size of string key is too large");
      return false;
    }
    it.keyLen = int(keySize);
    memcpy(it.charKey, key, keySize);
    break;
  }
  return true;
}

bool dbBtree::insert(dbDatabase *db, oid_t treeId, void *key, int keyType,
                     length_t keySize, oid_t oid, bool replace) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return false;
  }
  dbGetTie          treeTie;
  dbBtreePage::item ins;
  dbBtree *         tree = (dbBtree *)db->getObject(treeTie, treeId);

  if (!packItem(db, tree, ins, key, keyType, keySize, oid)) { return false; }

  oid_t rootId = tree->root;
  int   height = tree->height;

  if (rootId == 0) {
    dbPutTie tie;
    dbBtree *t = (dbBtree *)db->putObject(tie, treeId);
    t->root    = dbBtreePage::allocate(db, 0, tree->type, ins);
    t->height  = 1;
    return true;
  } else {
    int result = dbBtreePage::insert(db, rootId, tree->type, ins, tree->unique,
                                     replace, height);
    assert(result != not_found);
    if (result == overflow) {
      dbPutTie tie;
      dbBtree *t = (dbBtree *)db->putObject(tie, treeId);
      t->root    = dbBtreePage::allocate(db, rootId, tree->type, ins);
      t->height += 1;
    }
    return result != duplicate;
  }
}

bool dbBtree::is_unique(dbDatabase *db, oid_t treeId) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return false;
  }
  dbGetTie treeTie;
  dbBtree *tree = (dbBtree *)db->getObject(treeTie, treeId);
  return tree->unique;
}

int dbBtree::get_type(dbDatabase *db, oid_t treeId) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return 0;
  }
  dbGetTie treeTie;
  dbBtree *tree = (dbBtree *)db->getObject(treeTie, treeId);
  return tree->type;
}


bool dbBtree::remove(dbDatabase *db, oid_t treeId, void *key, int keyType,
                     length_t keySize, oid_t oid) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return false;
  }
  dbGetTie          treeTie;
  dbBtreePage::item rem;
  dbBtree *         tree = (dbBtree *)db->getObject(treeTie, treeId);

  if (oid == 0 && !tree->unique) {
    db->handleError(dybase_bad_key_type, "Associated object should be "
                                         "specified to perform remove from "
                                         "non-unique index");
    return false;
  }

  if (!packItem(db, tree, rem, key, keyType, keySize, oid)) { return false; }

  oid_t rootId = tree->root;
  int   height = tree->height;

  if (rootId == 0) { return false; }

  int result = dbBtreePage::remove(db, rootId, tree->type, rem, height);
  if (result == underflow) {
    dbBtreePage *page = (dbBtreePage *)db->get(rootId);
    if (page->nItems == 0) {
      dbPutTie tie;
      dbBtree *t = (dbBtree *)db->putObject(tie, treeId);
      if (height == 1) {
        t->height = 0;
        t->root   = 0;
      } else {
        if (tree->type == dybase_chars_type || tree->type == dybase_bytes_type) {
          t->root = page->strKey[0].oid;
        } else {
          t->root = page->record[dbBtreePage::maxItems - 1];
        }
        t->height -= 1;
      }
      db->freePage(rootId);
    }
    db->pool.unfix(page);
  } else if (result == dbBtree::overflow) {
    dbPutTie tie;
    dbBtree *t = (dbBtree *)db->putObject(tie, treeId);
    t->root    = dbBtreePage::allocate(db, rootId, tree->type, rem);
    t->height += 1;
  }
  return result != not_found;
}

void dbBtree::clear(dbDatabase *db, oid_t treeId) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return;
  }
  _clear(db, treeId);
}

void dbBtree::_clear(dbDatabase *db, oid_t treeId) {
  dbPutTie tie;
  dbBtree *tree = (dbBtree *)db->putObject(tie, treeId);
  if (tree->root != 0) {
    dbBtreePage::purge(db, tree->root, tree->type, tree->height);
    tree->root   = 0;
    tree->height = 0;
  }
}

void dbBtree::drop(dbDatabase *db, oid_t treeId) {
  dbCriticalSection cs(db->mutex);
  if (!db->opened) {
    db->handleError(dybase_not_opened, "Database not opened");
    return;
  }
  _drop(db, treeId);
}

void dbBtree::_drop(dbDatabase *db, oid_t treeId) {
  _clear(db, treeId);
  db->free(db->getPos(treeId) & ~dbFlagsMask, sizeof(dbBtree));
  db->freeId(treeId);
}

#define CHECK(a, b, inclusion) (a > b || (a == b && !inclusion))

#define FIND(KEY, TYPE)                                                        \
  if (sc.low != NULL) {                                                        \
    TYPE key = *(TYPE *)sc.low;                                                \
    while (l < r) {                                                            \
      int i = (l + r) >> 1;                                                    \
      if (CHECK(key, KEY[i], lowInclusion)) {                                  \
        l = i + 1;                                                             \
      } else {                                                                 \
        r = i;                                                                 \
      }                                                                        \
    }                                                                          \
    assert(r == l);                                                            \
  }                                                                            \
  if (sc.high != NULL) {                                                       \
    TYPE key = *(TYPE *)sc.high;                                               \
    if (height == 0) {                                                         \
      while (l < n) {                                                          \
        if (CHECK(KEY[l], key, highInclusion)) { return false; }               \
        sc.selection.add(record[maxItems - 1 - l]);                            \
        l += 1;                                                                \
      }                                                                        \
      return true;                                                             \
    } else {                                                                   \
      do {                                                                     \
        dbBtreePage *pg = (dbBtreePage *)db->get(record[maxItems - 1 - l]);    \
        if (!pg->find(db, sc, height)) {                                       \
          db->pool.unfix(pg);                                                  \
          return false;                                                        \
        }                                                                      \
        db->pool.unfix(pg);                                                    \
        if (l == n) { return true; }                                           \
      } while (KEY[l++] <= key);                                               \
      return false;                                                            \
    }                                                                          \
  }                                                                            \
  break

inline int compareStrings(void *s1, length_t n1, void *s2, length_t n2) {
  length_t len  = n1 < n2 ? n1 : n2;
  int      diff = memcmp(s1, s2, len);
  return diff != 0 ? diff : int(n1) - int(n2);
}

bool dbBtreePage::find(dbDatabase *db, dbSearchContext &sc, int height) {
  int l = 0, n = nItems, r = n;
  height -= 1;
  int lowInclusion  = sc.lowInclusive;
  int highInclusion = sc.highInclusive;

  switch (sc.keyType) {
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type: FIND(refKey, oid_t);
  case dybase_bool_type: FIND(boolKey, db_int1);
  case dybase_int_type: FIND(intKey, db_int4);
  case dybase_date_type:
  case dybase_long_type: FIND(longKey, db_int8);
  case dybase_real_type: FIND(realKey, db_real8);
  case dybase_bytes_type:
  case dybase_chars_type:
    if (sc.low != NULL) {
      while (l < r) {
        int i = (l + r) >> 1;
        if (compareStrings(sc.low, sc.lowSize, &charKey[strKey[i].offs],
                           strKey[i].size) >= lowInclusion) {
          l = i + 1;
        } else {
          r = i;
        }
      }
      assert(r == l);
    }
    if (sc.high != NULL) {
      if (height == 0) {
        while (l < n) {
          if (compareStrings(&charKey[strKey[l].offs], strKey[l].size, sc.high,
                             sc.highSize) >= highInclusion) {
            return false;
          }
          sc.selection.add(strKey[l].oid);
          l += 1;
        }
      } else {
        do {
          dbBtreePage *pg = (dbBtreePage *)db->get(strKey[l].oid);
          if (!pg->find(db, sc, height)) {
            db->pool.unfix(pg);
            return false;
          }
          db->pool.unfix(pg);
          if (l == n) { return true; }
          l += 1;
        } while (compareStrings(&charKey[strKey[l - 1].offs],
                                strKey[l - 1].size, sc.high, sc.highSize) <= 0);
        return false;
      }
    } else {
      if (height == 0) {
        while (l < n) {
          sc.selection.add(strKey[l].oid);
          l += 1;
        }
      } else {
        do {
          dbBtreePage *pg = (dbBtreePage *)db->get(strKey[l].oid);
          if (!pg->find(db, sc, height)) {
            db->pool.unfix(pg);
            return false;
          }
          db->pool.unfix(pg);
        } while (++l <= n);
      }
    }
    return true;
  }
  if (height == 0) {
    while (l < n) {
      sc.selection.add(record[maxItems - 1 - l]);
      l += 1;
    }
  } else {
    do {
      dbBtreePage *pg = (dbBtreePage *)db->get(record[maxItems - 1 - l]);
      if (!pg->find(db, sc, height)) {
        db->pool.unfix(pg);
        return false;
      }
      db->pool.unfix(pg);
    } while (++l <= n);
  }
  return true;
}

oid_t dbBtreePage::allocate(dbDatabase *db, oid_t root, int type, item &ins) {
  oid_t        pageId = db->allocatePage();
  dbBtreePage *page   = (dbBtreePage *)db->put(pageId);
  page->nItems        = 1;
  if (type == dybase_chars_type || type == dybase_bytes_type) {
    page->size = ins.keyLen * sizeof(char);
    page->strKey[0].offs = db_nat2(sizeof(page->charKey) - ins.keyLen * sizeof(char));
    page->strKey[0].size = db_nat2(ins.keyLen);
    page->strKey[0].oid  = ins.oid;
    page->strKey[1].oid  = root;
    memcpy(&page->charKey[page->strKey[0].offs], ins.charKey, ins.keyLen);
  } else {
    memcpy(page->charKey, ins.charKey, dbSizeofType[type]);
    page->record[maxItems - 1] = ins.oid;
    page->record[maxItems - 2] = root;
  }
  db->pool.unfix(page);
  return pageId;
}

#define INSERT(KEY, TYPE)                                                      \
  {                                                                            \
    TYPE key = ins.KEY;                                                        \
    while (l < r) {                                                            \
      int i = (l + r) >> 1;                                                    \
      if (key > pg->KEY[i])                                                    \
        l = i + 1;                                                             \
      else                                                                     \
        r = i;                                                                 \
    }                                                                          \
    assert(l == r);                                                            \
    /* insert before e[r] */                                                   \
    if (--height != 0) {                                                       \
      result = insert(db, pg->record[maxItems - r - 1], type, ins, unique,     \
                      replace, height);                                        \
      if (result != dbBtree::overflow) {                                       \
        db->pool.unfix(pg);                                                    \
        return result;                                                         \
      }                                                                        \
      n += 1;                                                                  \
    } else if (r < n && key == pg->KEY[r]) {                                   \
      if (replace) {                                                           \
        db->pool.unfix(pg);                                                    \
        pg                           = (dbBtreePage *)db->put(tie, pageId);    \
        pg->record[maxItems - r - 1] = ins.oid;                                \
        return dbBtree::done;                                                  \
      } else if (unique) {                                                     \
        return dbBtree::duplicate;                                             \
      }                                                                        \
    }                                                                          \
    db->pool.unfix(pg);                                                        \
    pg            = (dbBtreePage *)db->put(tie, pageId);                       \
    const int max = sizeof(pg->KEY) / (sizeof(oid_t) + sizeof(TYPE));          \
    if (n < max) {                                                             \
      memmove(&pg->KEY[r + 1], &pg->KEY[r], (n - r) * sizeof(TYPE));           \
      memcpy(&pg->record[maxItems - n - 1], &pg->record[maxItems - n],         \
             (n - r) * sizeof(oid_t));                                         \
      pg->KEY[r]                   = ins.KEY;                                  \
      pg->record[maxItems - r - 1] = ins.oid;                                  \
      pg->nItems += 1;                                                         \
      return dbBtree::done;                                                    \
    } else { /* page is full then divide page */                               \
      oid_t        pageId = db->allocatePage();                                \
      dbBtreePage *b      = (dbBtreePage *)db->put(pageId);                    \
      assert(n == max);                                                        \
      const int m = max / 2;                                                   \
      if (r < m) {                                                             \
        memcpy(b->KEY, pg->KEY, r * sizeof(TYPE));                             \
        b->KEY[r] = ins.KEY;                                                   \
        memcpy(&b->KEY[r + 1], &pg->KEY[r], (m - r - 1) * sizeof(TYPE));       \
        memcpy(pg->KEY, &pg->KEY[m - 1], (max - m + 1) * sizeof(TYPE));        \
        memcpy(&b->record[maxItems - r], &pg->record[maxItems - r],            \
               r * sizeof(oid_t));                                             \
        b->record[maxItems - r - 1] = ins.oid;                                 \
        memcpy(&b->record[maxItems - m], &pg->record[maxItems - m + 1],        \
               (m - r - 1) * sizeof(oid_t));                                   \
        memmove(&pg->record[maxItems - max + m - 1],                           \
                &pg->record[maxItems - max], (max - m + 1) * sizeof(oid_t));   \
      } else {                                                                 \
        memcpy(b->KEY, pg->KEY, m * sizeof(TYPE));                             \
        memcpy(pg->KEY, &pg->KEY[m], (r - m) * sizeof(TYPE));                  \
        pg->KEY[r - m] = ins.KEY;                                              \
        memcpy(&pg->KEY[r - m + 1], &pg->KEY[r], (max - r) * sizeof(TYPE));    \
        memcpy(&b->record[maxItems - m], &pg->record[maxItems - m],            \
               m * sizeof(oid_t));                                             \
        memmove(&pg->record[maxItems - r + m], &pg->record[maxItems - r],      \
                (r - m) * sizeof(oid_t));                                      \
        pg->record[maxItems - r + m - 1] = ins.oid;                            \
        memmove(&pg->record[maxItems - max + m - 1],                           \
                &pg->record[maxItems - max], (max - r) * sizeof(oid_t));       \
      }                                                                        \
      ins.oid = pageId;                                                        \
      ins.KEY = b->KEY[m - 1];                                                 \
      if (height == 0) {                                                       \
        pg->nItems = max - m + 1;                                              \
        b->nItems  = m;                                                        \
      } else {                                                                 \
        pg->nItems = max - m;                                                  \
        b->nItems  = m - 1;                                                    \
      }                                                                        \
      db->pool.unfix(b);                                                       \
      return dbBtree::overflow;                                                \
    }                                                                          \
  \
}

int dbBtreePage::insert(dbDatabase *db, oid_t pageId, int type, item &ins,
                        bool unique, bool replace, int height) {
  dbPutTie     tie;
  dbBtreePage *pg = (dbBtreePage *)db->get(pageId);
  int          result;
  int          l = 0, n = pg->nItems, r = n;

  switch (type) {
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type: INSERT(refKey, oid_t);
  case dybase_bool_type: INSERT(boolKey, db_int1);
  case dybase_int_type:  INSERT(intKey, db_int4);
  case dybase_date_type:
  case dybase_long_type: INSERT(longKey, db_int8);
  case dybase_real_type: INSERT(realKey, db_real8);
  case dybase_bytes_type:
  case dybase_chars_type: {
    while (l < r) {
      int i = (l + r) >> 1;
      if (compareStrings(ins.charKey, ins.keyLen,
                         &pg->charKey[pg->strKey[i].offs],
                         pg->strKey[i].size) > 0) {
        l = i + 1;
      } else {
        r = i;
      }
    }
    if (--height != 0) {
      result =
          insert(db, pg->strKey[r].oid, type, ins, unique, replace, height);
      assert(result != dbBtree::not_found);
      if (result != dbBtree::overflow) {
        db->pool.unfix(pg);
        return result;
      }
    } else if (r < n && compareStrings(ins.charKey, ins.keyLen,
                                       &pg->charKey[pg->strKey[r].offs],
                                       pg->strKey[r].size) == 0) {
      if (replace) {
        db->pool.unfix(pg);
        pg                = (dbBtreePage *)db->put(tie, pageId);
        pg->strKey[r].oid = ins.oid;
        return dbBtree::done;
      } else if (unique) {
        return dbBtree::duplicate;
      }
    }
    db->pool.unfix(pg);
    pg = (dbBtreePage *)db->put(tie, pageId);
    return pg->insertStrKey(db, r, ins, height);
  }
  }
  return dbBtree::done;
}

int dbBtreePage::insertStrKey(dbDatabase *db, int r, item &ins, int height) {
  int n = height != 0 ? nItems + 1 : nItems;
  // insert before e[r]
  int len = ins.keyLen;
  if (size + len * sizeof(char) + (n + 1) * sizeof(str) <= sizeof(charKey)) {
    memmove(&strKey[r + 1], &strKey[r], (n - r) * sizeof(str));
    size += len * sizeof(char);
    strKey[r].offs = db_nat2(sizeof(charKey) - size);
    strKey[r].size = db_nat2(len);
    strKey[r].oid  = ins.oid;
    memcpy(&charKey[sizeof(charKey) - size], ins.charKey, len * sizeof(char));
    nItems += 1;
  } else { // page is full then divide page
    oid_t        pageId    = db->allocatePage();
    dbBtreePage *b         = (dbBtreePage *)db->put(pageId);
    length_t     moved     = 0;
    length_t     inserted  = len * sizeof(char) + sizeof(str);
    long         prevDelta = (1L << (sizeof(long) * 8 - 1)) + 1;
    for (int bn = 0, i = 0;; bn += 1) {
      length_t addSize, subSize;
      int      j      = nItems - i - 1;
      length_t keyLen = strKey[i].size;
      if (bn == r) {
        keyLen   = len;
        inserted = 0;
        addSize  = len;
        if (height == 0) {
          subSize = 0;
          j += 1;
        } else {
          subSize = strKey[i].size;
        }
      } else {
        addSize = subSize = keyLen;
        if (height != 0) {
          if (i + 1 != r) {
            subSize += strKey[i + 1].size;
            j -= 1;
          } else {
            inserted = 0;
          }
        }
      }
      long delta =
          long(moved + addSize * sizeof(char) + (bn + 1) * sizeof(str)) -
          long(j * sizeof(str) + size - subSize * sizeof(char) + inserted);
      if (delta >= -prevDelta) {
        char insKey[dbBtreePage::dbMaxKeyLen];
        if (bn <= r) { memcpy(insKey, ins.charKey, len * sizeof(char)); }
        if (height == 0) {
          memcpy(ins.charKey, (char *)&b->charKey[b->strKey[bn - 1].offs],
                 b->strKey[bn - 1].size);
          ins.keyLen = b->strKey[bn - 1].size;
        } else {
          assert(((void)"String fits in the B-Tree page",
                  moved + (bn + 1) * sizeof(str) <= sizeof(charKey)));
          if (bn != r) {
            ins.keyLen = int(keyLen);
            memcpy(ins.charKey, &charKey[strKey[i].offs],
                   keyLen * sizeof(char));
            b->strKey[bn].oid = strKey[i].oid;
            size -= db_nat4(keyLen * sizeof(char));
            i += 1;
          } else {
            b->strKey[bn].oid = ins.oid;
          }
        }
        compactify(i);
        if (bn < r || (bn == r && height == 0)) {
          memmove(&strKey[r - i + 1], &strKey[r - i], (n - r) * sizeof(str));
          size += len * sizeof(char);
          nItems += 1;
          assert(((void)"String fits in the B-Tree page",
                  size + (n - i + 1) * sizeof(str) <= sizeof(charKey)));
          strKey[r - i].offs = db_nat2(sizeof(charKey) - size);
          strKey[r - i].size = db_nat2(len);
          strKey[r - i].oid  = ins.oid;
          memcpy(&charKey[strKey[r - i].offs], insKey, len * sizeof(char));
        }
        b->nItems = bn;
        b->size   = db_nat4(moved);
        ins.oid   = pageId;
        db->pool.unfix(b);
        assert(nItems > 0 && b->nItems > 0);
        return dbBtree::overflow;
      }
      prevDelta = delta;
      moved += keyLen * sizeof(char);
      assert(((void)"String fits in the B-Tree page",
              moved + (bn + 1) * sizeof(str) <= sizeof(charKey)));
      b->strKey[bn].size = db_nat2(keyLen);
      b->strKey[bn].offs = db_nat2(sizeof(charKey) - moved);
      if (bn == r) {
        b->strKey[bn].oid = ins.oid;
        memcpy(&b->charKey[b->strKey[bn].offs], ins.charKey,
               keyLen * sizeof(char));
      } else {
        b->strKey[bn].oid = strKey[i].oid;
        memcpy(&b->charKey[b->strKey[bn].offs], &charKey[strKey[i].offs],
               keyLen * sizeof(char));
        size -= db_nat4(keyLen * sizeof(char));
        i += 1;
      }
    }
  }
  return size + sizeof(str) * (nItems + 1) < sizeof(charKey) / 2
             ? dbBtree::underflow
             : dbBtree::done;
}

void dbBtreePage::compactify(int m) {
  int i, j, offs, len, n = nItems;
#ifdef NO_LARGE_LOCAL_ARRAYS
  int *size  = new int[dbPageSize];
  int *index = new int[dbPageSize];
#else
  int size[dbPageSize];
  int index[dbPageSize];
#endif
  if (m == 0) { return; }
  if (m < 0) {
    m = -m;
    for (i = 0; i < n - m; i++) {
      len                                        = strKey[i].size;
      size[strKey[i].offs + len * sizeof(char)]  = len;
      index[strKey[i].offs + len * sizeof(char)] = i;
    }
    for (; i < n; i++) {
      len                                        = strKey[i].size;
      size[strKey[i].offs + len * sizeof(char)]  = len;
      index[strKey[i].offs + len * sizeof(char)] = -1;
    }
  } else {
    for (i = 0; i < m; i++) {
      len                                        = strKey[i].size;
      size[strKey[i].offs + len * sizeof(char)]  = len;
      index[strKey[i].offs + len * sizeof(char)] = -1;
    }
    for (; i < n; i++) {
      len                                        = strKey[i].size;
      size[strKey[i].offs + len * sizeof(char)]  = len;
      index[strKey[i].offs + len * sizeof(char)] = i - m;
      strKey[i - m].oid                          = strKey[i].oid;
      strKey[i - m].size                         = db_nat2(len);
    }
    strKey[i - m].oid = strKey[i].oid;
  }
  nItems = n -= m;
  for (offs = sizeof(charKey), i = offs; n != 0; i -= len) {
    len = size[i] * sizeof(char);
    j   = index[i];
    if (j >= 0) {
      offs -= len;
      n -= 1;
      strKey[j].offs = db_nat2(offs);
      if (offs != i - len) {
        memmove(&charKey[offs], &charKey[(i - len)], len);
      }
    }
  }
#ifdef NO_LARGE_LOCAL_ARRAYS
  delete[] size;
  delete[] index;
#endif
}

int dbBtreePage::removeStrKey(int r) {
  int len  = strKey[r].size;
  int offs = strKey[r].offs;
  memmove(charKey + sizeof(charKey) - size + len * sizeof(char),
          charKey + sizeof(charKey) - size, size - sizeof(charKey) + offs);
  memcpy(&strKey[r], &strKey[r + 1], (nItems - r) * sizeof(str));
  nItems -= 1;
  size -= len * sizeof(char);
  for (int i = nItems; --i >= 0;) {
    if (strKey[i].offs < offs) {
      strKey[i].offs += db_nat2(len * sizeof(char));
    }
  }
  return size + sizeof(str) * (nItems + 1) < sizeof(charKey) / 2
             ? dbBtree::underflow
             : dbBtree::done;
}

int dbBtreePage::replaceStrKey(dbDatabase *db, int r, item &ins, int height) {
  ins.oid = strKey[r].oid;
  removeStrKey(r);
  return insertStrKey(db, r, ins, height);
}

int dbBtreePage::handlePageUnderflow(dbDatabase *db, int r, int type, item &rem,
                                     int height) {
  dbPutTie tie;
  if (type == dybase_chars_type || type == dybase_bytes_type) {
    dbBtreePage *a  = (dbBtreePage *)db->put(tie, strKey[r].oid);
    int          an = a->nItems;
    if (r < (int)nItems) { // exists greater page
      dbBtreePage *b           = (dbBtreePage *)db->get(strKey[r + 1].oid);
      int          bn          = b->nItems;
      length_t     merged_size = (an + bn) * sizeof(str) + a->size + b->size;
      if (height != 1) {
        merged_size += strKey[r].size * sizeof(char) + sizeof(str) * 2;
      }
      if (merged_size > sizeof(charKey)) {
        // reallocation of nodes between pages a and b
        int      i, j, k;
        dbPutTie tie;
        db->pool.unfix(b);
        b               = (dbBtreePage *)db->put(tie, strKey[r + 1].oid);
        length_t size_a = a->size;
        length_t size_b = b->size;
        length_t addSize, subSize;
        if (height != 1) {
          addSize = strKey[r].size;
          subSize = b->strKey[0].size;
        } else {
          addSize = subSize = b->strKey[0].size;
        }
        i = 0;
        long prevDelta =
            long(an * sizeof(str) + size_a) - long(bn * sizeof(str) + size_b);
        for (;;) {
          i += 1;
          long delta =
              long((an + i) * sizeof(str) + size_a + addSize * sizeof(char)) -
              long((bn - i) * sizeof(str) + size_b - subSize * sizeof(char));
          if (delta >= 0) {
            if (delta >= -prevDelta) { i -= 1; }
            break;
          }
          size_a += addSize * sizeof(char);
          size_b -= subSize * sizeof(char);
          prevDelta = delta;
          if (height != 1) {
            addSize = subSize;
            subSize = b->strKey[i].size;
          } else {
            addSize = subSize = b->strKey[i].size;
          }
        }
        int result = dbBtree::done;
        if (i > 0) {
          k = i;
          if (height != 1) {
            int len = strKey[r].size;
            a->size += len * sizeof(char);
            a->strKey[an].offs = db_nat2(sizeof(a->charKey) - a->size);
            a->strKey[an].size = db_nat2(len);
            memcpy(a->charKey + a->strKey[an].offs, charKey + strKey[r].offs,
                   len * sizeof(char));
            k -= 1;
            an += 1;
            a->strKey[an + k].oid = b->strKey[k].oid;
            b->size -= b->strKey[k].size * sizeof(char);
          }
          for (j = 0; j < k; j++) {
            int len = b->strKey[j].size;
            a->size += len * sizeof(char);
            b->size -= len * sizeof(char);
            a->strKey[an].offs = db_nat2(sizeof(a->charKey) - a->size);
            a->strKey[an].size = db_nat2(len);
            a->strKey[an].oid  = b->strKey[j].oid;
            memcpy(a->charKey + a->strKey[an].offs,
                   b->charKey + b->strKey[j].offs, len * sizeof(char));
            an += 1;
          }
          memcpy(rem.charKey, b->charKey + b->strKey[i - 1].offs,
                 b->strKey[i - 1].size * sizeof(char));
          rem.keyLen = b->strKey[i - 1].size;
          result     = replaceStrKey(db, r, rem, height);
          a->nItems  = an;
          b->compactify(i);
        }
        assert(a->nItems > 0 && b->nItems > 0);
        return result;
      } else { // merge page b to a
        if (height != 1) {
          a->size += (a->strKey[an].size = strKey[r].size) * sizeof(char);
          a->strKey[an].offs = db_nat2(sizeof(charKey) - a->size);
          memcpy(&a->charKey[a->strKey[an].offs], &charKey[strKey[r].offs],
                 strKey[r].size * sizeof(char));
          an += 1;
          a->strKey[an + bn].oid = b->strKey[bn].oid;
        }
        for (int i = 0; i < bn; i++, an++) {
          a->strKey[an] = b->strKey[i];
          a->strKey[an].offs -= db_nat2(a->size);
        }
        a->size += b->size;
        a->nItems = an;
        memcpy(a->charKey + sizeof(charKey) - a->size,
               b->charKey + sizeof(charKey) - b->size, b->size);
        db->pool.unfix(b);
        db->freePage(strKey[r + 1].oid);
        strKey[r + 1].oid = strKey[r].oid;
        return removeStrKey(r);
      }
    } else { // page b is before a
      dbBtreePage *b           = (dbBtreePage *)db->get(strKey[r - 1].oid);
      int          bn          = b->nItems;
      length_t     merged_size = (an + bn) * sizeof(str) + a->size + b->size;
      if (height != 1) {
        merged_size += strKey[r - 1].size * sizeof(char) + sizeof(str) * 2;
      }
      if (merged_size > sizeof(charKey)) {
        // reallocation of nodes between pages a and b
        dbPutTie tie;
        int      i, j, k;
        db->pool.unfix(b);
        b               = (dbBtreePage *)db->put(tie, strKey[r - 1].oid);
        length_t size_a = a->size;
        length_t size_b = b->size;
        length_t addSize, subSize;
        if (height != 1) {
          addSize = strKey[r - 1].size;
          subSize = b->strKey[bn - 1].size;
        } else {
          addSize = subSize = b->strKey[bn - 1].size;
        }
        i = 0;
        long prevDelta =
            long(an * sizeof(str) + size_a) - long(bn * sizeof(str) + size_b);
        for (;;) {
          i += 1;
          long delta =
              long((an + i) * sizeof(str) + size_a + addSize * sizeof(char)) -
              long((bn - i) * sizeof(str) + size_b - subSize * sizeof(char));
          if (delta >= 0) {
            if (delta >= -prevDelta) { i -= 1; }
            break;
          }
          prevDelta = delta;
          size_a += addSize * sizeof(char);
          size_b -= subSize * sizeof(char);
          if (height != 1) {
            addSize = subSize;
            subSize = b->strKey[bn - i - 1].size;
          } else {
            addSize = subSize = b->strKey[bn - i - 1].size;
          }
        }
        int result = dbBtree::done;
        if (i > 0) {
          k = i;
          assert(i < bn);
          if (height != 1) {
            memmove(&a->strKey[i], a->strKey, (an + 1) * sizeof(str));
            b->size -= b->strKey[bn - k].size * sizeof(char);
            k -= 1;
            a->strKey[k].oid  = b->strKey[bn].oid;
            int len           = strKey[r - 1].size;
            a->strKey[k].size = db_nat2(len);
            a->size += len * lengthof(char);
            a->strKey[k].offs = db_nat2(sizeof(charKey) - a->size);
            memcpy(&a->charKey[a->strKey[k].offs], &charKey[strKey[r - 1].offs],
                   len * sizeof(char));
          } else {
            memmove(&a->strKey[i], a->strKey, an * sizeof(str));
          }
          for (j = 0; j < k; j++) {
            int len = b->strKey[bn - k + j].size;
            a->size += len * sizeof(char);
            b->size -= len * sizeof(char);
            a->strKey[j].offs = db_nat2(sizeof(a->charKey) - a->size);
            a->strKey[j].size = db_nat2(len);
            a->strKey[j].oid  = b->strKey[bn - k + j].oid;
            memcpy(a->charKey + a->strKey[j].offs,
                   b->charKey + b->strKey[bn - k + j].offs, len * sizeof(char));
          }
          an += i;
          a->nItems = an;
          memcpy(rem.charKey, b->charKey + b->strKey[bn - k - 1].offs,
                 b->strKey[bn - k - 1].size * sizeof(char));
          rem.keyLen = b->strKey[bn - k - 1].size;
          result     = replaceStrKey(db, r - 1, rem, height);
          b->compactify(-i);
        }
        assert(a->nItems > 0 && b->nItems > 0);
        return result;
      } else { // merge page b to a
        if (height != 1) {
          memmove(a->strKey + bn + 1, a->strKey, (an + 1) * sizeof(str));
          a->size += (a->strKey[bn].size = strKey[r - 1].size) * sizeof(char);
          a->strKey[bn].offs = db_nat2(sizeof(charKey) - a->size);
          a->strKey[bn].oid  = b->strKey[bn].oid;
          memcpy(&a->charKey[a->strKey[bn].offs], &charKey[strKey[r - 1].offs],
                 strKey[r - 1].size * sizeof(char));
          an += 1;
        } else {
          memmove(a->strKey + bn, a->strKey, an * sizeof(str));
        }
        for (int i = 0; i < bn; i++) {
          a->strKey[i] = b->strKey[i];
          a->strKey[i].offs -= db_nat2(a->size);
        }
        an += bn;
        a->nItems = an;
        a->size += b->size;
        memcpy(a->charKey + sizeof(charKey) - a->size,
               b->charKey + sizeof(charKey) - b->size, b->size);
        db->pool.unfix(b);
        db->freePage(strKey[r - 1].oid);
        return removeStrKey(r - 1);
      }
    }
  } else {
    dbBtreePage *a = (dbBtreePage *)db->put(tie, record[maxItems - r - 1]);
    length_t     sizeofType = dbSizeofType[type];
    int          an         = a->nItems;
    if (r < int(nItems)) { // exists greater page
      dbBtreePage *b  = (dbBtreePage *)db->get(record[maxItems - r - 2]);
      int          bn = b->nItems;
      assert(bn >= an);
      if (height != 1) {
        memcpy(a->charKey + an * sizeofType, charKey + r * sizeofType,
               sizeofType);
        an += 1;
        bn += 1;
      }
      length_t merged_size = (an + bn) * (sizeof(oid_t) + sizeofType);
      if (merged_size > sizeof(charKey)) {
        // reallocation of nodes between pages a and b
        int      i = bn - ((an + bn) >> 1);
        dbPutTie tie;
        db->pool.unfix(b);
        b = (dbBtreePage *)db->put(tie, record[maxItems - r - 2]);
        memcpy(a->charKey + an * sizeofType, b->charKey, i * sizeofType);
        memcpy(b->charKey, b->charKey + i * sizeofType, (bn - i) * sizeofType);
        memcpy(&a->record[maxItems - an - i], &b->record[maxItems - i],
               i * sizeof(oid_t));
        memmove(&b->record[maxItems - bn + i], &b->record[maxItems - bn],
                (bn - i) * sizeof(oid_t));
        memcpy(charKey + r * sizeofType, a->charKey + (an + i - 1) * sizeofType,
               sizeofType);
        b->nItems -= i;
        a->nItems += i;
        return dbBtree::done;
      } else { // merge page b to a
        memcpy(a->charKey + an * sizeofType, b->charKey, bn * sizeofType);
        memcpy(&a->record[maxItems - an - bn], &b->record[maxItems - bn],
               bn * sizeof(oid_t));
        db->pool.unfix(b);
        db->freePage(record[maxItems - r - 2]);
        memmove(&record[maxItems - nItems], &record[maxItems - nItems - 1],
                (nItems - r - 1) * sizeof(oid_t));
        memcpy(charKey + r * sizeofType, charKey + (r + 1) * sizeofType,
               (nItems - r - 1) * sizeofType);
        a->nItems += bn;
        nItems -= 1;
        return (nItems + 1) * (sizeofType + sizeof(oid_t)) < sizeof(charKey) / 2
                   ? dbBtree::underflow
                   : dbBtree::done;
      }
    } else { // page b is before a
      dbBtreePage *b  = (dbBtreePage *)db->get(record[maxItems - r]);
      int          bn = b->nItems;
      assert(bn >= an);
      if (height != 1) {
        an += 1;
        bn += 1;
      }
      length_t merged_size = (an + bn) * (sizeof(oid_t) + sizeofType);
      if (merged_size > sizeof(charKey)) {
        // reallocation of nodes between pages a and b
        int      i = bn - ((an + bn) >> 1);
        dbPutTie tie;
        db->pool.unfix(b);
        b = (dbBtreePage *)db->put(tie, record[maxItems - r]);
        memmove(a->charKey + i * sizeofType, a->charKey, an * sizeofType);
        memcpy(a->charKey, b->charKey + (bn - i) * sizeofType, i * sizeofType);
        memcpy(&a->record[maxItems - an - i], &a->record[maxItems - an],
               an * sizeof(oid_t));
        memcpy(&a->record[maxItems - i], &b->record[maxItems - bn],
               i * sizeof(oid_t));
        if (height != 1) {
          memcpy(a->charKey + (i - 1) * sizeofType,
                 charKey + (r - 1) * sizeofType, sizeofType);
        }
        memcpy(charKey + (r - 1) * sizeofType,
               b->charKey + (bn - i - 1) * sizeofType, sizeofType);
        b->nItems -= i;
        a->nItems += i;
        return dbBtree::done;
      } else { // merge page b to a
        memmove(a->charKey + bn * sizeofType, a->charKey, an * sizeofType);
        memcpy(a->charKey, b->charKey, bn * sizeofType);
        memcpy(&a->record[maxItems - an - bn], &a->record[maxItems - an],
               an * sizeof(oid_t));
        memcpy(&a->record[maxItems - bn], &b->record[maxItems - bn],
               bn * sizeof(oid_t));
        if (height != 1) {
          memcpy(a->charKey + (bn - 1) * sizeofType,
                 charKey + (r - 1) * sizeofType, sizeofType);
        }
        db->pool.unfix(b);
        db->freePage(record[maxItems - r]);
        record[maxItems - r] = record[maxItems - r - 1];
        a->nItems += bn;
        nItems -= 1;
        return (nItems + 1) * (sizeofType + sizeof(oid_t)) < sizeof(charKey) / 2
                   ? dbBtree::underflow
                   : dbBtree::done;
      }
    }
  }
}

#define REMOVE(KEY, TYPE)                                                      \
  {                                                                            \
    TYPE key = rem.KEY;                                                        \
    while (l < r) {                                                            \
      i = (l + r) >> 1;                                                        \
      if (key > pg->KEY[i])                                                    \
        l = i + 1;                                                             \
      else                                                                     \
        r = i;                                                                 \
    }                                                                          \
    if (--height == 0) {                                                       \
      oid_t oid = rem.oid;                                                     \
      while (r < n) {                                                          \
        if (key == pg->KEY[r]) {                                               \
          if (pg->record[maxItems - r - 1] == oid || oid == 0) {               \
            db->pool.unfix(pg);                                                \
            pg = (dbBtreePage *)db->put(tie, pageId);                          \
            memcpy(&pg->KEY[r], &pg->KEY[r + 1], (n - r - 1) * sizeof(TYPE));  \
            memmove(&pg->record[maxItems - n + 1], &pg->record[maxItems - n],  \
                    (n - r - 1) * sizeof(oid_t));                              \
            pg->nItems = --n;                                                  \
            return n * (sizeof(TYPE) + sizeof(oid_t)) <                        \
                           sizeof(pg->charKey) / 2                             \
                       ? dbBtree::underflow                                    \
                       : dbBtree::done;                                        \
          }                                                                    \
        } else {                                                               \
          break;                                                               \
        }                                                                      \
        r += 1;                                                                \
      }                                                                        \
      db->pool.unfix(pg);                                                      \
      return dbBtree::not_found;                                               \
    }                                                                          \
    break;                                                                     \
  \
}

int dbBtreePage::remove(dbDatabase *db, oid_t pageId, int type, item &rem,
                        int height) {
  dbBtreePage *pg = (dbBtreePage *)db->get(pageId);
  dbPutTie     tie;
  int          i, n = pg->nItems, l = 0, r = n;

  switch (type) {
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type: REMOVE(refKey, oid_t);
  case dybase_bool_type: REMOVE(boolKey, db_int1);
  case dybase_int_type:  REMOVE(intKey, db_int4);
  case dybase_date_type:
  case dybase_long_type: REMOVE(longKey, db_int8);
  case dybase_real_type: REMOVE(realKey, db_real8);
  case dybase_bytes_type:
  case dybase_chars_type: {
    while (l < r) {
      i = (l + r) >> 1;
      if (compareStrings(rem.charKey, rem.keyLen,
                         &pg->charKey[pg->strKey[i].offs],
                         pg->strKey[i].size) > 0) {
        l = i + 1;
      } else {
        r = i;
      }
    }
    if (--height != 0) {
      do {
        switch (remove(db, pg->strKey[r].oid, type, rem, height)) {
        case dbBtree::underflow:
          db->pool.unfix(pg);
          pg = (dbBtreePage *)db->put(tie, pageId);
          return pg->handlePageUnderflow(db, r, type, rem, height);
        case dbBtree::done: db->pool.unfix(pg); return dbBtree::done;
        case dbBtree::overflow:
          db->pool.unfix(pg);
          pg = (dbBtreePage *)db->put(tie, pageId);
          return pg->insertStrKey(db, r, rem, height);
        }
      } while (++r <= n);
    } else {
      while (r < n) {
        if (compareStrings(rem.charKey, rem.keyLen,
                           &pg->charKey[pg->strKey[r].offs],
                           pg->strKey[r].size) == 0) {
          if (pg->strKey[r].oid == rem.oid || rem.oid == 0) {
            db->pool.unfix(pg);
            pg = (dbBtreePage *)db->put(tie, pageId);
            return pg->removeStrKey(r);
          }
        } else {
          break;
        }
        r += 1;
      }
    }
    db->pool.unfix(pg);
    return dbBtree::not_found;
  }
  default: assert(false);
  }
  do {
    switch (remove(db, pg->record[maxItems - r - 1], type, rem, height)) {
    case dbBtree::underflow:
      db->pool.unfix(pg);
      pg = (dbBtreePage *)db->put(tie, pageId);
      return pg->handlePageUnderflow(db, r, type, rem, height);
    case dbBtree::done: db->pool.unfix(pg); return dbBtree::done;
    }
  } while (++r <= n);

  db->pool.unfix(pg);
  return dbBtree::not_found;
}

void dbBtreePage::purge(dbDatabase *db, oid_t pageId, int type, int height) {
  if (--height != 0) {
    dbBtreePage *pg = (dbBtreePage *)db->get(pageId);
    int          n  = pg->nItems + 1;
    if (type == dybase_chars_type || type == dybase_bytes_type) { // page of strings
      while (--n >= 0) {
        purge(db, pg->strKey[n].oid, type, height);
      }
    } else {
      while (--n >= 0) {
        purge(db, pg->record[maxItems - n - 1], type, height);
      }
    }
    db->pool.unfix(pg);
  }
  db->freePage(pageId);
}

void dbBtreePage::markPage(dbDatabase *db, oid_t pageId, int type, int height) {
  dbBtreePage *pg =
      (dbBtreePage *)db->pool.get(db->getGCPos(pageId) & ~dbPageObjectFlag);
  int i, n = pg->nItems;
  if (--height != 0) {
    if (type == dybase_chars_type || type == dybase_bytes_type) { // page of strings
      for (i = 0; i <= n; i++) {
        markPage(db, pg->strKey[i].oid, type, height);
      }
    } else {
      for (i = 0; i <= n; i++) {
        markPage(db, pg->record[maxItems - i - 1], type, height);
      }
    }
  } else {
    if (type != dybase_chars_type && type != dybase_bytes_type) { // page of scalars
      for (i = 0; i < n; i++) {
        db->markOid(pg->record[maxItems - i - 1]);
      }
    } else { // page of strings
      for (i = 0; i < n; i++) {
        db->markOid(pg->strKey[i].oid);
      }
    }
  }
  db->pool.unfix(pg);
}

int dbBtreeIterator::compare(void *key, int keyType, dbBtreePage *pg, int pos) {
  switch (keyType) {
  case dybase_bool_type: return *(db_int1 *)key - pg->boolKey[pos];
  case dybase_int_type: return *(db_int4 *)key - pg->intKey[pos];
  case dybase_date_type:
  case dybase_long_type:
    return *(db_int8 *)key < pg->longKey[pos]
               ? -1
               : *(db_int8 *)key == pg->longKey[pos] ? 0 : 1;
  case dybase_real_type:
    return *(db_real8 *)key < pg->realKey[pos]
               ? -1
               : *(db_real8 *)key == pg->realKey[pos] ? 0 : 1;
  case dybase_object_ref_type:
  case dybase_array_ref_type:
  case dybase_index_ref_type: return *(oid_t *)key - pg->refKey[pos];
  }
  return 0;
}

int dbBtreeIterator::compareStr(void *key, length_t keyLength, dbBtreePage *pg,
                                int pos) {
  return compareStrings(key, keyLength, &pg->charKey[pg->strKey[pos].offs],
                        pg->strKey[pos].size);
}

dbBtreeIterator::dbBtreeIterator(dbDatabase *db, oid_t treeId, int type,
                                 void *from, length_t fromLength,
                                 int fromInclusion, void *till,
                                 length_t tillLength, int tillInclusion,
                                 bool ascent) {
  int      l, r, i;
  dbGetTie tie;
  dbBtree *tree = (dbBtree *)db->getObject(tie, treeId);
  sp            = 0;

  if (tree->height == 0) { return; }

  if (type != tree->type) {
    if (from != NULL || till != NULL) {
      db->throwException(dybase_bad_key_type,
                         "Type of the key doesn't match index type");
    } else {
      type = tree->type;
    }
  }
  dbBtreePage *pg;
  this->db            = db;
  this->from          = from;
  this->till          = till;
  this->fromLength    = fromLength;
  this->tillLength    = tillLength;
  this->fromInclusion = fromInclusion;
  this->tillInclusion = tillInclusion;
  this->type          = type;
  this->ascent        = ascent;
  this->height        = tree->height;
  int height          = tree->height;

  if (from != NULL) {
    switch (type) {
    case dybase_object_ref_type:
    case dybase_array_ref_type:
    case dybase_index_ref_type:
      from_val.refKey = *(oid_t *)from;
      this->from      = &from_val.refKey;
      break;
    case dybase_bool_type:
      from_val.boolKey = *(db_int1 *)from;
      this->from       = &from_val.boolKey;
      break;
    case dybase_int_type:
      from_val.intKey = *(db_int4 *)from;
      this->from      = &from_val.intKey;
      break;
    case dybase_date_type:
    case dybase_long_type:
      from_val.longKey = *(db_int8 *)from;
      this->from       = &from_val.longKey;
      break;
    case dybase_real_type:
      from_val.realKey = *(db_real8 *)from;
      this->from       = &from_val.realKey;
      break;
    }
  }
  if (till != NULL) {
    switch (type) {
    case dybase_object_ref_type:
    case dybase_array_ref_type:
    case dybase_index_ref_type:
      till_val.refKey = *(oid_t *)till;
      this->till      = &till_val.refKey;
      break;
    case dybase_bool_type:
      till_val.boolKey = *(db_int1 *)till;
      this->till       = &till_val.boolKey;
      break;
    case dybase_int_type:
      till_val.intKey = *(db_int4 *)till;
      this->till      = &till_val.intKey;
      break;
    case dybase_date_type:
    case dybase_long_type:
      till_val.longKey = *(db_int8 *)till;
      this->till       = &till_val.longKey;
      break;
    case dybase_real_type:
      till_val.realKey = *(db_real8 *)till;
      this->till       = &till_val.realKey;
      break;
    }
  }

  int pageId = tree->root;

  if (type == dybase_chars_type || type == dybase_bytes_type) {
    if (ascent) {
      if (from == NULL) {
        while (--height >= 0) {
          posStack[sp]  = 0;
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          pageId        = pg->strKey[0].oid;
          end           = pg->nItems;
          db->pool.unfix(pg);
          sp += 1;
        }
      } else {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          l             = 0;
          r             = pg->nItems;
          while (l < r) {
            i = (l + r) >> 1;
            if (compareStr(from, fromLength, pg, i) >= fromInclusion) {
              l = i + 1;
            } else {
              r = i;
            }
          }
          assert(r == l);
          posStack[sp] = r;
          pageId       = pg->strKey[r].oid;
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp] = pageId;
        pg            = (dbBtreePage *)db->get(pageId);
        l             = 0;
        end = r = pg->nItems;
        while (l < r) {
          i = (l + r) >> 1;
          if (compareStr(from, fromLength, pg, i) >= fromInclusion) {
            l = i + 1;
          } else {
            r = i;
          }
        }
        assert(r == l);
        if (r == end) {
          sp += 1;
          gotoNextItem(pg, r - 1);
        } else {
          posStack[sp++] = r;
          db->pool.unfix(pg);
        }
      }
      if (sp != 0 && till != NULL) {
        pg = (dbBtreePage *)db->get(pageStack[sp - 1]);
        if (-compareStr(till, tillLength, pg, posStack[sp - 1]) >=
            tillInclusion) {
          sp = 0;
        }
        db->pool.unfix(pg);
      }
    } else { // descent order
      if (till == NULL) {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          posStack[sp]  = pg->nItems;
          pageId        = pg->strKey[posStack[sp]].oid;
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp]  = pageId;
        pg             = (dbBtreePage *)db->get(pageId);
        posStack[sp++] = pg->nItems - 1;
        db->pool.unfix(pg);
      } else {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          l             = 0;
          r             = pg->nItems;
          while (l < r) {
            i = (l + r) >> 1;
            if (compareStr(till, tillLength, pg, i) >= 1 - tillInclusion) {
              l = i + 1;
            } else {
              r = i;
            }
          }
          assert(r == l);
          posStack[sp] = r;
          pageId       = pg->strKey[r].oid;
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp] = pageId;
        pg            = (dbBtreePage *)db->get(pageId);
        l             = 0;
        r             = pg->nItems;
        while (l < r) {
          i = (l + r) >> 1;
          if (compareStr(till, tillLength, pg, i) >= 1 - tillInclusion) {
            l = i + 1;
          } else {
            r = i;
          }
        }
        assert(r == l);
        if (r == 0) {
          sp += 1;
          gotoNextItem(pg, r);
        } else {
          posStack[sp++] = r - 1;
          db->pool.unfix(pg);
        }
      }
      if (sp != 0 && from != NULL) {
        pg = (dbBtreePage *)db->get(pageStack[sp - 1]);
        if (compareStr(from, fromLength, pg, posStack[sp - 1]) >=
            fromInclusion) {
          sp = 0;
        }
        db->pool.unfix(pg);
      }
    }
  } else { // scalar type
    if (ascent) {
      if (from == NULL) {
        while (--height >= 0) {
          posStack[sp]  = 0;
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          pageId        = pg->record[dbBtreePage::maxItems - 1];
          end           = pg->nItems;
          db->pool.unfix(pg);
          sp += 1;
        }
      } else {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          l             = 0;
          r             = pg->nItems;
          while (l < r) {
            i = (l + r) >> 1;
            if (compare(from, type, pg, i) >= fromInclusion) {
              l = i + 1;
            } else {
              r = i;
            }
          }
          assert(r == l);
          posStack[sp] = r;
          pageId       = pg->record[dbBtreePage::maxItems - 1 - r];
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp] = pageId;
        pg            = (dbBtreePage *)db->get(pageId);
        l             = 0;
        r = end = pg->nItems;
        while (l < r) {
          i = (l + r) >> 1;
          if (compare(from, type, pg, i) >= fromInclusion) {
            l = i + 1;
          } else {
            r = i;
          }
        }
        assert(r == l);
        if (r == end) {
          sp += 1;
          gotoNextItem(pg, r - 1);
        } else {
          posStack[sp++] = r;
          db->pool.unfix(pg);
        }
      }
      if (sp != 0 && till != NULL) {
        pg = (dbBtreePage *)db->get(pageStack[sp - 1]);
        if (-compare(till, type, pg, posStack[sp - 1]) >= tillInclusion) {
          sp = 0;
        }
        db->pool.unfix(pg);
      }
    } else { // descent order
      if (till == NULL) {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          posStack[sp]  = pg->nItems;
          pageId        = pg->record[dbBtreePage::maxItems - 1 - posStack[sp]];
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp]  = pageId;
        pg             = (dbBtreePage *)db->get(pageId);
        posStack[sp++] = pg->nItems - 1;
        db->pool.unfix(pg);
      } else {
        while (--height > 0) {
          pageStack[sp] = pageId;
          pg            = (dbBtreePage *)db->get(pageId);
          l             = 0;
          r             = pg->nItems;
          while (l < r) {
            i = (l + r) >> 1;
            if (compare(till, type, pg, i) >= 1 - tillInclusion) {
              l = i + 1;
            } else {
              r = i;
            }
          }
          assert(r == l);
          posStack[sp] = r;
          pageId       = pg->record[dbBtreePage::maxItems - 1 - r];
          db->pool.unfix(pg);
          sp += 1;
        }
        pageStack[sp] = pageId;
        pg            = (dbBtreePage *)db->get(pageId);
        l             = 0;
        r             = pg->nItems;
        while (l < r) {
          i = (l + r) >> 1;
          if (compare(till, type, pg, i) >= 1 - tillInclusion) {
            l = i + 1;
          } else {
            r = i;
          }
        }
        assert(r == l);
        if (r == 0) {
          sp += 1;
          gotoNextItem(pg, r);
        } else {
          posStack[sp++] = r - 1;
          db->pool.unfix(pg);
        }
      }
      if (sp != 0 && from != NULL) {
        pg = (dbBtreePage *)db->get(pageStack[sp - 1]);
        if (compare(from, type, pg, posStack[sp - 1]) >= fromInclusion) {
          sp = 0;
        }
        db->pool.unfix(pg);
      }
    }
  }
}

oid_t dbBtreeIterator::next() {
  if (sp == 0) { return 0; }
  int          pos = posStack[sp - 1];
  dbBtreePage *pg  = (dbBtreePage *)db->get(pageStack[sp - 1]);
  oid_t        oid = (type == dybase_chars_type || type == dybase_bytes_type)
                  ? pg->strKey[pos].oid
                  : pg->record[dbBtreePage::maxItems - 1 - pos];
  gotoNextItem(pg, pos);
  return oid;
}

void dbBtreeIterator::gotoNextItem(dbBtreePage *pg, int pos) {
  oid_t pageId;
  if (type == dybase_chars_type || type == dybase_bytes_type) {
    if (ascent) {
      if (++pos == end) {
        while (--sp != 0) {
          db->pool.unfix(pg);
          pos = posStack[sp - 1];
          pg  = (dbBtreePage *)db->get(pageStack[sp - 1]);
          if (++pos <= (int)pg->nItems) {
            posStack[sp - 1] = pos;
            do {
              pageId = pg->strKey[pos].oid;
              db->pool.unfix(pg);
              pg            = (dbBtreePage *)db->get(pageId);
              end           = pg->nItems;
              pageStack[sp] = pageId;
              posStack[sp] = pos = 0;
            } while (++sp < height);
            break;
          }
        }
      } else {
        posStack[sp - 1] = pos;
      }
      if (sp != 0 && till != NULL &&
          -compareStr(till, tillLength, pg, pos) >= tillInclusion) {
        sp = 0;
      }
    } else { // descent order
      if (--pos < 0) {
        while (--sp != 0) {
          db->pool.unfix(pg);
          pos = posStack[sp - 1];
          pg  = (dbBtreePage *)db->get(pageStack[sp - 1]);
          if (--pos >= 0) {
            posStack[sp - 1] = pos;
            do {
              pageId = pg->strKey[pos].oid;
              db->pool.unfix(pg);
              pg            = (dbBtreePage *)db->get(pageId);
              pageStack[sp] = pageId;
              posStack[sp] = pos = pg->nItems;
            } while (++sp < height);
            posStack[sp - 1] = --pos;
            break;
          }
        }
      } else {
        posStack[sp - 1] = pos;
      }
      if (sp != 0 && from != NULL &&
          compareStr(from, fromLength, pg, pos) >= fromInclusion) {
        sp = 0;
      }
    }
  } else { // scalar type
    if (ascent) {
      if (++pos == end) {
        while (--sp != 0) {
          db->pool.unfix(pg);
          pos = posStack[sp - 1];
          pg  = (dbBtreePage *)db->get(pageStack[sp - 1]);
          if (++pos <= (int)pg->nItems) {
            posStack[sp - 1] = pos;
            do {
              pageId = pg->record[dbBtreePage::maxItems - 1 - pos];
              db->pool.unfix(pg);
              pg            = (dbBtreePage *)db->get(pageId);
              end           = pg->nItems;
              pageStack[sp] = pageId;
              posStack[sp] = pos = 0;
            } while (++sp < height);
            break;
          }
        }
      } else {
        posStack[sp - 1] = pos;
      }
      if (sp != 0 && till != NULL &&
          -compare(till, type, pg, pos) >= tillInclusion) {
        sp = 0;
      }
    } else { // descent order
      if (--pos < 0) {
        while (--sp != 0) {
          db->pool.unfix(pg);
          pos = posStack[sp - 1];
          pg  = (dbBtreePage *)db->get(pageStack[sp - 1]);
          if (--pos >= 0) {
            posStack[sp - 1] = pos;
            do {
              pageId = pg->record[dbBtreePage::maxItems - 1 - pos];
              db->pool.unfix(pg);
              pg            = (dbBtreePage *)db->get(pageId);
              pageStack[sp] = pageId;
              posStack[sp] = pos = pg->nItems;
            } while (++sp < height);
            posStack[sp - 1] = --pos;
            break;
          }
        }
      } else {
        posStack[sp - 1] = pos;
      }
      if (sp != 0 && from != NULL &&
          compare(from, type, pg, pos) >= fromInclusion) {
        sp = 0;
      }
    }
  }
  db->pool.unfix(pg);
}
