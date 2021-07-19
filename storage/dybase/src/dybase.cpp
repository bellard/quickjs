
#include "stdtp.h"
#include "database.h"

#define INSIDE_DYBASE

#include "btree.h"
#include "dybase.h"

dybase_storage_t dybase_open(const char *file_path, int page_pool_size,
                             dybase_error_handler_t hnd, int read_write) {
  try {
    if (page_pool_size == 0) { page_pool_size = dbDefaultPagePoolSize; }

    dbDatabase::dbAccessType at =
        read_write ? dbDatabase::dbAllAccess : dbDatabase::dbReadOnly;

    dbDatabase *db = new dbDatabase(at, (dbDatabase::dbErrorHandler)hnd,
                                    page_pool_size / dbPageSize);
    if (db->open(file_path)) {
      return db;
    } else {
      delete db;
      return NULL;
    }
  } catch (dbException &) { 
    return NULL; 
  }
}

void dybase_close(dybase_storage_t storage) {
  dbDatabase *db = (dbDatabase *)storage;
  try {
    db->close();
    delete db;
  } catch (dbException &) { delete db; }
}

void dybase_commit(dybase_storage_t storage) {
  try {
    ((dbDatabase *)storage)->commit();
  } catch (dbException &) {}
}

void dybase_rollback(dybase_storage_t storage) {
  try {
    ((dbDatabase *)storage)->rollback();
  } catch (dbException &) {}
}

dybase_oid_t dybase_get_root_object(dybase_storage_t storage) {
  // try {
  return ((dbDatabase *)storage)->getRoot();
  //} catch (dbException&) {
  //    return 0;
  //}
}

void dybase_set_root_object(dybase_storage_t storage, dybase_oid_t oid) {
  try {
    ((dbDatabase *)storage)->setRoot(oid);
  } catch (dbException &) {}
}

dybase_oid_t dybase_allocate_object(dybase_storage_t storage) {
  try {
    return ((dbDatabase *)storage)->allocate();
  } catch (dbException &) { return 0; }
}

void dybase_deallocate_object(dybase_storage_t storage, dybase_oid_t oid) {
  try {
    ((dbDatabase *)storage)->freeObject(oid);
  } catch (dbException &) {}
}

dybase_handle_t dybase_begin_store_object(dybase_storage_t storage,
                                          dybase_oid_t     oid,
                                          char const *     class_name) {
  try {
    return ((dbDatabase *)storage)->getStoreHandle(oid, class_name);
  } catch (dbException &) { return NULL; }
}

void dybase_store_object_field(dybase_handle_t handle, char const *field_name,
                               int field_type, void *value_ptr,
                               int value_length) {
  try {
    ((dbStoreHandle *)handle)
        ->setFieldValue(field_name, field_type, value_ptr, value_length);
  } catch (dbException &) {}
}

void dybase_store_array_element(dybase_handle_t handle, int elem_type,
                                void *value_ptr, int value_length) {
  try {
    ((dbStoreHandle *)handle)->setElement(elem_type, value_ptr, value_length);
  } catch (dbException &) {}
}

void dybase_store_map_entry(dybase_handle_t handle, int key_type, void *key_ptr,
                            int key_length, int value_type, void *value_ptr,
                            int value_length) {
  try {
    ((dbStoreHandle *)handle)->setElement(key_type, key_ptr, key_length);
    ((dbStoreHandle *)handle)->setElement(value_type, value_ptr, value_length);
  } catch (dbException &) {}
}

void dybase_end_store_object(dybase_handle_t handle) {
  dbStoreHandle *hnd = (dbStoreHandle *)handle;
  try {
    hnd->db->storeObject(hnd);
    delete hnd;
  } catch (dbException &) { delete hnd; }
}

dybase_handle_t dybase_begin_load_object(dybase_storage_t storage,
                                         dybase_oid_t     oid) {
  try {
    return ((dbDatabase *)storage)->getLoadHandle(oid);
  } catch (dbException &) { return NULL; }
}

void dybase_end_load_object(dybase_handle_t handle) {
  if (handle) {
    dbLoadHandle *hnd = (dbLoadHandle *)handle;
    assert(hnd);
    delete hnd;
  }
}

char *dybase_get_class_name(dybase_handle_t handle) {
  return ((dbLoadHandle *)handle)->getClassName();
}

char *dybase_next_field(dybase_handle_t handle) {
  dbLoadHandle *hnd = (dbLoadHandle *)handle;
  if (!hnd->hasNextField()) {
    delete hnd;
    return NULL;
  } else {
    return hnd->getFieldName();
  }
}

void dybase_next_element(dybase_handle_t handle) {
  bool hasNext = ((dbLoadHandle *)handle)->hasNext();
  assert(hasNext);
  hasNext = hasNext;
}

void dybase_get_value(dybase_handle_t handle, int *type, void **value_ptr,
                      int *value_length) {
  dbLoadHandle *hnd = (dbLoadHandle *)handle;
  *type             = hnd->getType();
  *value_ptr        = hnd->getValue();
  *value_length     = hnd->getLength();
}

