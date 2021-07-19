#ifndef __DYBASE_H__
#define __DYBASE_H__

#ifndef DYBASE_DLL_ENTRY
#if defined(_WIN32) && defined(DYBASE_DLL)
#ifdef INSIDE_DYBASE
#define DYBASE_DLL_ENTRY __declspec(dllexport)
#else
#define DYBASE_DLL_ENTRY __declspec(dllimport)
#endif
#else
#define DYBASE_DLL_ENTRY
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _INC_CTYPE
#include <ctype.h>
#endif //_INC_CTYPE

/**
 * Supported database field types
 */
enum dybase_type {
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
  dybase_bytes_type  = 11,  // literal blob - byte vector, max length 2^32
};

/**
 * Error codes
 */
enum dybase_error_code {
  dybase_no_error,
  dybase_not_opened,
  dybase_open_error,
  dybase_file_error,
  dybase_bad_key_type,
  dybase_out_of_memory_error
};

typedef void *   dybase_storage_t;
typedef void *   dybase_handle_t;
typedef void *   dybase_iterator_t;
typedef unsigned dybase_oid_t;

typedef void *   hashtable_t;

typedef void (*dybase_error_handler_t)(int error_code, char const *msg);

/**
 * Open storage
 * @param file_path path to the storage file
 * @page_pool_size size of page pool in bytes, if 0, then default value will be
 * used
 * @param hnd error handler
 * @return pointer to the opened storage or NULL if open failed
 */
dybase_storage_t DYBASE_DLL_ENTRY dybase_open(const char *file_path,
                                              int         page_pool_size,
                                              dybase_error_handler_t hnd,
                                              int read_write);

/**
 * Close storage
 * @param storage pointer to the opened storage
 */
void DYBASE_DLL_ENTRY dybase_close(dybase_storage_t storage);

/**
 * Commit current transaction
 * @param storage pointer to the opened storage
 */
void DYBASE_DLL_ENTRY dybase_commit(dybase_storage_t storage);

/**
 * Rollback current transaction
 * @param storage pointer to the opened storage
 */
void DYBASE_DLL_ENTRY dybase_rollback(dybase_storage_t storage);

/**
 * Get identifier of the root object
 * @param storage pointer to the opened storage
 * @return root object OID or 0 if root was not yet specified
 */
dybase_oid_t DYBASE_DLL_ENTRY dybase_get_root_object(dybase_storage_t storage);

/**
 * Set storage root
 * @param storage pointer to the opened storage
 * @param oid object identifier of new storage root
 */
void DYBASE_DLL_ENTRY dybase_set_root_object(dybase_storage_t storage,
                                             dybase_oid_t     oid);

/**
 * Allocate object
 * @param storage pointer to the opened storage
 * @return new OID assigned to the object
 */
dybase_oid_t DYBASE_DLL_ENTRY dybase_allocate_object(dybase_storage_t storage);

/**
 * Deallocate object
 * @param storage pointer to the opened storage
 * @param oid object identifier of deallocated object
 */
void DYBASE_DLL_ENTRY dybase_deallocate_object(dybase_storage_t storage,
                                               dybase_oid_t     oid);

/**
 * Begin store of the object
 * @param storage pointer to the opened storage
 * @param oid object identifier of stored object
 * @param class name name of the class of stored object
 * @return handle of stored object which should be used in subsequent
 * dybase_store_object_field and dybase_end_store_object methods
 */
dybase_handle_t DYBASE_DLL_ENTRY dybase_begin_store_object(
    dybase_storage_t storage, dybase_oid_t oid, char const *class_name);

/**
 * Store object field
 * @param handle object handle returned by dybase_begin_store_object
 * @param field_name name of the field
 * @param field_type type of the field (one of the constants from dybase_type
 * enum)
 * @param value_ptr pointer to the value
 * @param value length (used for string, array and map types)
 */
void DYBASE_DLL_ENTRY dybase_store_object_field(dybase_handle_t handle,
                                                char const *    field_name,
                                                int field_type, void *value_ptr,
                                                int value_length);

/**
 * Store array element. This method can be also used to store map elements, in
 * this case for each map entry the method should be invoked for entry key and
 * entry value.
 * @param handle object handle returned by dybase_begin_store_object
 * @param elem_type type of the element (one of the constants from dybase_type
 * enum)
 * @param value_ptr pointer to the value
 * @param value length (used for string, array and map types)
 */
void DYBASE_DLL_ENTRY dybase_store_array_element(dybase_handle_t handle,
                                                 int elem_type, void *value_ptr,
                                                 int value_length);

