

/*
 * QuickJS C library
 * 
 * Copyright (c) 2017-2020 Fabrice Bellard
 * Copyright (c) 2017-2020 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <assert.h>
#include "../cutils.h"
#include "dybase/include/dybase.h"
#include "quickjs-storage.h"

enum {
  __JS_ATOM_NULL = JS_ATOM_NULL,
#define DEF(name, str) JS_ATOM_ ## name,
#include "../quickjs-atom.h"
#undef DEF
  JS_ATOM_END,
};

JSValue JS_GetObjectClassName(JSContext *ctx, JSValueConst obj);
JSValue JS_GetLocalValue(JSContext *ctx, JSAtom name);

typedef struct JSStorage {
  dybase_storage_t hs;
  JSContext*       ctx;
  hashtable_t      oid2obj;
  JSValue          classname2proto;
  JSValue          root;
} JSStorage;

static JSClassID js_storage_class_id = 0;
static JSClassID js_index_class_id = 0;
static JSClassID js_index_iterator_class_id = 0;

void errHandler(int error_code, char const *msg) {
  fprintf(stderr, "Storage error: %d - '%s'\n", error_code, msg );
  //CsThrowKnownError(VM::get_current(), CsErrPersistError, msg);
}


#define JS_CLASS_OBJECT 1
#define JS_CLASS_ARRAY 2

JS_BOOL js_is_persitable(JSValue val) {
  JSClassID cid = JS_GetClassID(val, NULL);
  return cid == JS_CLASS_OBJECT || cid == JS_CLASS_ARRAY || cid == js_index_class_id;
}

JS_BOOL js_set_persistent_rt(JSRuntime* rt, JSValue val, struct JSStorage* pst, uint32_t oid, JS_PERSISTENT_STATUS status);
JS_BOOL js_set_persistent(JSContext* ctx, JSValue val, struct JSStorage* pst, uint32_t oid, JS_PERSISTENT_STATUS status);
void js_set_persistent_status(JSValue val, JS_PERSISTENT_STATUS status);
JS_PERSISTENT_STATUS js_is_persistent(JSValue val, JSStorage** pstor, uint32_t* poid);
uint32_t* js_get_persistent_oid_ref(JSValue val);

static JSStorage* storage_of(JSValue obj) { return JS_GetOpaque(obj, js_storage_class_id); }

static dybase_oid_t db_persist_entity(JSContext *ctx, JSStorage* pst, JSValue obj);

JS_BOOL db_is_index(JSValue val);

typedef unsigned char byte;

typedef union db_data {
  int32_t  i;
  double   d;
  byte *   s; // string
  int64_t  i64;
  dybase_oid_t oid;
} db_data;

typedef struct db_triplet {
  db_data      data;
  int32_t      type;
  int32_t      len;
} db_triplet;

static void *keyptr(db_triplet *tri) {
  if (tri->type == dybase_chars_type || tri->type == dybase_bytes_type)
    return (void *)tri->data.s;
  return (void *)&tri->data;
}

void db_transform(JSContext *ctx, JSStorage* pst, JSValueConst val, db_triplet *pt) {

  pt->len = 0;
  pt->data.i64 = 0;

  size_t size;
  double ms1970;

  switch (JS_VALUE_GET_NORM_TAG(val))
  {
    case JS_TAG_INT:
      pt->data.i = JS_VALUE_GET_INT(val);
      pt->type = dybase_int_type;
      break;
    case JS_TAG_BIG_INT:
      JS_ToBigInt64(ctx, &pt->data.i64, val);
      pt->type = dybase_long_type;
      break;
    case JS_TAG_BOOL:
      pt->data.i = val == JS_TRUE;
      pt->type = dybase_bool_type;
      break;
    case JS_TAG_NULL:
    case JS_TAG_UNDEFINED:
      pt->type = dybase_chars_type; // null is null string
      pt->len = 0;
      pt->data.i64 = 0;
      break;
    case JS_TAG_FLOAT64:
      JS_ToFloat64(ctx,&pt->data.d,val);
      pt->type = dybase_real_type;
      break;
    case JS_TAG_STRING:
    {
      size_t len;
      const char *str = JS_ToCStringLen(ctx, &len, val);
      pt->len = len; 
      pt->data.s = (byte*)str;
      pt->type = dybase_chars_type;
      break;
    }
    case JS_TAG_OBJECT: 
    if (JS_IsArray(ctx, val)) {
      pt->type = dybase_array_ref_type;
      pt->data.oid = db_persist_entity(ctx, pst, val);
    }
    else if (db_is_index(val)) {
      pt->type = dybase_index_ref_type;
      JSStorage* ipst;
      if (!js_is_persistent(val, &ipst, &pt->data.oid) || (ipst != pst))
        assert(0);
    }
    else if (JS_GetArrayBuffer(ctx, &size, val)) {
      uint8_t * ptr = JS_GetArrayBuffer(ctx, &size, val);
      pt->len = size; // length in bytes + 1 byte for the type
      pt->data.s = ptr;
      pt->type = dybase_bytes_type;
    }
    else if(JS_IsObjectPlain(ctx,val))
    {
      pt->type = dybase_object_ref_type;
      pt->data.oid = db_persist_entity(ctx, pst, val);
    } 
    else if (JS_IsDate(ctx, val, &ms1970)) {
      int64_t ft = (int64_t)(ms1970 * 10000) + 116444736000000LL /*SEC_TO_UNIX_EPOCH*/;
      pt->type = dybase_date_type;
      pt->data.i64 = ft;
    }
    break;
  }
  return;
}