dybase_oid_t dybase_create_index(dybase_storage_t storage, int key_type,
                                 int unique) {
  try {
    return dbBtree::allocate((dbDatabase *)storage, key_type, (bool)unique);
  } catch (dbException &) { return 0; }
}

int dybase_insert_in_index(dybase_storage_t storage, dybase_oid_t index,
                           void *key, int key_type, int key_size,
                           dybase_oid_t obj, int replace) {
  try {
    return dbBtree::insert((dbDatabase *)storage, (oid_t)index, key, key_type,
                           key_size, (oid_t)obj, (bool)replace);
  } catch (dbException &) { return 0; }
}

int dybase_remove_from_index(dybase_storage_t storage, dybase_oid_t index,
                             void *key, int key_type, int key_size,
                             dybase_oid_t obj) {
  try {
    return dbBtree::remove((dbDatabase *)storage, (oid_t)index, key, key_type,
                           key_size, (oid_t)obj);
  } catch (dbException &) { return 0; }
}

int dybase_is_index_unique(dybase_storage_t storage, dybase_oid_t index) {
  try {
    return dbBtree::is_unique((dbDatabase *)storage, (oid_t)index);
  } catch (dbException &) { return 0; }
}

int dybase_get_index_type(dybase_storage_t storage, dybase_oid_t index) {
  try {
    return dbBtree::get_type((dbDatabase *)storage, (oid_t)index);
  }
  catch (dbException &) { return 0; }
}

int dybase_index_search(dybase_storage_t storage, dybase_oid_t index,
                        int key_type, void *min_key, int min_key_size,
                        int min_key_inclusive, void *max_key, int max_key_size,
                        int            max_key_inclusive,
                        dybase_oid_t **selected_objects) {
  try {
    dbSearchContext ctx;
    ctx.low           = min_key;
    ctx.lowSize       = min_key_size;
    ctx.lowInclusive  = min_key_inclusive;
    ctx.high          = max_key;
    ctx.highSize      = max_key_size;
    ctx.highInclusive = max_key_inclusive;
    ctx.keyType       = key_type;
    dbBtree::find((dbDatabase *)storage, (oid_t)index, ctx);
    *selected_objects = ctx.selection.grab();
    return ctx.selection.size();
  } catch (dbException &) { return 0; }
}

void dybase_free_selection(dybase_storage_t /*storage*/,
                           dybase_oid_t *selected_objects, int /*n_selected*/) {
  try {
    delete[] selected_objects;
  } catch (dbException &) {}
}

void dybase_drop_index(dybase_storage_t storage, dybase_oid_t index) {
  try {
    dbBtree::drop((dbDatabase *)storage, (oid_t)index);
  } catch (dbException &) {}
}

void dybase_clear_index(dybase_storage_t storage, dybase_oid_t index) {
  try {
    dbBtree::clear((dbDatabase *)storage, (oid_t)index);
  } catch (dbException &) {}
}

dybase_iterator_t dybase_create_index_iterator(
    dybase_storage_t storage, dybase_oid_t index, int key_type, void *min_key,
    int min_key_size, int min_key_inclusive, void *max_key, int max_key_size,
    int max_key_inclusive, int ascent) {
  try {
    return (dybase_iterator_t) new dbBtreeIterator(
        (dbDatabase *)storage, (oid_t)index, key_type, min_key, min_key_size,
        min_key_inclusive, max_key, max_key_size, max_key_inclusive,
        (bool)ascent);
  } catch (dbException &) { return NULL; }
}

dybase_oid_t dybase_index_iterator_next(dybase_iterator_t iterator) {
  try {
    return ((dbBtreeIterator *)iterator)->next();
  } catch (dbException &) { return 0; }
}

void dybase_free_index_iterator(dybase_iterator_t iterator) {
  delete (dbBtreeIterator *)iterator;
}

void dybase_set_gc_threshold(dybase_storage_t storage, long allocated_delta) {
  ((dbDatabase *)storage)->setGcThreshold(allocated_delta);
}

void dybase_gc(dybase_storage_t storage) { ((dbDatabase *)storage)->gc(); }


hashtable_t hashtable_create() {
  return new dbHashtable();
}
void  hashtable_put(hashtable_t ht, void *key, int keySize, void *value)
{
  dbHashtable* pht = (dbHashtable*)ht;
  pht->put(key, keySize, value);
}
void* hashtable_get(hashtable_t ht, void *key, int keySize)
{
  dbHashtable* pht = (dbHashtable*)ht;
  return pht->get(key, keySize);
}
void hashtable_free(hashtable_t ht)
{
  dbHashtable* pht = (dbHashtable*)ht;
  delete pht;
}

void* hashtable_remove(hashtable_t ht, void *key, int keySize)
{
  dbHashtable* pht = (dbHashtable*)ht;
  return pht->remove(key, keySize);
}
void  hashtable_clear(hashtable_t ht) {
  dbHashtable* pht = (dbHashtable*)ht;
  pht->clear();
}

typedef int each_cb_t(void* key, unsigned int key_length, void* data, void* opaque);

void hashtable_each(hashtable_t ht, each_cb_t* pcb, void* opaque)
{
  dbHashtable* pht = (dbHashtable*)ht;
  pht->each(pcb, opaque);
}