/**
 * Store map entry
 * @param handle object handle returned by dybase_begin_store_object
 * @param key_type type of the key (one of the constants from dybase_type enum)
 * @param key_ptr pointer to the key
 * @param key_length (used for string and array types)
 * @param value_type type of the value (one of the constants from dybase_type
 * enum)
 * @param value_ptr pointer to the value
 * @param value_length (used for string, array and map types)
 */
void DYBASE_DLL_ENTRY dybase_store_map_entry(dybase_handle_t handle,
                                             int key_type, void *key_ptr,
                                             int key_length, int value_type,
                                             void *value_ptr, int value_length);

/**
 * End of object store
 * @param handle object handle returned by dybase_begin_store_object
 */
void DYBASE_DLL_ENTRY dybase_end_store_object(dybase_handle_t handle);

/**
 * Begin loading of the object
 * @param storage pointer to the opened storage
 * @param oid object identifier of loaded object
 * @return handle of stored object which should be used in subsequent
 * dybase_load_object_field and dybase_get_class_name methods
 */
dybase_handle_t DYBASE_DLL_ENTRY
                dybase_begin_load_object(dybase_storage_t storage, dybase_oid_t oid);

/**
 * End loading of the object
 * @param handle of stored object
 */
void dybase_end_load_object(dybase_handle_t handle);

/**
 * Get loaded object class name
 * @param handle object handle returned by dybase_begin_load_object
 * @return class name of the loaded object
 */
DYBASE_DLL_ENTRY char *dybase_get_class_name(dybase_handle_t handle);

/**
 * Move to next field. This function should be called before dybase_get_value
 * function. When this functions is called first time after
 * dybase_begin_load_object, information about first field is returned.
 * @param handle object handle returned by dybase_begin_load_object
 * @return field name or NULL if there are no more fields (in this case this
 * function also destruct the handle)
 */
DYBASE_DLL_ENTRY char *dybase_next_field(dybase_handle_t handle);

/**
 * Move to next array element. This function should be called before
 * dybase_get_value function. When this functions is called first time after
 * dybase_next_field, cursor is positioned at element of array with index 0
 * @param handle object handle returned by dybase_begin_load_object
 */
void DYBASE_DLL_ENTRY dybase_next_element(dybase_handle_t handle);

/**
 * Get value of the field, array element or map enrty. For map entry you should
 * invoke this methogd twice - first for entry  key, and second - for entry
 * value.
 * @param handle object handle returned by dybase_begin_load_object
 * @param field_type pointer to the location to receive type of the field or
 * array element
 * @param value_ptr pointer to the location to receive pointer to the object
 * value
 * @param value_length pointer to the location to receive length of string,
 * array or map field
 */
void DYBASE_DLL_ENTRY dybase_get_value(dybase_handle_t handle, int *type,
                                       void **value_ptr, int *value_length);

/**
 * Create object index
 * @param storage pointer to the opened storage
 * @param key_type type of the index key
 * @param unique whether index allows duplicates or not
 * @return OID of created index
 */
dybase_oid_t DYBASE_DLL_ENTRY dybase_create_index(dybase_storage_t storage,
                                                  int key_type, int unique);

/**
 * Insert value in index
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @param key pointer to the value of the key
 * @param key_type type of the inserted key which should match type of the index
 * @param key_size size of the string key
 * @param obj OID of the object associated with this key
 * @param replace object for existing key (if this parameter is 0, then
 * inserting object with duplicated key will cause retruning error in case of
 * unique index and presence of more than one record with the same value of the
 * key in case of non unique index).
 * @return 1 if object is successfully inserted, 0 if index is unique and eky is
 * already presebnt in the index
 */
int DYBASE_DLL_ENTRY dybase_insert_in_index(dybase_storage_t storage,
                                            dybase_oid_t index, void *key,
                                            int key_type, int key_size,
                                            dybase_oid_t obj, int replace);

/**
 * Remove key from the index
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @param key pointer to the value of the key
 * @param key_type type of the inserted key which should match type of the index
 * @param key_size size of the string key
 * @param obj OID of the object associated with this key (it can be 0 if index
 * is unique)
 * @return 1 if object is successfully removed from the index, 0 if key is not
 * present in the index
 */
int DYBASE_DLL_ENTRY dybase_remove_from_index(dybase_storage_t storage,
                                              dybase_oid_t index, void *key,
                                              int key_type, int key_size,
                                              dybase_oid_t obj);

/**
 * Returns uniqueness attribute of the index.
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @return 1 if index is unique, 0 - otherwise.
 */

int DYBASE_DLL_ENTRY dybase_is_index_unique(dybase_storage_t storage,
                                            dybase_oid_t     index);

/**
 * Returns type of the index.
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @return type of the index.
 */

int DYBASE_DLL_ENTRY dybase_get_index_type(dybase_storage_t storage,
                                           dybase_oid_t index);