void db_free_transform(JSContext *ctx, db_triplet *pt) {
  if(pt->type == dybase_chars_type)
    JS_FreeCString(ctx, pt->data.s);
  return;
}

void db_store_field(JSContext *ctx, JSStorage* pst, dybase_handle_t h, JSValueConst val)
{
  db_triplet db_val;
  db_transform(ctx, pst, val, &db_val);
  dybase_store_array_element(h, 
    db_val.type, 
    keyptr(&db_val),
    db_val.len);
  db_free_transform(ctx, &db_val);
}

void db_store_atom(JSContext *ctx, JSStorage* pst, dybase_handle_t h, JSAtom atom)
{
  db_triplet db_val;

  char buf[1024];
  const char *str = JS_AtomGetStr(ctx, &buf[1], 1024 - 2, atom);
  db_val.len = strlen(str); // length in bytes + 1 byte for the type
  db_val.data.s = (byte*)str;
  db_val.type = dybase_chars_type;

  dybase_store_array_element(h, db_val.type, (void *)db_val.data.s, db_val.len);
}

void db_store_object_data(JSContext *ctx, JSStorage* pst, dybase_oid_t oid, JSValue obj) {

  JSPropertyEnum* tab = NULL;
  uint32_t len = 0;

  JSValue cname = JS_GetObjectClassName(ctx, obj);

  const char *class_name = ""; 

  // trying to get name of custom class (if any)

  if (JS_IsString(cname))
    class_name = JS_ToCString(ctx, cname);
 
  dybase_handle_t h = dybase_begin_store_object(pst->hs, oid, class_name);
  assert(h);

  if(class_name[0])
   JS_FreeCString(ctx, class_name);

  JS_FreeValue(ctx, cname);

  JS_GetOwnPropertyNames(ctx, &tab, &len, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);

  dybase_store_object_field(h, ".", dybase_map_type, 0, len);
  
  for (uint32_t n = 0; n < len; ++n) {
    db_store_atom(ctx, pst, h, tab[n].atom);
    JSValue val = JS_GetProperty(ctx, obj, tab[n].atom);
    db_store_field(ctx, pst, h, val);
    JS_FreeValue(ctx, val);
    JS_SetProperty(ctx, obj, tab[n].atom, JS_UNDEFINED); // to free sub-items to make obj dormant
  }

  dybase_end_store_object(h);

  js_free_prop_enum(ctx, tab, len);

  js_set_persistent_status(obj, JS_PERSISTENT_DORMANT);
}

void db_store_array_data(JSContext *ctx, JSStorage* pst, dybase_oid_t oid, JSValue obj) {
  
  uint32_t len = 0;

  dybase_handle_t h = dybase_begin_store_object(pst->hs, oid, "");
  assert(h);

  uint64_t length = 0;

  JS_GetPropertyLength(ctx, (int64_t*)&length, obj);

  if (length > UINT32_MAX) 
    length = UINT32_MAX;

  dybase_store_object_field(h, ".", dybase_array_type, 0, (int)length);

  for (uint32_t n = 0; n < length; ++n) {
    JSValue val = JS_GetPropertyUint32(ctx,obj,n);
    db_store_field(ctx, pst, h, val);
    JS_FreeValue(ctx, val);
    JS_SetPropertyUint32(ctx, obj, n, JS_UNDEFINED); // to free references
  }

  dybase_end_store_object(h);

  js_set_persistent_status(obj, JS_PERSISTENT_DORMANT);
}

void db_store_entity(JSContext *ctx, JSStorage* pst, dybase_oid_t oid, JSValue obj) {

  JS_PERSISTENT_STATUS status = js_is_persistent(obj, NULL, NULL);
  if (status != JS_PERSISTENT_MODIFIED)
    return;
  if (JS_IsArray(ctx, obj))
    db_store_array_data(ctx, pst, oid, obj);
  else if (JS_IsObjectPlain(ctx, obj)) // pure Object
    db_store_object_data(ctx, pst, oid, obj);
  else if (db_is_index(obj))
    ;
  else 
    assert(0);
}

JSValue db_fetch_value(JSContext *ctx, JSStorage* pst, dybase_handle_t h);
JSAtom  db_fetch_atom(JSContext *ctx, JSStorage* pst, dybase_handle_t h);


int db_fetch_object_data(JSContext *ctx, JSValue obj, JSStorage* pst, dybase_oid_t oid) {

  int pf = js_is_persistent(obj, NULL, NULL);

  if (pf >= JS_PERSISTENT_LOADED)
    return 0; // already loaded, nothing to do.

  dybase_handle_t h = dybase_begin_load_object(pst->hs, oid);

  char *class_name = dybase_get_class_name(h);
  if (!class_name) {
    assert(0);
    return -1;
  } 
  if (class_name[0]) { /* custom */
    JSValue proto = JS_UNDEFINED;
    JSAtom  cname = JS_NewAtom(ctx, class_name);
    if (pst->classname2proto == JS_UNINITIALIZED)
      pst->classname2proto = JS_NewObject(ctx);
    else
      proto = JS_GetProperty(ctx, pst->classname2proto, cname);
    if (proto == JS_UNDEFINED) {
      JSValue cls = JS_GetLocalValue(ctx, cname);
      if (JS_IsConstructor(ctx,cls))
        proto = JS_GetProperty(ctx, cls, JS_ATOM_prototype);
      JS_FreeValue(ctx, cls);
    }
    if (proto != JS_UNDEFINED) {
      JS_SetPrototype(ctx, obj, proto);
      JS_SetProperty(ctx, pst->classname2proto, cname,proto);
    }
    JS_FreeAtom(ctx, cname);
  }

  char *field_name = dybase_next_field(h);
  if (!field_name) {
    assert(0);
    return -1;
  }

  int type;
  void *value_ptr = NULL;
  int   value_length = 0;
  dybase_get_value(h, &type, &value_ptr, &value_length);

  assert(type == dybase_map_type);

  for (int i = 0; i < value_length; i++) {
    dybase_next_element(h);
    JSAtom key_atom = db_fetch_atom(ctx,pst,h);
    assert(key_atom != JS_ATOM_NULL);
    dybase_next_element(h);
    JSValue val = db_fetch_value(ctx, pst, h);
    JS_SetProperty(ctx, obj, key_atom, val);
    JS_FreeAtom(ctx, key_atom);
    //??? JS_FreeValue(ctx, val);
  }

  dybase_end_load_object(h);

  js_set_persistent_status(obj, JS_PERSISTENT_LOADED); // drop DORMANT flag

  return 1;

}

int db_fetch_array_data(JSContext *ctx, JSValue obj, JSStorage* pst, dybase_oid_t oid) {
  
  int pf = js_is_persistent(obj, NULL, NULL);

  if (pf >= JS_PERSISTENT_LOADED)
    return 0; // already loaded, nothing to do.

  dybase_handle_t h = dybase_begin_load_object(pst->hs, oid);
  assert(h);

  char *className = dybase_get_class_name(h);
  if (!className) {
    assert(0);
    dybase_end_load_object(h);
    return -1;
  }

  char *fieldName = dybase_next_field(h);
  if (!fieldName) {
    assert(0);
    dybase_end_load_object(h);
    return -1;
  }

  int   type;
  void *value_ptr = NULL;
  int   value_length = 0;
  dybase_get_value(h, &type, &value_ptr, &value_length);

  assert(type == dybase_array_type);

  for (int i = 0; i < value_length; i++) {
    dybase_next_element(h);
    JSValue el = db_fetch_value(ctx,pst,h);
    JS_SetPropertyInt64(ctx, obj, i, el);
  }

  dybase_end_load_object(h);

  js_set_persistent_status(obj, JS_PERSISTENT_LOADED); // drop DORMANT flag

  return 1;
}

JSValue db_fetch_object(JSContext *ctx, JSStorage* pst, dybase_oid_t oid)
{
  JSValue rv = JS_NewObject(ctx);
  if(js_set_persistent(ctx, rv, pst, oid, JS_PERSISTENT_DORMANT))
    hashtable_put(pst->oid2obj, &oid, sizeof(oid), JS_VALUE_GET_PTR(rv));
  return rv;
}

JSValue db_fetch_array(JSContext *ctx, JSStorage* pst, dybase_oid_t oid)
{
  JSValue rv = JS_NewArray(ctx);
  if(js_set_persistent(ctx, rv, pst, oid, JS_PERSISTENT_DORMANT))
    hashtable_put(pst->oid2obj, &oid, sizeof(oid), JS_VALUE_GET_PTR(rv));
  return rv;
}

JSAtom db_fetch_atom(JSContext *ctx, JSStorage* pst, dybase_handle_t h) {
  int type;
  void *value_ptr = NULL;
  int   value_length = 0;
  dybase_get_value(h, &type, &value_ptr, &value_length);
  if (type != dybase_chars_type)
    return JS_ATOM_NULL;
  if (!value_length)
    return JS_ATOM_NULL;
  return JS_NewAtomLen(ctx, (const char *)value_ptr, value_length);
}