/**
 * Perform index search
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @param key_type type of the inserted key which should match type of the index
 * @param min_key pointer to the low boundary of key value (if NULL then there
 * is no low boundary)
 * @param min_key_size optional size of low boundary key value (for string type)
 * @param min_key_inclusive is low boundary inclusive or not
 * @param max_key pointer to the high boundary of key value (if NULL then there
 * is no high boundary)
 * @param min_key_size optional size of high boundary key value (for string
 * type)
 * @param max_key_inclusive is high boundary inclusive or not
 * @param selected_objects pointer to the location to receive pointer to the
 * array of selected objects, if number of selected objects is greater than 0,
 * then the buffer should be deallocated by dybase_free_seletion
 * @return number of the selected objects
 */
int DYBASE_DLL_ENTRY dybase_index_search(
    dybase_storage_t storage, dybase_oid_t index, int key_type, void *min_key,
    int min_key_size, int min_key_inclusive, void *max_key, int max_key_size,
    int max_key_inclusive, dybase_oid_t **selected_objects);

/**
 * Deallocate selection
 * @param storage pointer to the opened storage
 * @param selected_objects array of selected objects returned by
 * dybase_index_search
 * @param n_selected number of selected objects
 */
void DYBASE_DLL_ENTRY dybase_free_selection(dybase_storage_t storage,
                                            dybase_oid_t *   selected_objects,
                                            int              n_selected);

/**
 * Drop index
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 */
void DYBASE_DLL_ENTRY dybase_drop_index(dybase_storage_t storage,
                                        dybase_oid_t     index);

/**
 * Remove all entries from the index
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 */
void DYBASE_DLL_ENTRY dybase_clear_index(dybase_storage_t storage,
                                         dybase_oid_t     index);

/**
 * Create index iterator. Iterator is used for traversing all index members in
 * key ascending order
 * @param storage pointer to the opened storage
 * @param index OID of index created by dybase_create_index
 * @param key_type type of the inserted key which should match type of the index
 * @param min_key pointer to the low boundary of key value (if NULL then there
 * is no low boundary)
 * @param min_key_size optional size of low boundary key value (for string type)
 * @param min_key_inclusive is low boundary inclusive or not
 * @param max_key pointer to the high boundary of key value (if NULL then there
 * is no high boundary)
 * @param min_key_size optional size of high boundary key value (for string
 * type)
 * @param max_key_inclusive is high boundary inclusive or not
 * @param ascent if true, then iterate in key ascending order
 * @return new index iterator
 */
dybase_iterator_t DYBASE_DLL_ENTRY dybase_create_index_iterator(
    dybase_storage_t storage, dybase_oid_t index, int key_type, void *min_key,
    int min_key_size, int min_key_inclusive, void *max_key, int max_key_size,
    int max_key_inclusive, int ascent);

/**
 * Get next element
 * @param iterator index iterator
 * @return oid of next object in the index or 0 if there are no more objects
 */
dybase_oid_t DYBASE_DLL_ENTRY
             dybase_index_iterator_next(dybase_iterator_t iterator);

/**
 * Free index iterator
 * @param iterator index iterator
 */
void DYBASE_DLL_ENTRY dybase_free_index_iterator(dybase_iterator_t iterator);

/**
 * Set garbage collection threshold.
 * By default garbage collection is disable (threshold is set to 0).
 * If it is set to non zero value, GC will be started each time when
 * delta between total size of allocated and deallocated objects exceeds
 * specified threshold OR after reaching end of allocation bitmap in allocator.
 * @param allocated_delta delta between total size of allocated and deallocated
 * object since last GC or storage opening
 */
void DYBASE_DLL_ENTRY dybase_set_gc_threshold(dybase_storage_t storage,
                                              long             allocated_delta);

/**
 * Explicit start of garbage collector
 */
void DYBASE_DLL_ENTRY dybase_gc(dybase_storage_t storage);


hashtable_t DYBASE_DLL_ENTRY hashtable_create();
void       DYBASE_DLL_ENTRY hashtable_put(hashtable_t ht, void *key, int keySize, void *value);
void*      DYBASE_DLL_ENTRY hashtable_get(hashtable_t ht, void *key, int keySize);
void*      DYBASE_DLL_ENTRY hashtable_remove(hashtable_t ht, void *key, int keySize);
void       DYBASE_DLL_ENTRY hashtable_clear(hashtable_t ht);

typedef int each_cb_t(void* key, unsigned int key_length, void* data, void* opaque);

void       DYBASE_DLL_ENTRY hashtable_each(hashtable_t ht, each_cb_t* pcb, void* opaque);
void       DYBASE_DLL_ENTRY hashtable_free(hashtable_t ht);


#ifdef __cplusplus
}
#endif

#endif