JS_BOOL db_check_cache(JSContext *ctx, JSStorage* pst, dybase_oid_t oid, JSValue* pval)
{
  struct JSObject* pobj = hashtable_get(pst->oid2obj, &oid, sizeof(oid));
  if (!pobj)
    return 0;
  *pval = JS_MKPTR(JS_TAG_OBJECT, pobj);
  return 1;
}

// loads object (plain JS object) from storage by its oid
JSValue db_load_object(JSContext *ctx, JSStorage* pst, dybase_oid_t oid)
{
  JSValue obj;
  if (db_check_cache(ctx, pst, oid, &obj))
    return JS_DupValue(ctx,obj);
  obj = db_fetch_object(ctx, pst, oid);
  hashtable_put(pst->oid2obj, js_get_persistent_oid_ref(obj), sizeof(oid), JS_VALUE_GET_PTR(obj));
  return obj;
}

static JSValue db_load_index(JSContext *ctx, JSStorage* pst, dybase_oid_t index_oid, int force_new);

JSValue db_fetch_value(JSContext *ctx, JSStorage* pst, dybase_handle_t h) {

  int type;
  void *value_ptr = NULL;
  int   value_length = 0;
  dybase_get_value(h, &type, &value_ptr, &value_length);

  dybase_oid_t oid = 0;
  JSValue      obj = JS_UNDEFINED;

  switch (type) {
  case dybase_object_ref_type: {
    oid = *((dybase_oid_t *)value_ptr);
    if (db_check_cache(ctx, pst, oid, &obj)) 
      return JS_DupValue(ctx,obj);
    obj = db_fetch_object(ctx, pst, oid);
    break;
  }
  case dybase_array_ref_type: {
    oid = *((dybase_oid_t *)value_ptr);
    if (db_check_cache(ctx, pst, oid, &obj)) 
      return JS_DupValue(ctx, obj);
    obj = db_fetch_array(ctx, pst, oid);
    break;
  }
  case dybase_index_ref_type: {
    oid = *((dybase_oid_t *)value_ptr);
    return db_load_index(ctx, pst, oid, FALSE);
  }
  case dybase_bool_type: 
    return JS_NewBool(ctx,*((char *)value_ptr));
      
  case dybase_int_type: 
    return JS_NewInt32(ctx,*((int *)value_ptr));

  case dybase_date_type: {
    int64_t wtime = *((int64_t *)value_ptr);
    double  date_ms1970 = wtime / 10000.0 - 11644473600LL /*SEC_TO_UNIX_EPOCH*/;
    return JS_NewDate(ctx, date_ms1970);
  }
  case dybase_long_type: 
    return JS_NewBigInt64(ctx,*((int64_t *)value_ptr));

  case dybase_real_type:
    return JS_NewFloat64(ctx, *(double *)value_ptr);

  case dybase_chars_type:
    if (!value_length)
      return JS_NULL;
    else
      return JS_NewStringLen(ctx, (const char *)value_ptr, value_length);
    break;

  case dybase_bytes_type:
    if (!value_length)
      return JS_NULL;
    else
      return JS_NewArrayBufferCopy(ctx, (const byte *)value_ptr, value_length);
    break;

  //
  case dybase_map_type:
    // element couldn't be a map
    // map =(by default) to object
    return JS_EXCEPTION;

  default: 
    return JS_EXCEPTION;
  }

  hashtable_put(pst->oid2obj, js_get_persistent_oid_ref(obj), sizeof(oid), JS_VALUE_GET_PTR(obj));

  return obj;
}

int db_fetch_entity(JSContext *ctx, JSValue obj) 
{
  dybase_oid_t oid;
  JSStorage*   pst;

  if (!js_is_persistent(obj, &pst, &oid))
    return -1;

  assert(js_is_persitable(obj));

  int r = 0;

  if (JS_IsArray(ctx, obj))
    r = db_fetch_array_data(ctx, obj, pst, oid);
  else if (db_is_index(obj))
    r = 1;
  else if (JS_IsObjectPlain(ctx, obj)) // pure Object
    r = db_fetch_object_data(ctx, obj, pst, oid);
  else {
    assert(0);
    r = -1;
  }

  return r;
}

dybase_oid_t db_persist_entity(JSContext *ctx, JSStorage* pst, JSValue obj) {

  assert(js_is_persitable(obj));

  dybase_oid_t oid;
  JSStorage* vps;

  if (js_is_persistent(obj, &vps, &oid)) {
    if (vps == pst) { // already here
      //js_set_persistent_status(obj, status);
      return oid;
    }
    // it is attached to another storage
    if (vps) {
      db_store_entity(ctx, vps, oid, obj);
      hashtable_remove(vps->oid2obj, &oid, sizeof(oid));
      js_set_persistent(ctx, obj, NULL, 0, JS_NOT_PERSISTENT);
    }
  }

  oid = dybase_allocate_object(pst->hs);
  if(!oid)
    return 0;

  assert(oid);

  if (js_set_persistent(ctx, obj, pst, oid, JS_PERSISTENT_MODIFIED /*to force its saving*/))
  {
    hashtable_put(pst->oid2obj, js_get_persistent_oid_ref(obj), sizeof(oid), JS_VALUE_GET_PTR(obj));
    return oid;
  } else 
    return 0;
}

static JSValue db_storage_open(JSContext *ctx, JSValueConst this_val,  int argc, JSValueConst *argv)
{
  const char *filename = NULL;
  int mode;

  filename = JS_ToCString(ctx, argv[0]);
  if (!filename)
    goto fail;
  
  mode = argv[1] == JS_UNDEFINED ? 1 : JS_ToBool(ctx, argv[1]);
  if (!mode < 0)
    goto fail;
  
  dybase_storage_t hs = dybase_open(filename, 4 * 1024 * 1024, errHandler, mode != 0);

  if(!hs)
    goto fail;

  if (mode) dybase_gc(hs);

  JSStorage* pst = js_mallocz(ctx, sizeof(JSStorage));

  pst->hs = hs;
  pst->oid2obj = hashtable_create();
  pst->root = JS_NULL;
  pst->classname2proto = JS_UNINITIALIZED;
  pst->ctx = JS_DupContext(ctx);

  JSValue obj = JS_NewObjectClass(ctx, js_storage_class_id);

  JS_SetOpaque(obj, pst);
  
  JS_FreeCString(ctx, filename);

  dybase_oid_t root_oid = dybase_get_root_object(hs);
  if (root_oid) {
    JSValue root = JS_NewObject(ctx);
    if (js_set_persistent(ctx, root, pst, root_oid, JS_PERSISTENT_DORMANT))
    {
      hashtable_put(pst->oid2obj, js_get_persistent_oid_ref(root), sizeof(dybase_oid_t), JS_VALUE_GET_PTR(root));
      pst->root = root;
    } else 
      JS_FreeValue(ctx, root);
  }
  return obj;

fail:
  JS_FreeCString(ctx, filename);
  return JS_EXCEPTION;
}


JSStorage* get_storage(JSValueConst obj) {
  return (JSStorage*) JS_GetOpaque(obj, js_storage_class_id);
}

typedef struct commit_ctx {
  JSContext *ctx;
  JSStorage *pst;
  int        forget;
  int        count;
} commit_ctx;

static int commit_value(void* key, unsigned int key_length, void* data, void* opaque) {
  JSValue obj = JS_MKPTR(JS_TAG_OBJECT, data);
  commit_ctx *cc = (commit_ctx *)opaque;
  dybase_oid_t *poid = (dybase_oid_t *)key;
  db_store_entity(cc->ctx, cc->pst, *poid, obj);
  return 0;
}

static int final_commit_value(void* key, unsigned int key_length, void* data, void* opaque) {
  JSValue obj = JS_MKPTR(JS_TAG_OBJECT, data);
  commit_ctx *cc = (commit_ctx *)opaque;
  dybase_oid_t *poid = (dybase_oid_t *)key;
  db_store_entity(cc->ctx, cc->pst, *poid, obj);
  js_set_persistent(cc->ctx, obj, NULL, 0, JS_NOT_PERSISTENT);
  ++cc->count;
  return 0;
}

static void commit_storage(JSContext *ctx, JSStorage* pst) {
  commit_ctx cc = { ctx,pst,1};
  hashtable_each(pst->oid2obj, &commit_value, &cc);
}

static void final_commit_storage(JSContext *ctx, JSStorage* pst) {
  commit_ctx cc = { ctx,pst,1, 0 };
  do {
    cc.count = 0;
    hashtable_t ht = pst->oid2obj;
    pst->oid2obj = hashtable_create();
    hashtable_each(ht, &final_commit_value, &cc);
    hashtable_free(ht);
  } while (cc.count);
}

void free_storage(JSValue st) 
{
  JSStorage* ps = storage_of(st);
  if (!ps) return;
  JSContext *ctx = ps->ctx;
  js_set_persistent(ctx, ps->root, NULL, 0, JS_NOT_PERSISTENT);
  JS_FreeValue(ctx, ps->root);
  JS_FreeValue(ctx, ps->classname2proto);
  final_commit_storage(ctx, ps);
  dybase_commit(ps->hs);
  dybase_close(ps->hs);
  ps->hs = 0;
  hashtable_free(ps->oid2obj);
  JS_FreeContext(ctx);
  js_free(ctx, ps);
  JS_SetOpaque(st, NULL);
}

static JSValue db_storage_commit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSStorage* ps = storage_of(this_val);
  if (!ps) return JS_EXCEPTION;
  commit_storage(ctx, ps);
  dybase_commit(ps->hs);
  return JS_UNDEFINED;
}

static JSValue db_storage_create_index(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue db_storage_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  JSStorage* ps = get_storage(this_val);
  if (!ps) return JS_UNDEFINED;
  free_storage(this_val);
  return JS_TRUE;
}

static JSValue db_storage_get_root(JSContext *ctx, JSValueConst this_val)
{
  JSStorage* ps = get_storage(this_val);
  if (!ps)
    return JS_NULL;
  return JS_DupValue(ctx, ps->root);
}

static JSValue db_storage_set_root(JSContext *ctx, JSValueConst this_val, JSValueConst val)
{
  JSStorage* pst = get_storage(this_val);
  if (!pst)
    return JS_EXCEPTION;
  JS_FreeValue(ctx, pst->root);
  pst->root = JS_DupValue(ctx, val);
  dybase_oid_t oid = db_persist_entity(ctx, pst, val);
  dybase_set_root_object(pst->hs, oid);
  db_store_entity(ctx, pst, oid, val); // need this immediately ?
  return JS_UNDEFINED;
}

int js_load_persistent_object(JSContext *ctx, JSValueConst obj) {
  return db_fetch_entity(ctx, obj);
}

int js_free_persistent_object(JSRuntime *rt, JSValueConst obj) {
  JSStorage* pst;
  dybase_oid_t oid;
  JS_PERSISTENT_STATUS status = js_is_persistent(obj, &pst, &oid);
  if (pst) {
    if(status == JS_PERSISTENT_MODIFIED)
       db_store_entity(pst->ctx, pst, oid, obj);
    hashtable_remove(pst->oid2obj, &oid, sizeof(oid));
    js_set_persistent_rt(rt, obj, pst, 0, JS_NOT_PERSISTENT);
  }
  return 0;
}

static void js_storage_finalizer(JSRuntime *rt, JSValue val)
{
  free_storage(val);
}

static void js_storage_mark(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func)
{
  JSStorage *pst = get_storage(val);
  if (pst) {
    JS_MarkValue(rt, pst->root, mark_func);
    //JS_MarkValue(rt, pst->oid2obj, mark_func);
    JS_MarkValue(rt, pst->classname2proto, mark_func);
  }
}


static const JSCFunctionListEntry js_storage_funcs[] = {
  JS_CFUNC_DEF("open", 2, db_storage_open),
  //JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_CONFIGURABLE),
  //JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_CONFIGURABLE),
  //JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_CONFIGURABLE),
  //JS_OBJECT_DEF("Error", js_std_error_props, countof(js_std_error_props), JS_PROP_CONFIGURABLE),
};

static const JSCFunctionListEntry js_storage_proto_funcs[] = {
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Storage", JS_PROP_CONFIGURABLE),
  JS_CFUNC_DEF("close", 0, db_storage_close),
  JS_CFUNC_DEF("commit", 0, db_storage_commit),
  JS_CFUNC_DEF("createIndex", 0, db_storage_create_index),
  JS_CGETSET_DEF("root", db_storage_get_root, db_storage_set_root),
};

static JSClassDef js_storage_class = {
    "Storage",
    .finalizer = js_storage_finalizer,
    .gc_mark = js_storage_mark
};

//|
//| class Storage.Index
//|

JS_BOOL db_is_index(JSValue val) {
  return JS_GetClassID(val,NULL) == js_index_class_id;
}

static JSValue db_load_index(JSContext *ctx, JSStorage* pst, dybase_oid_t index_oid, int force_new) 
{
  JSValue obj;
  if (!force_new) {
    if (db_check_cache(ctx, pst, index_oid, &obj))
      return JS_DupValue(ctx, obj);
  }

  obj = JS_NewObjectClass(ctx, js_index_class_id);

  js_set_persistent(ctx, obj, pst, index_oid, JS_PERSISTENT_LOADED);

  hashtable_put(pst->oid2obj, js_get_persistent_oid_ref(obj), sizeof(index_oid), JS_VALUE_GET_PTR(obj));

  return obj;
}

static JSValue db_storage_create_index(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  JSStorage* pst = get_storage(this_val);
  if (!pst)
    return JS_EXCEPTION;

  dybase_oid_t oid_idx = 0;

  const char* type = JS_ToCString(ctx, argv[0]);

  int key_type = 0;

  if (strcmp(type, "string") == 0) key_type = dybase_chars_type;
  else if (strcmp(type, "integer") == 0) key_type = dybase_int_type;
  else if (strcmp(type, "long") == 0) key_type = dybase_long_type;
  else if (strcmp(type, "float") == 0) key_type = dybase_real_type;
  else if (strcmp(type, "date") == 0) key_type = dybase_date_type;
  else {
    JS_FreeCString(ctx, type);
    return JS_ThrowTypeError(ctx, "invalid Index type");
  }
  JS_FreeCString(ctx, type);

  int unique = 1;

  if (argc > 1)
    unique = JS_ToBool(ctx, argv[1]);

  dybase_oid_t oid = dybase_create_index(pst->hs, key_type, unique);

  return db_load_index(ctx, pst, oid, TRUE);
}

// for unique indexes: either object or undefined 
// for non-unique indexes: [object1,... objectN] or [] 

static JSValue db_index_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {

  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  db_triplet db_key;
  db_transform(ctx,pst,argv[0],&db_key);

  dybase_oid_t *selected_objects = NULL;

  int num_selected = dybase_index_search( pst->hs, index_oid, db_key.type,
    keyptr(&db_key),
    db_key.len, 1 /*min_key_inclusive*/,
    keyptr(&db_key),
    db_key.len, 1 /*min_key_inclusive*/, &selected_objects);
  
  JSValue val;
  if (dybase_is_index_unique(pst->hs, index_oid))
    // return the 1st element
    val = num_selected ? db_load_object(ctx, pst, selected_objects[0]) : JS_UNDEFINED;
  else {
    val = JS_NewArray(ctx);
    for (int n = 0; n < num_selected; ++n) {
      JSValue el = db_load_object(ctx, pst, selected_objects[n]);
      JS_SetPropertyInt64(ctx, val, n, el);
    }
  }
  // free selected array
  dybase_free_selection(pst->hs, selected_objects, num_selected);
  return val;
}

static JSValue db_index_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) 
{
  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  if (!JS_IsObjectPlain(ctx,argv[1]))
    return JS_ThrowTypeError(ctx,"index can contain only plain objects");

  dybase_oid_t oid = db_persist_entity(ctx, pst, argv[1]);
  db_store_entity(ctx, pst, oid, argv[1]);

  int replace = JS_ToBool(ctx, argv[2]) > 0;

  // transform 'key' into triplet
  db_triplet db_key;
  db_transform(ctx,pst, argv[0], &db_key);
      
  int ret = dybase_insert_in_index(pst->hs, index_oid, keyptr(&db_key), db_key.type, db_key.len, oid, replace);

  db_free_transform(ctx, &db_key);

  return JS_NewBool(ctx, ret);
}

static JSValue db_index_delete(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  return JS_UNDEFINED;
}

static JSValue db_create_index_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue db_index_select(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

static JSValue db_index_clear(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  dybase_clear_index(pst->hs, index_oid);

  return JS_UNDEFINED;
}

static JSValue db_index_get_length(JSContext *ctx, JSValueConst this_val) {
  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  dybase_oid_t *selected_objects = NULL;

  int key_type = dybase_get_index_type(pst->hs, index_oid);

  int num_selected = dybase_index_search(pst->hs, index_oid, key_type,
    NULL, 0, 1,
    NULL, 0, 1, &selected_objects);

  dybase_free_selection(pst->hs, selected_objects, num_selected);

  return JS_NewInt32(ctx, num_selected);
}

static JSValue db_index_get_type(JSContext *ctx, JSValueConst this_val) {
  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  int key_type = dybase_get_index_type(pst->hs, index_oid);
  switch (key_type) {
    case dybase_chars_type: return JS_NewString(ctx, "string");
    case dybase_int_type: return JS_NewString(ctx, "integer");
    case dybase_long_type: return JS_NewString(ctx, "long");
    case dybase_real_type: return JS_NewString(ctx, "float");
    case dybase_date_type: return JS_NewString(ctx, "date");
    default: break;
  }
  return JS_NULL;
}

static JSValue db_index_get_unique(JSContext *ctx, JSValueConst this_val) {
  JSStorage*   pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  int u = dybase_is_index_unique(pst->hs, index_oid);
  
  return JS_NewBool(ctx,u);
}


static void js_index_finalizer(JSRuntime *rt, JSValue val)
{
  // we are not allocating anything for the index
}

static const JSCFunctionListEntry js_index_proto_funcs[] = {
  JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Storage.Index", JS_PROP_CONFIGURABLE),
  JS_CFUNC_DEF("delete", 1, db_index_delete),
  JS_CFUNC_DEF("clear", 0, db_index_clear),
  JS_CFUNC_DEF("get", 1, db_index_get),
  JS_CFUNC_DEF("set", 2, db_index_set),
  JS_CFUNC_DEF("select", 5, db_index_select),
  JS_CGETSET_DEF("length", db_index_get_length, NULL),
  JS_CGETSET_DEF("unique", db_index_get_unique, NULL),
  JS_CGETSET_DEF("type", db_index_get_type, NULL),
  JS_CFUNC_DEF("[Symbol.iterator]", 0, db_create_index_iterator),
};

static JSClassDef js_index_class = {
    "Index",
    //.finalizer = js_index_finalizer
};

typedef struct IndexIterator {
  JSStorage* pst;
  dybase_iterator_t iterator;
} IndexIterator;

static JSValue js_index_iterator_next(JSContext *ctx, JSValueConst this_val,
  int argc, JSValueConst *argv,
  BOOL *pdone, int magic) 
{
  IndexIterator *it = JS_GetOpaque2(ctx, this_val, js_index_iterator_class_id);
  if (!it)
    return JS_EXCEPTION;

  dybase_oid_t oid = dybase_index_iterator_next( it->iterator );

  if (!oid) {
    *pdone = TRUE;
    return JS_UNDEFINED;
  }

  *pdone = FALSE;
  JSValue item = db_load_object(ctx, it->pst, oid);
  return item;
}

static JSValue db_index_select(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) 
{
  JSValue enum_obj;
  IndexIterator *it;

  JSStorage   *pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  enum_obj = JS_NewObjectClass(ctx, js_index_iterator_class_id);
  if (JS_IsException(enum_obj))
    goto fail;
  it = js_malloc(ctx, sizeof(*it));
  if (!it)
    goto fail1;
  JS_SetOpaque(enum_obj, it);

  it->pst = pst;

  db_triplet start;
  db_triplet end;

  db_transform(ctx, pst, argv[0], &start);
  db_transform(ctx, pst, argv[1], &end);

  int ascending = 1; if (argc >= 3) ascending = JS_ToBool(ctx, argv[2]) > 0;
  int start_inclusive = 1; if (argc >= 4) start_inclusive = JS_ToBool(ctx, argv[3]) > 0;
  int end_inclusive = 1; if (argc >= 5) end_inclusive = JS_ToBool(ctx, argv[4]) > 0;
  
  it->iterator = dybase_create_index_iterator(
    pst->hs, index_oid, start.type,
    keyptr(&start), start.len, start_inclusive,
    keyptr(&end), end.len, end_inclusive,
    ascending);

  db_free_transform(ctx, &start);
  db_free_transform(ctx, &end);

  if(it->iterator)
    return enum_obj;
fail1:
  JS_FreeValue(ctx, enum_obj);
fail:
  return JS_EXCEPTION;
}

static JSValue db_create_index_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  JSValue enum_obj;
  IndexIterator *it;

  JSStorage   *pst;
  dybase_oid_t index_oid;

  if (!js_is_persistent(this_val, &pst, &index_oid))
    return JS_EXCEPTION;

  enum_obj = JS_NewObjectClass(ctx, js_index_iterator_class_id);
  if (JS_IsException(enum_obj))
    goto fail;
  it = js_malloc(ctx, sizeof(*it));
  if (!it)
    goto fail1;
  JS_SetOpaque(enum_obj, it);

  it->pst = pst;

  int index_type = dybase_get_index_type(pst->hs, index_oid);

  it->iterator = dybase_create_index_iterator(
    pst->hs, index_oid, index_type,
    NULL, 0, 0,
    NULL, 0, 0,
    1);

  if (it->iterator)
    return enum_obj;
fail1:
  JS_FreeValue(ctx, enum_obj);
fail:
  return JS_EXCEPTION;
}

static JSValue js_index_iterator(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
  return JS_DupValue(ctx,this_val);
}

static const JSCFunctionListEntry js_index_iterator_proto_funcs[] = {
    JS_ITERATOR_NEXT_DEF("next", 0, js_index_iterator_next, 0),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Storage.IndexIterator", JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("[Symbol.iterator]", 0, js_index_iterator),
};

static void js_index_iterator_finalizer(JSRuntime *rt, JSValue val) {
  IndexIterator *it = JS_GetOpaque(val, js_index_iterator_class_id);
  if (it) {
    dybase_free_index_iterator(it->iterator);
    js_free_rt(rt, it);
  }
}

static JSClassDef js_index_iterator_class = {
    "Storage.IndexIterator",
    .finalizer = js_index_iterator_finalizer
};

static int js_storage_init(JSContext *ctx, JSModuleDef *m)
{
  JSValue proto;

  /* Storage class */
  /* the class ID is created once */
  JS_NewClassID(&js_storage_class_id);
  /* the class is created once per runtime */
  JS_NewClass(JS_GetRuntime(ctx), js_storage_class_id, &js_storage_class);
  proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, proto, js_storage_proto_funcs, countof(js_storage_proto_funcs));
  JS_SetClassProto(ctx, js_storage_class_id, proto);
  JS_SetModuleExportList(ctx, m, js_storage_funcs, countof(js_storage_funcs));

  JS_NewClassID(&js_index_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_index_class_id, &js_index_class);
  JSValue index_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, index_proto, js_index_proto_funcs, countof(js_index_proto_funcs));
  JS_SetClassProto(ctx, js_index_class_id, index_proto);

  JS_NewClassID(&js_index_iterator_class_id);
  JS_NewClass(JS_GetRuntime(ctx), js_index_iterator_class_id, &js_index_iterator_class);
  JSValue index_iterator_proto = JS_NewObject(ctx);
  JS_SetPropertyFunctionList(ctx, index_iterator_proto, js_index_iterator_proto_funcs, countof(js_index_iterator_proto_funcs));
  JS_SetClassProto(ctx, js_index_iterator_class_id, index_iterator_proto);

  return 0;
}

JSModuleDef *js_init_module_storage(JSContext *ctx, const char *module_name)
{
  JSModuleDef *m;
  m = JS_NewCModule(ctx, module_name, js_storage_init);
  if (!m)
    return NULL;
  JS_AddModuleExportList(ctx, m, js_storage_funcs, countof(js_storage_funcs));
  return m;
}
