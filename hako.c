#include "hako.h"
#include "cutils.h"
#include "quickjs.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "version.h"
#include "wasi_version.h"
#include "ts_strip/ts_strip.h"

#define PKG "quickjs-wasi: "

#if defined(__WASI__) || defined(__wasi__)
#define WASM_EXPORT(func) __attribute__((export_name(#func))) func
#define HAKO_IMPORT(name)                                                      \
  __attribute__((import_module("hako"), import_name(name)))
#else
#define WASM_EXPORT(func) func
#define HAKO_IMPORT(name)
#endif

typedef enum {
  HAKO_MODULE_SOURCE_STRING,
  HAKO_MODULE_SOURCE_PRECOMPILED,
  HAKO_MODULE_SOURCE_ERROR
} HakoModuleSourceType;

typedef struct HakoModuleSource {
  uint32_t type;
  union {
    char *source_code;
    JSModuleDef *module_def;
  } data;
} HakoModuleSource;

HAKO_IMPORT("call_function")
JSValue *host_call_function(JSContext *ctx, JSValueConst *this_ptr, int32_t argc,
                            JSValueConst *argv, uint32_t magic_func_id);

HAKO_IMPORT("interrupt_handler")
extern int32_t host_interrupt_handler(JSRuntime *rt, JSContext *ctx, void *opaque);

HAKO_IMPORT("load_module")
extern HakoModuleSource *host_load_module(JSRuntime *rt, JSContext *ctx,
                                          const char *module_name, void *opaque,
                                          JSValueConst *attributes);

HAKO_IMPORT("normalize_module")
extern char *host_normalize_module(JSRuntime *rt, JSContext *ctx,
                                   const char *module_base_name,
                                   const char *module_name, void *opaque);

HAKO_IMPORT("module_init")
extern int32_t host_module_init(JSContext *ctx, JSModuleDef *m);

HAKO_IMPORT("class_constructor")
extern JSValue *host_class_constructor(JSContext *ctx, JSValueConst *new_target,
                                       int32_t argc, JSValueConst *argv,
                                       JSClassID class_id);

HAKO_IMPORT("class_finalizer")
extern void host_class_finalizer(JSRuntime *rt, void *opaque,
                                 JSClassID class_id);

HAKO_IMPORT("class_gc_mark")
extern void host_class_gc_mark(JSRuntime *rt, void *opaque, JSClassID class_id,
                               JS_MarkFunc *mark_func);

HAKO_IMPORT("promise_rejection_tracker")
extern void host_promise_rejection_tracker(JSContext *ctx,
                                           JSValueConst *promise,
                                           JSValueConst *reason,
                                           JS_BOOL is_handled, void *opaque);

static HakoBuildInfo build_info = {.version = HAKO_VERSION,
                                   .flags = 0x00000001,
                                   .build_date = __DATE__ " " __TIME__,
                                   .quickjs_version = QUICKJS_VERSION,
                                   .wasi_sdk_version = WASI_VERSION,
                                   .wasi_libc = WASI_WASI_LIBC,
                                   .llvm = WASI_LLVM,
                                   .config = WASI_CONFIG};

JSValueConst HAKO_Undefined = JS_UNDEFINED;
JSValueConst HAKO_Null = JS_NULL;
JSValueConst HAKO_False = JS_FALSE;
JSValueConst HAKO_True = JS_TRUE;

static inline JS_BOOL is_static_constant(const JSValue *ptr) {
  return ptr == (JSValue *)&HAKO_Undefined || ptr == (JSValue *)&HAKO_Null ||
         ptr == (JSValue *)&HAKO_False || ptr == (JSValue *)&HAKO_True;
}

static void* ts_strip_malloc_wrapper(void* user_data, size_t size) {
  JSRuntime* rt = (JSRuntime*)user_data;
  if (!rt) return NULL;
  return js_malloc_rt(rt, size);
}

static void* ts_strip_realloc_wrapper(void* user_data, void* ptr, size_t size) {
  JSRuntime* rt = (JSRuntime*)user_data;
  if (!rt) return NULL;
  return js_realloc_rt(rt, ptr, size);
}

static void ts_strip_free_wrapper(void* user_data, void* ptr) {
  JSRuntime* rt = (JSRuntime*)user_data;
  if (!rt || !ptr) return;
  js_free_rt(rt, ptr);
}

static int32_t ends_with_ts(const char *str) {
  size_t len;
  if (!str) return 0;
  
  len = strlen(str);
  if (len < 3) return 0;
  
  // Check for .ts or .mts
  if (len >= 3 && strcmp(str + len - 3, ".ts") == 0) return 1;
  if (len >= 4 && strcmp(str + len - 4, ".mts") == 0) return 1;
  if (len >= 4 && strcmp(str + len - 4, ".tsx") == 0) return 1;
  if (len >= 5 && strcmp(str + len - 5, ".mtsx") == 0) return 1;
  
  return 0;
}

static int32_t ends_with_module_extension(const char *str) {
  size_t len;
  if (!str) return 0;
  
  len = strlen(str);
  
  // Check for .mjs (JavaScript modules)
  if (len >= 4 && strcmp(str + len - 4, ".mjs") == 0) return 1;
  
  // Check for .mts (TypeScript modules)
  if (len >= 4 && strcmp(str + len - 4, ".mts") == 0) return 1;
  
  // Check for .mtsx (TypeScript JSX modules)
  if (len >= 5 && strcmp(str + len - 5, ".mtsx") == 0) return 1;
  
  return 0;
}

int32_t hako_module_set_import_meta(JSContext *ctx, JSValueConst func_val,
                                JS_BOOL use_realpath, JS_BOOL is_main) {
  JSModuleDef *m;
  char buf[1024 + 16];
  JSValue meta_obj = JS_UNDEFINED;
  JSAtom module_name_atom;
  const char *module_name = NULL;
  int32_t ret = -1;

  m = JS_VALUE_GET_PTR(func_val);
  module_name_atom = JS_GetModuleName(ctx, m);
  module_name = JS_AtomToCString(ctx, module_name_atom);
  JS_FreeAtom(ctx, module_name_atom);

  if (!module_name)
    goto done;

  if (!strchr(module_name, ':')) {
    strcpy(buf, "file://");
#if !defined(_WIN32) && !defined(__wasi__)
    if (use_realpath) {
      char *res = realpath(module_name, buf + strlen(buf));
      if (!res) {
        JS_ThrowTypeError(ctx, "realpath failure");
        goto done;
      }
    } else
#endif
    {
      pstrcat(buf, sizeof(buf), module_name);
    }
  } else {
    pstrcpy(buf, sizeof(buf), module_name);
  }

  meta_obj = JS_GetImportMeta(ctx, m);
  if (JS_IsException(meta_obj))
    goto done;

  JS_DefinePropertyValueStr(ctx, meta_obj, "url", JS_NewString(ctx, buf),
                            JS_PROP_C_W_E);
  JS_DefinePropertyValueStr(ctx, meta_obj, "main", JS_NewBool(ctx, is_main),
                            JS_PROP_C_W_E);
  ret = 0;

done:
  if (module_name)
    JS_FreeCString(ctx, module_name);
  if (!JS_IsUndefined(meta_obj))
    JS_FreeValue(ctx, meta_obj);
  return ret;
}

static JSModuleDef *hako_compile_module(JSContext *ctx, const char *module_name,
                                        const char *module_body) {
  int32_t eval_flags;
  JSValue func_val = JS_UNDEFINED;
  JSModuleDef *module = NULL;

  eval_flags =
      JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_FLAG_STRICT;

  func_val =
      JS_Eval(ctx, module_body, strlen(module_body), module_name, eval_flags);

  if (JS_IsException(func_val))
    goto done;

  if (!JS_VALUE_IS_MODULE(func_val)) {
    JS_ThrowTypeError(ctx, "Module '%s' code compiled to non-module object",
                      module_name);
    goto done;
  }

  if (hako_module_set_import_meta(ctx, func_val, TRUE, FALSE) < 0)
    goto done;

  module = JS_VALUE_GET_PTR(func_val);

done:
  if (!JS_IsUndefined(func_val))
    JS_FreeValue(ctx, func_val);
  return module;
}

static JSModuleDef *hako_load_module(JSContext *ctx, const char *module_name,
                                     void *user_data, JSValueConst attributes) {
  JSRuntime *rt;
  HakoModuleSource *module_source = NULL;
  JSModuleDef *result = NULL;
  char *source_code = NULL;

  rt = JS_GetRuntime(ctx);
  module_source =
      host_load_module(rt, ctx, module_name, user_data, &attributes);

  if (module_source == NULL) {
    JS_ThrowTypeError(
        ctx,
        "Module not found: '%s'. Please check that the module name is "
        "correct and the module is available in your environment.",
        module_name);
    goto done;
  }

  switch (module_source->type) {
  case HAKO_MODULE_SOURCE_STRING:
    source_code = (char *)module_source->data.source_code;
    if (source_code != NULL) {
      result = hako_compile_module(ctx, module_name, source_code);
    } else {
      JS_ThrowTypeError(ctx, "Invalid source code for module '%s'",
                        module_name);
    }
    break;

  case HAKO_MODULE_SOURCE_PRECOMPILED:
    result = (JSModuleDef *)module_source->data.module_def;
    if (result == NULL) {
      JS_ThrowTypeError(ctx, "Invalid precompiled module for '%s'",
                        module_name);
    }
    break;

  case HAKO_MODULE_SOURCE_ERROR:
  default:
    JS_ThrowTypeError(
        ctx,
        "Module not found: '%s'. Please check that the module name is "
        "correct and the module is available in your environment.",
        module_name);
    break;
  }

done:
  if (source_code)
    js_free(ctx, source_code);
  if (module_source)
    js_free(ctx, module_source);
  return result;
}

static char *hako_normalize_module(JSContext *ctx, const char *module_base_name,
                                   const char *module_name, void *user_data) {
  JSRuntime *rt;
  char *normalized_module_name = NULL;
  char *js_module_name = NULL;

  rt = JS_GetRuntime(ctx);
  normalized_module_name =
      host_normalize_module(rt, ctx, module_base_name, module_name, user_data);

  if (!normalized_module_name)
    goto done;

  js_module_name = js_strdup(ctx, normalized_module_name);

done:
  if (normalized_module_name)
    js_free(ctx, normalized_module_name);
  return js_module_name;
}


static JSValue *jsvalue_to_heap(JSContext *ctx, JSValueConst value) {
  JSValue *result = js_malloc(ctx, sizeof(JSValue));
  if (result) {
    *result = value;
  }
  return result;
}

JSValue *HAKO_Throw(JSContext *ctx, JSValueConst *error) {
  JSValue copy = JS_DupValue(ctx, *error);
  return jsvalue_to_heap(ctx, JS_Throw(ctx, copy));
}

JSValue *HAKO_ThrowError(JSContext *ctx, HAKO_ErrorType error_type,
                         const char *message) {
  JSValue result;

  switch (error_type) {
  case HAKO_ERROR_RANGE:
    result = JS_ThrowRangeError(ctx, "%s", message);
    break;
  case HAKO_ERROR_REFERENCE:
    result = JS_ThrowReferenceError(ctx, "%s", message);
    break;
  case HAKO_ERROR_SYNTAX:
    result = JS_ThrowSyntaxError(ctx, "%s", message);
    break;
  case HAKO_ERROR_TYPE:
    result = JS_ThrowTypeError(ctx, "%s", message);
    break;
  case HAKO_ERROR_INTERNAL:
    result = JS_ThrowInternalError(ctx, "%s", message);
    break;
  case HAKO_ERROR_OUT_OF_MEMORY:
    result = JS_ThrowOutOfMemory(ctx);
    break;
  default:
    result = JS_ThrowInternalError(ctx, "Unknown error type (%d): %s",
                                   error_type, message);
    break;
  }
  return jsvalue_to_heap(ctx, result);
}

JSValue *HAKO_NewError(JSContext *ctx) {
  return jsvalue_to_heap(ctx, JS_NewError(ctx));
}

void HAKO_RuntimeSetMemoryLimit(JSRuntime *rt, size_t limit) {
  JS_SetMemoryLimit(rt, limit);
}

JSValue *HAKO_RuntimeComputeMemoryUsage(JSRuntime *rt, JSContext *ctx) {
  JSMemoryUsage s;
  JSValue result;

  JS_ComputeMemoryUsage(rt, &s);
  result = JS_NewObject(ctx);

  JS_SetPropertyStr(ctx, result, "malloc_limit",
                    JS_NewInt64(ctx, s.malloc_limit));
  JS_SetPropertyStr(ctx, result, "malloc_size",
                    JS_NewInt64(ctx, s.malloc_size));
  JS_SetPropertyStr(ctx, result, "malloc_count",
                    JS_NewInt64(ctx, s.malloc_count));
  JS_SetPropertyStr(ctx, result, "memory_used_size",
                    JS_NewInt64(ctx, s.memory_used_size));
  JS_SetPropertyStr(ctx, result, "memory_used_count",
                    JS_NewInt64(ctx, s.memory_used_count));
  JS_SetPropertyStr(ctx, result, "atom_count", JS_NewInt64(ctx, s.atom_count));
  JS_SetPropertyStr(ctx, result, "atom_size", JS_NewInt64(ctx, s.atom_size));
  JS_SetPropertyStr(ctx, result, "str_count", JS_NewInt64(ctx, s.str_count));
  JS_SetPropertyStr(ctx, result, "str_size", JS_NewInt64(ctx, s.str_size));
  JS_SetPropertyStr(ctx, result, "obj_count", JS_NewInt64(ctx, s.obj_count));
  JS_SetPropertyStr(ctx, result, "obj_size", JS_NewInt64(ctx, s.obj_size));
  JS_SetPropertyStr(ctx, result, "prop_count", JS_NewInt64(ctx, s.prop_count));
  JS_SetPropertyStr(ctx, result, "prop_size", JS_NewInt64(ctx, s.prop_size));
  JS_SetPropertyStr(ctx, result, "shape_count",
                    JS_NewInt64(ctx, s.shape_count));
  JS_SetPropertyStr(ctx, result, "shape_size", JS_NewInt64(ctx, s.shape_size));
  JS_SetPropertyStr(ctx, result, "js_func_count",
                    JS_NewInt64(ctx, s.js_func_count));
  JS_SetPropertyStr(ctx, result, "js_func_size",
                    JS_NewInt64(ctx, s.js_func_size));
  JS_SetPropertyStr(ctx, result, "js_func_code_size",
                    JS_NewInt64(ctx, s.js_func_code_size));
  JS_SetPropertyStr(ctx, result, "js_func_pc2line_count",
                    JS_NewInt64(ctx, s.js_func_pc2line_count));
  JS_SetPropertyStr(ctx, result, "js_func_pc2line_size",
                    JS_NewInt64(ctx, s.js_func_pc2line_size));
  JS_SetPropertyStr(ctx, result, "c_func_count",
                    JS_NewInt64(ctx, s.c_func_count));
  JS_SetPropertyStr(ctx, result, "array_count",
                    JS_NewInt64(ctx, s.array_count));
  JS_SetPropertyStr(ctx, result, "fast_array_count",
                    JS_NewInt64(ctx, s.fast_array_count));
  JS_SetPropertyStr(ctx, result, "fast_array_elements",
                    JS_NewInt64(ctx, s.fast_array_elements));
  JS_SetPropertyStr(ctx, result, "binary_object_count",
                    JS_NewInt64(ctx, s.binary_object_count));
  JS_SetPropertyStr(ctx, result, "binary_object_size",
                    JS_NewInt64(ctx, s.binary_object_size));

  return jsvalue_to_heap(ctx, result);
}

char *HAKO_RuntimeDumpMemoryUsage(JSRuntime *rt) {
  char *result = NULL;
  FILE *memfile = NULL;
  JSMemoryUsage s;

  result = js_malloc_rt(rt, sizeof(char) * 1024);
  if (!result)
    goto done;

  memfile = fmemopen(result, 1024, "w");
  if (!memfile) {
    js_free_rt(rt, result);
    result = NULL;
    goto done;
  }

  JS_ComputeMemoryUsage(rt, &s);
  JS_DumpMemoryUsage(memfile, &s, rt);

done:
  if (memfile)
    fclose(memfile);
  return result;
}

void HAKO_RuntimeJSThrow(JSContext *ctx, const char *message) {
  JS_ThrowReferenceError(ctx, "%s", message);
}

void HAKO_SetMaxStackSize(JSRuntime *rt, size_t stack_size) {
  JS_SetMaxStackSize(rt, stack_size);
}

HAKO_Status HAKO_InitTypeStripper(JSRuntime* rt) {
  if (!rt) {
    return HAKO_STATUS_ERROR_INVALID_ARGS;
  }
  if (JS_GetRuntimeOpaque(rt) != NULL) {
    return HAKO_STATUS_SUCCESS;
  }
  
  ts_strip_allocator_t allocator = {
    .malloc_func = ts_strip_malloc_wrapper,
    .realloc_func = ts_strip_realloc_wrapper,
    .free_func = ts_strip_free_wrapper,
    .user_data = rt
  };
  ts_strip_ctx_t* ctx = ts_strip_ctx_new_with_allocator(&allocator);
  if (ctx == NULL) {
    return HAKO_STATUS_ERROR_OUT_OF_MEMORY;
  }
  JS_SetRuntimeOpaque(rt, ctx);
  return HAKO_STATUS_SUCCESS;
}

void HAKO_CleanupTypeStripper(JSRuntime* rt) {
  if (!rt) {
    return;
  }
  ts_strip_ctx_t* ctx = JS_GetRuntimeOpaque(rt);
  if (ctx != NULL) {
    ts_strip_ctx_delete(ctx);
    JS_SetRuntimeOpaque(rt, NULL);
  }
}

HAKO_Status HAKO_StripTypes(JSRuntime* rt, const char* typescript_source,
                            char** javascript_out,
                            size_t* javascript_len) {
  ts_strip_result_t result;
  ts_strip_ctx_t* ctx = JS_GetRuntimeOpaque(rt);
  
  if (ctx == NULL) {
    return HAKO_STATUS_ERROR_INVALID_ARGS;
  }
  
  result = ts_strip_with_ctx(ctx, typescript_source, 
                             javascript_out, javascript_len);
  
  switch (result) {
    case TS_STRIP_SUCCESS:
      return HAKO_STATUS_SUCCESS;
    case TS_STRIP_ERROR_INVALID_INPUT:
      return HAKO_STATUS_ERROR_INVALID_ARGS;
    case TS_STRIP_ERROR_PARSE_FAILED:
      return HAKO_STATUS_ERROR_PARSE_FAILED;
    case TS_STRIP_ERROR_UNSUPPORTED:
      return HAKO_STATUS_ERROR_UNSUPPORTED;
    case TS_STRIP_ERROR_OUT_OF_MEMORY:
      return HAKO_STATUS_ERROR_OUT_OF_MEMORY;
    default:
      return HAKO_STATUS_ERROR_PARSE_FAILED;
  }
}

JSValueConst *HAKO_GetUndefined(void) { return &HAKO_Undefined; }
JSValueConst *HAKO_GetNull(void) { return &HAKO_Null; }
JSValueConst *HAKO_GetFalse(void) { return &HAKO_False; }
JSValueConst *HAKO_GetTrue(void) { return &HAKO_True; }

JSRuntime *HAKO_NewRuntime(void) {
  JSRuntime *rt = JS_NewRuntime();
  if (rt == NULL)
    return NULL;

  JS_SetRuntimeInfo(rt, "HakoJS");
  return rt;
}

void HAKO_FreeRuntime(JSRuntime *rt) { JS_FreeRuntime(rt); }

void HAKO_SetStripInfo(JSRuntime *rt, int32_t flags) { JS_SetStripInfo(rt, flags); }

int32_t HAKO_GetStripInfo(JSRuntime *rt) { return JS_GetStripInfo(rt); }

JSContext *HAKO_NewContext(JSRuntime *rt, HAKO_Intrinsic intrinsics) {
  JSContext *ctx = NULL;

  if (intrinsics == 0) {
    ctx = JS_NewContext(rt);
    return ctx;
  }

  ctx = JS_NewContextRaw(rt);
  if (ctx == NULL)
    return NULL;

  if (intrinsics & HAKO_Intrinsic_BaseObjects) {
    JS_AddIntrinsicBaseObjects(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Date) {
    JS_AddIntrinsicDate(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Eval) {
    JS_AddIntrinsicEval(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_StringNormalize) {
    JS_AddIntrinsicStringNormalize(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_RegExp) {
    JS_AddIntrinsicRegExp(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_RegExpCompiler) {
    JS_AddIntrinsicRegExpCompiler(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_JSON) {
    JS_AddIntrinsicJSON(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Proxy) {
    JS_AddIntrinsicProxy(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_MapSet) {
    JS_AddIntrinsicMapSet(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_TypedArrays) {
    JS_AddIntrinsicTypedArrays(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Promise) {
    JS_AddIntrinsicPromise(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Performance) {
    JS_AddIntrinsicPerformance(ctx);
  }
  if (intrinsics & HAKO_Intrinsic_Crypto) {
    JS_AddIntrinsicCrypto(ctx);
  }
  return ctx;
}

void HAKO_SetContextData(JSContext *ctx, void *data) {
  JS_SetContextOpaque(ctx, data);
}

void *HAKO_GetContextData(JSContext *ctx) { return JS_GetContextOpaque(ctx); }

void HAKO_FreeContext(JSContext *ctx) { JS_FreeContext(ctx); }

JSValue *HAKO_DupValuePointer(JSContext *ctx, JSValueConst *val) {
  return jsvalue_to_heap(ctx, JS_DupValue(ctx, *val));
}

void HAKO_FreeValuePointer(JSContext *ctx, JSValue *value) {
  if (is_static_constant(value)) {
    fprintf(stderr, "Attempted to free static constant\n");
    __builtin_unreachable();
  }
  JS_FreeValue(ctx, *value);
  js_free(ctx, value);
}

void HAKO_FreeValuePointerRuntime(JSRuntime *rt, JSValue *value) {
  if (is_static_constant(value)) {
    fprintf(stderr, "Attempted to free static constant\n");
    __builtin_unreachable();
  }
  JS_FreeValueRT(rt, *value);
  js_free_rt(rt, value);
}

void *HAKO_Malloc(JSContext *ctx, size_t size) {
  void *ptr = NULL;

  if (size == 0)
    return NULL;

  ptr = js_malloc(ctx, size);
  if (ptr == NULL) {
    JS_ThrowOutOfMemory(ctx);
    return NULL;
  }
  return ptr;
}

void *HAKO_RuntimeMalloc(JSRuntime *rt, size_t size) {
  void *ptr = NULL;

  if (size == 0)
    return NULL;

  ptr = js_malloc_rt(rt, size);
  if (ptr == NULL) {
    __builtin_trap();
    return NULL;
  }
  return ptr;
}

void HAKO_Free(JSContext *ctx, void *ptr) { js_free(ctx, ptr); }

void HAKO_RuntimeFree(JSRuntime *rt, void *ptr) { js_free_rt(rt, ptr); }

void HAKO_FreeCString(JSContext *ctx, const char *str) {
  JS_FreeCString(ctx, str);
}

JSValue *HAKO_NewObject(JSContext *ctx) {
  return jsvalue_to_heap(ctx, JS_NewObject(ctx));
}

JSValue *HAKO_NewObjectProto(JSContext *ctx, JSValueConst *proto) {
  return jsvalue_to_heap(ctx, JS_NewObjectProto(ctx, *proto));
}

JSValue *HAKO_NewArray(JSContext *ctx) {
  return jsvalue_to_heap(ctx, JS_NewArray(ctx));
}

void hako_free_buffer(JSRuntime *rt, void *unused_opaque, void *ptr) {
  js_free_rt(rt, ptr);
}

JSValue *HAKO_NewArrayBuffer(JSContext *ctx, void *buffer, size_t length) {
  if (length == 0) {
    return jsvalue_to_heap(ctx, JS_NewArrayBuffer(ctx, NULL, 0, NULL, NULL, 0));
  }
  return jsvalue_to_heap(ctx, JS_NewArrayBuffer(ctx, (uint8_t *)buffer, length,
                                                hako_free_buffer, NULL, 0));
}

JSValue *HAKO_NewFloat64(JSContext *ctx, double num) {
  return jsvalue_to_heap(ctx, JS_NewFloat64(ctx, num));
}

double HAKO_GetFloat64(JSContext *ctx, JSValueConst *value) {
  double result = NAN;
  JS_ToFloat64(ctx, &result, *value);
  return result;
}

JSValue *HAKO_NewString(JSContext *ctx, const char *string) {
  return jsvalue_to_heap(ctx, JS_NewString(ctx, string));
}

const char *HAKO_ToCString(JSContext *ctx, JSValueConst *value) {
  return JS_ToCString(ctx, *value);
}

void *HAKO_CopyArrayBuffer(JSContext *ctx, JSValueConst *data,
                           size_t *out_length) {
  size_t length = 0;
  uint8_t *buffer = NULL;
  uint8_t *result = NULL;

  buffer = JS_GetArrayBuffer(ctx, &length, *data);
  if (!buffer) {
    if (out_length)
      *out_length = 0;
    goto done;
  }

  result = js_malloc(ctx, length);
  if (!result) {
    if (out_length)
      *out_length = 0;
    goto done;
  }

  memcpy(result, buffer, length);
  if (out_length)
    *out_length = length;

done:
  return result;
}

JSValue hako_get_symbol_key(JSContext *ctx, JSValueConst *value) {
  JSValue global = JS_UNDEFINED;
  JSValue Symbol = JS_UNDEFINED;
  JSValue Symbol_keyFor = JS_UNDEFINED;
  JSValue key = JS_UNDEFINED;

  global = JS_GetGlobalObject(ctx);
  Symbol = JS_GetPropertyStr(ctx, global, "Symbol");
  Symbol_keyFor = JS_GetPropertyStr(ctx, Symbol, "keyFor");
  key = JS_Call(ctx, Symbol_keyFor, Symbol, 1, value);

  JS_FreeValue(ctx, Symbol_keyFor);
  JS_FreeValue(ctx, Symbol);
  JS_FreeValue(ctx, global);

  return key;
}

JSValue hako_resolve_func_data(JSContext *ctx, JSValueConst this_val, int32_t argc,
                               JSValueConst *argv, int32_t magic,
                               JSValue *func_data) {
  return JS_DupValue(ctx, func_data[0]);
}

JSValue *HAKO_Eval(JSContext *ctx, const char *js_code, size_t js_code_length,
                   const char *filename, JS_BOOL detect_module,
                   int32_t eval_flags) {
  JSModuleDef *module = NULL;
  JSValue func_obj = JS_UNDEFINED;
  JSValue eval_result = JS_UNDEFINED;
  JSValue module_namespace = JS_UNDEFINED;
  JSValue then_resolve_module_namespace = JS_UNDEFINED;
  JSValue new_promise = JS_UNDEFINED;
  JSAtom then_atom = JS_ATOM_NULL;
  JSValueConst then_args[1];
  int32_t is_module;
  JSValue *result = NULL;
  char *stripped_js = NULL;
  size_t stripped_len = 0;
  const char *code_to_eval = js_code;
  size_t code_len = js_code_length;
  int32_t should_strip = 0;

  // Check if we should strip TypeScript types
  should_strip =
      (eval_flags & JS_EVAL_FLAG_STRIP_TYPES) || ends_with_ts(filename);

  if (should_strip) {
    HAKO_Status strip_status =
        HAKO_StripTypes(JS_GetRuntime(ctx), js_code, &stripped_js, &stripped_len);

    if (strip_status == HAKO_STATUS_SUCCESS) {
      code_to_eval = stripped_js;
      code_len = stripped_len;
    } else if (strip_status == HAKO_STATUS_ERROR_UNSUPPORTED) {
      // Unsupported syntax - use stripped output anyway (it's returned even on
      // error)
      if (stripped_js != NULL) {
        code_to_eval = stripped_js;
        code_len = stripped_len;
      }
    } else {
      // Fatal stripping error - return exception
      if (stripped_js != NULL) {
         js_free_rt(JS_GetRuntime(ctx), stripped_js);
      }
      return jsvalue_to_heap(
          ctx,
          JS_ThrowSyntaxError(ctx, "Failed to strip TypeScript types: %s",
                              strip_status == HAKO_STATUS_ERROR_PARSE_FAILED
                                  ? "parse failed"
                              : strip_status == HAKO_STATUS_ERROR_OUT_OF_MEMORY
                                  ? "out of memory"
                                  : "invalid input"));
    }
  }

  if (detect_module && (eval_flags & JS_EVAL_TYPE_MODULE) == 0) {
    // Check for module extensions (.mjs, .mts, .mtsx) or ES module syntax
    if (ends_with_module_extension(filename) ||
        JS_DetectModule(code_to_eval, code_len)) {
      eval_flags |= JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT;
    }
  }

  is_module = (eval_flags & JS_EVAL_TYPE_MODULE) != 0;

  if (is_module && (eval_flags & JS_EVAL_FLAG_COMPILE_ONLY) == 0) {
    func_obj = JS_Eval(ctx, code_to_eval, code_len, filename,
                       eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func_obj)) {
      result = jsvalue_to_heap(ctx, func_obj);
      func_obj = JS_UNDEFINED;
      goto done;
    }

    if (!JS_VALUE_IS_MODULE(func_obj)) {
      JS_FreeValue(ctx, func_obj);
      result = jsvalue_to_heap(
          ctx,
          JS_ThrowTypeError(ctx, "Module code compiled to non-module object"));
      func_obj = JS_UNDEFINED;
      goto done;
    }

    module = JS_VALUE_GET_PTR(func_obj);
    if (module == NULL) {
      JS_FreeValue(ctx, func_obj);
      result = jsvalue_to_heap(
          ctx, JS_ThrowTypeError(ctx, "Module compiled to null"));
      func_obj = JS_UNDEFINED;
      goto done;
    }

    eval_result = JS_EvalFunction(ctx, func_obj);
    func_obj = JS_UNDEFINED;
  } else {
    eval_result = JS_Eval(ctx, code_to_eval, code_len, filename, eval_flags);
  }

  if (JS_IsException(eval_result)) {
    result = jsvalue_to_heap(ctx, eval_result);
    eval_result = JS_UNDEFINED;
    goto done;
  }

  if (!JS_IsPromise(eval_result)) {
    if (is_module) {
      module_namespace = JS_GetModuleNamespace(ctx, module);
      result = jsvalue_to_heap(ctx, module_namespace);
      module_namespace = JS_UNDEFINED;
    } else {
      result = jsvalue_to_heap(ctx, eval_result);
      eval_result = JS_UNDEFINED;
    }
    goto done;
  }

  // eval_result is a promise - return it regardless of state (pending,
  // fulfilled, or rejected)
  if (is_module) {
    // For modules, always return a promise that resolves to the namespace
    module_namespace = JS_GetModuleNamespace(ctx, module);
    if (JS_IsException(module_namespace)) {
      result = jsvalue_to_heap(ctx, module_namespace);
      module_namespace = JS_UNDEFINED;
      goto done;
    }

    then_resolve_module_namespace = JS_NewCFunctionData(
        ctx, &hako_resolve_func_data, 0, 0, 1, &module_namespace);
    JS_FreeValue(ctx, module_namespace);
    module_namespace = JS_UNDEFINED;

    if (JS_IsException(then_resolve_module_namespace)) {
      result = jsvalue_to_heap(ctx, then_resolve_module_namespace);
      then_resolve_module_namespace = JS_UNDEFINED;
      goto done;
    }

    then_atom = JS_NewAtom(ctx, "then");
    then_args[0] = then_resolve_module_namespace;
    new_promise = JS_Invoke(ctx, eval_result, then_atom, 1, then_args);

    result = jsvalue_to_heap(ctx, new_promise);
    new_promise = JS_UNDEFINED;
  } else {
    // For non-modules, return the promise as-is (including rejected promises)
    result = jsvalue_to_heap(ctx, eval_result);
    eval_result = JS_UNDEFINED;
  }

done:
  if (stripped_js != NULL) {
     js_free_rt(JS_GetRuntime(ctx), stripped_js);
  }
  if (!JS_IsUndefined(func_obj))
    JS_FreeValue(ctx, func_obj);
  if (!JS_IsUndefined(eval_result))
    JS_FreeValue(ctx, eval_result);
  if (!JS_IsUndefined(module_namespace))
    JS_FreeValue(ctx, module_namespace);
  if (!JS_IsUndefined(then_resolve_module_namespace))
    JS_FreeValue(ctx, then_resolve_module_namespace);
  if (!JS_IsUndefined(new_promise))
    JS_FreeValue(ctx, new_promise);
  if (then_atom != JS_ATOM_NULL)
    JS_FreeAtom(ctx, then_atom);

  return result;
}

JSValue *HAKO_NewSymbol(JSContext *ctx, const char *description, int32_t isGlobal) {
  JSValue global = JS_UNDEFINED;
  JSValue Symbol = JS_UNDEFINED;
  JSValue descriptionValue = JS_UNDEFINED;
  JSValue Symbol_for = JS_UNDEFINED;
  JSValue symbol = JS_UNDEFINED;
  JSValue *result = NULL;

  global = JS_GetGlobalObject(ctx);
  Symbol = JS_GetPropertyStr(ctx, global, "Symbol");
  descriptionValue = JS_NewString(ctx, description);

  if (isGlobal != 0) {
    Symbol_for = JS_GetPropertyStr(ctx, Symbol, "for");
    symbol = JS_Call(ctx, Symbol_for, Symbol, 1, &descriptionValue);
    result = jsvalue_to_heap(ctx, symbol);
    symbol = JS_UNDEFINED;
    goto done;
  }

  symbol = JS_Call(ctx, Symbol, JS_UNDEFINED, 1, &descriptionValue);
  result = jsvalue_to_heap(ctx, symbol);
  symbol = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(Symbol_for))
    JS_FreeValue(ctx, Symbol_for);
  if (!JS_IsUndefined(descriptionValue))
    JS_FreeValue(ctx, descriptionValue);
  if (!JS_IsUndefined(Symbol))
    JS_FreeValue(ctx, Symbol);
  if (!JS_IsUndefined(global))
    JS_FreeValue(ctx, global);
  if (!JS_IsUndefined(symbol))
    JS_FreeValue(ctx, symbol);

  return result;
}

const char *HAKO_GetSymbolDescriptionOrKey(JSContext *ctx,
                                           JSValueConst *value) {
  JSValue key = JS_UNDEFINED;
  JSValue description = JS_UNDEFINED;
  const char *result = NULL;

  key = hako_get_symbol_key(ctx, value);
  if (!JS_IsUndefined(key)) {
    result = JS_ToCString(ctx, key);
    goto done;
  }

  description = JS_GetPropertyStr(ctx, *value, "description");
  result = JS_ToCString(ctx, description);

done:
  if (!JS_IsUndefined(key))
    JS_FreeValue(ctx, key);
  if (!JS_IsUndefined(description))
    JS_FreeValue(ctx, description);

  return result;
}

JS_BOOL HAKO_IsGlobalSymbol(JSContext *ctx, JSValueConst *value) {
  JSValue key = JS_UNDEFINED;
  int32_t undefined;

  key = hako_get_symbol_key(ctx, value);
  undefined = JS_IsUndefined(key);
  JS_FreeValue(ctx, key);

  return undefined ? 0 : 1;
}

JS_BOOL HAKO_IsJobPending(JSRuntime *rt) { return JS_IsJobPending(rt); }

int32_t HAKO_ExecutePendingJob(JSRuntime *rt, int32_t maxJobsToExecute,
                           JSContext **lastJobContext) {
  JSContext *pctx = NULL;
  int32_t status = 1;
  int32_t executed = 0;

  while (executed != maxJobsToExecute && status == 1) {
    status = JS_ExecutePendingJob(rt, &pctx);
    if (status == -1) {
      *lastJobContext = pctx;
      return -1;
    } else if (status == 1) {
      *lastJobContext = pctx;
      executed++;
    }
  }

  *lastJobContext = pctx;
  return executed;
}

JSValue *HAKO_GetProp(JSContext *ctx, JSValueConst *this_val,
                      JSValueConst *prop_name) {
  JSAtom prop_atom;
  JSValue prop_val = JS_UNDEFINED;
  JSValue *result = NULL;

  prop_atom = JS_ValueToAtom(ctx, *prop_name);
  prop_val = JS_GetProperty(ctx, *this_val, prop_atom);
  JS_FreeAtom(ctx, prop_atom);

  if (JS_IsException(prop_val))
    goto done;

  result = jsvalue_to_heap(ctx, prop_val);
  prop_val = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(prop_val))
    JS_FreeValue(ctx, prop_val);
  return result;
}

JSValue *HAKO_GetPropNumber(JSContext *ctx, JSValueConst *this_val,
                            int32_t prop_name) {
  JSValue prop_val;

  prop_val = JS_GetPropertyUint32(ctx, *this_val, (uint32_t)prop_name);
  if (JS_IsException(prop_val))
    return NULL;

  return jsvalue_to_heap(ctx, prop_val);
}

JS_BOOL HAKO_SetProp(JSContext *ctx, JSValueConst *this_val,
                     JSValueConst *prop_name, JSValueConst *prop_value) {
  JSAtom prop_atom;
  int32_t result;

  prop_atom = JS_ValueToAtom(ctx, *prop_name);
  result =
      JS_SetProperty(ctx, *this_val, prop_atom, JS_DupValue(ctx, *prop_value));
  JS_FreeAtom(ctx, prop_atom);
  return result;
}

JS_BOOL HAKO_DefineProp(JSContext *ctx, JSValueConst *this_val,
                        JSValueConst *prop_name, JSValueConst *prop_value,
                        JSValueConst *get, JSValueConst *set,
                        JS_BOOL configurable, JS_BOOL enumerable,
                        JS_BOOL has_value, JS_BOOL has_writable,
                        JS_BOOL writable) {
  JSAtom prop_atom;
  int32_t flags = 0;
  int32_t has_get, has_set, is_accessor;
  int32_t result;

  prop_atom = JS_ValueToAtom(ctx, *prop_name);
  if (prop_atom == JS_ATOM_NULL)
    return -1;

  has_get = !JS_IsUndefined(*get);
  has_set = !JS_IsUndefined(*set);
  is_accessor = (has_get || has_set);

  if (is_accessor && (has_value || has_writable)) {
    JS_FreeAtom(ctx, prop_atom);
    JS_ThrowTypeError(ctx, "accessor descriptor cannot include value/writable");
    return -1;
  }

  flags |= JS_PROP_HAS_CONFIGURABLE;
  if (configurable) {
    flags |= JS_PROP_CONFIGURABLE;
  }
  flags |= JS_PROP_HAS_ENUMERABLE;
  if (enumerable) {
    flags |= JS_PROP_ENUMERABLE;
  }
  if (has_get)
    flags |= JS_PROP_HAS_GET;
  if (has_set)
    flags |= JS_PROP_HAS_SET;
  if (has_value) {
    flags |= JS_PROP_HAS_VALUE;
  }
  if (!is_accessor && has_writable) {
    flags |= JS_PROP_HAS_WRITABLE;
    if (writable) {
      flags |= JS_PROP_WRITABLE;
    }
  }

  result = JS_DefineProperty(ctx, *this_val, prop_atom, *prop_value, *get, *set,
                             flags);
  JS_FreeAtom(ctx, prop_atom);
  return result;
}

static inline JS_BOOL __JS_AtomIsTaggedInt(JSAtom v) {
  return (v & (1U << 31)) != 0;
}

static inline uint32_t __JS_AtomToUInt32(JSAtom atom) {
  return atom & ~(1U << 31);
}

JSValue *HAKO_GetOwnPropertyNames(JSContext *ctx, JSValue ***out_ptrs,
                                  uint32_t *out_len, JSValueConst *obj,
                                  int32_t flags) {
  JSPropertyEnum *tab = NULL;
  JSValue **ptrs = NULL;
  uint32_t total_props = 0;
  uint32_t out_props = 0;
  uint32_t i;
  int32_t hako_standard_compliant_number;
  int32_t hako_include_string;
  int32_t hako_include_number;
  int32_t status;
  JSAtom atom;
  JSValue atom_value = JS_UNDEFINED;
  JSValue *result = NULL;

  if (out_ptrs == NULL || out_len == NULL) {
    result = jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "Invalid arguments"));
    goto done;
  }

  *out_ptrs = NULL;
  *out_len = 0;

  if (JS_VALUE_GET_TAG(*obj) != JS_TAG_OBJECT) {
    result = jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "not an object"));
    goto done;
  }

  hako_standard_compliant_number =
      (flags & HAKO_STANDARD_COMPLIANT_NUMBER) != 0;
  hako_include_string = (flags & JS_GPN_STRING_MASK) != 0;
  hako_include_number =
      hako_standard_compliant_number ? 0 : (flags & HAKO_GPN_NUMBER_MASK) != 0;

  if (hako_include_number) {
    flags = flags | JS_GPN_STRING_MASK;
  }

  status = JS_GetOwnPropertyNames(ctx, &tab, &total_props, *obj, flags);
  if (status < 0) {
    result = jsvalue_to_heap(ctx, JS_GetException(ctx));
    goto done;
  }

  if (total_props == 0)
    goto done;

  ptrs = js_malloc(ctx, sizeof(JSValue *) * total_props);
  if (!ptrs) {
    result = jsvalue_to_heap(ctx, JS_ThrowOutOfMemory(ctx));
    goto done;
  }

  for (i = 0; i < total_props; i++) {
    atom = tab[i].atom;

    if (__JS_AtomIsTaggedInt(atom)) {
      if (hako_include_number) {
        uint32_t v = __JS_AtomToUInt32(atom);
        ptrs[out_props++] = jsvalue_to_heap(ctx, JS_NewInt32(ctx, v));
      } else if (hako_include_string && hako_standard_compliant_number) {
        ptrs[out_props++] =
            jsvalue_to_heap(ctx, JS_AtomToValue(ctx, tab[i].atom));
      }
      JS_FreeAtom(ctx, atom);
      continue;
    }

    atom_value = JS_AtomToValue(ctx, atom);
    JS_FreeAtom(ctx, atom);

    if (JS_IsString(atom_value)) {
      if (hako_include_string) {
        ptrs[out_props++] = jsvalue_to_heap(ctx, atom_value);
        atom_value = JS_UNDEFINED;
      } else {
        JS_FreeValue(ctx, atom_value);
        atom_value = JS_UNDEFINED;
      }
    } else {
      ptrs[out_props++] = jsvalue_to_heap(ctx, atom_value);
      atom_value = JS_UNDEFINED;
    }
  }

  *out_ptrs = ptrs;
  *out_len = out_props;
  ptrs = NULL;

done:
  if (!JS_IsUndefined(atom_value))
    JS_FreeValue(ctx, atom_value);
  if (tab != NULL)
    js_free(ctx, tab);
  if (ptrs != NULL)
    js_free(ctx, ptrs);
  return result;
}

JSValue *HAKO_Call(JSContext *ctx, JSValueConst *func_obj,
                   JSValueConst *this_obj, int32_t argc, JSValueConst **argv_ptrs) {
  JSValueConst argv[argc];
  int32_t i;

  for (i = 0; i < argc; i++) {
    argv[i] = *(argv_ptrs[i]);
  }

  return jsvalue_to_heap(ctx, JS_Call(ctx, *func_obj, *this_obj, argc, argv));
}

JSValue *HAKO_GetLastError(JSContext *ctx, JSValue *maybe_exception) {
  JSValue exception = JS_UNDEFINED;

  if (maybe_exception != NULL) {
    if (JS_IsException(*maybe_exception)) {
      return jsvalue_to_heap(ctx, JS_GetException(ctx));
    }
    return NULL;
  }

  exception = JS_GetException(ctx);
  if (!JS_IsNull(exception)) {
    return jsvalue_to_heap(ctx, exception);
  }
  return NULL;
}

const char *HAKO_Dump(JSContext *ctx, JSValueConst *obj) {
  JSValue error_obj = JS_UNDEFINED;
  JSValue json_value = JS_UNDEFINED;
  JSValue current_error = JS_UNDEFINED;
  JSValue current_obj = JS_UNDEFINED;
  JSValue next_obj = JS_UNDEFINED;
  JSValue message = JS_UNDEFINED;
  JSValue name = JS_UNDEFINED;
  JSValue stack = JS_UNDEFINED;
  JSValue cause = JS_UNDEFINED;
  const char *result = NULL;
  int32_t depth;

  if (JS_IsError(ctx, *obj)) {
    error_obj = JS_NewObject(ctx);
    current_error = JS_DupValue(ctx, *obj);
    current_obj = error_obj;
    depth = 0;

    while (depth < 3) {
      message = JS_GetPropertyStr(ctx, current_error, "message");
      if (!JS_IsException(message) && !JS_IsUndefined(message)) {
        JS_SetPropertyStr(ctx, current_obj, "message", message);
        message = JS_UNDEFINED;
      } else {
        if (!JS_IsUndefined(message))
          JS_FreeValue(ctx, message);
        message = JS_UNDEFINED;
      }

      name = JS_GetPropertyStr(ctx, current_error, "name");
      if (!JS_IsException(name) && !JS_IsUndefined(name)) {
        JS_SetPropertyStr(ctx, current_obj, "name", name);
        name = JS_UNDEFINED;
      } else {
        if (!JS_IsUndefined(name))
          JS_FreeValue(ctx, name);
        name = JS_UNDEFINED;
      }

      stack = JS_GetPropertyStr(ctx, current_error, "stack");
      if (!JS_IsException(stack) && !JS_IsUndefined(stack)) {
        JS_SetPropertyStr(ctx, current_obj, "stack", stack);
        stack = JS_UNDEFINED;
      } else {
        if (!JS_IsUndefined(stack))
          JS_FreeValue(ctx, stack);
        stack = JS_UNDEFINED;
      }

      cause = JS_GetPropertyStr(ctx, current_error, "cause");

      if (!JS_IsException(cause) && !JS_IsUndefined(cause) &&
          !JS_IsNull(cause) && JS_IsError(ctx, cause) && depth < 2) {
        next_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, current_obj, "cause", next_obj);
        current_obj = next_obj;
        next_obj = JS_UNDEFINED;
        JS_FreeValue(ctx, current_error);
        current_error = cause;
        cause = JS_UNDEFINED;
        depth++;
      } else {
        if (!JS_IsException(cause) && !JS_IsUndefined(cause) &&
            !JS_IsNull(cause)) {
          JS_SetPropertyStr(ctx, current_obj, "cause", cause);
          cause = JS_UNDEFINED;
        } else {
          if (!JS_IsUndefined(cause))
            JS_FreeValue(ctx, cause);
          cause = JS_UNDEFINED;
        }
        JS_FreeValue(ctx, current_error);
        current_error = JS_UNDEFINED;
        break;
      }
    }

    json_value =
        JS_JSONStringify(ctx, error_obj, JS_UNDEFINED, JS_NewInt32(ctx, 2));
    JS_FreeValue(ctx, error_obj);
    error_obj = JS_UNDEFINED;

    if (!JS_IsException(json_value)) {
      result = JS_ToCString(ctx, json_value);
      JS_FreeValue(ctx, json_value);
      json_value = JS_UNDEFINED;
      goto done;
    } else {
      JS_FreeValue(ctx, json_value);
      json_value = JS_UNDEFINED;
    }
  } else {
    json_value = JS_JSONStringify(ctx, *obj, JS_UNDEFINED, JS_NewInt32(ctx, 2));
    if (!JS_IsException(json_value)) {
      result = JS_ToCString(ctx, json_value);
      JS_FreeValue(ctx, json_value);
      json_value = JS_UNDEFINED;
      goto done;
    } else {
      JS_FreeValue(ctx, json_value);
      json_value = JS_UNDEFINED;
    }
  }

  {
    static char error_buffer[128];
    snprintf(error_buffer, sizeof(error_buffer),
             "{\"error\":\"Failed to serialize object\"}");
    result = error_buffer;
  }

done:
  if (!JS_IsUndefined(error_obj))
    JS_FreeValue(ctx, error_obj);
  if (!JS_IsUndefined(json_value))
    JS_FreeValue(ctx, json_value);
  if (!JS_IsUndefined(current_error))
    JS_FreeValue(ctx, current_error);
  if (!JS_IsUndefined(message))
    JS_FreeValue(ctx, message);
  if (!JS_IsUndefined(name))
    JS_FreeValue(ctx, name);
  if (!JS_IsUndefined(stack))
    JS_FreeValue(ctx, stack);
  if (!JS_IsUndefined(cause))
    JS_FreeValue(ctx, cause);
  if (!JS_IsUndefined(next_obj))
    JS_FreeValue(ctx, next_obj);

  return result;
}

JS_BOOL HAKO_IsModule(JSContext *ctx, JSValueConst *module_func_obj) {
  return JS_VALUE_IS_MODULE(*module_func_obj);
}

JSValue *HAKO_GetModuleNamespace(JSContext *ctx,
                                 JSValueConst *module_func_obj) {
  JSModuleDef *module;

  if (!JS_VALUE_IS_MODULE(*module_func_obj)) {
    return jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "Not a module"));
  }

  module = JS_VALUE_GET_PTR(*module_func_obj);
  return jsvalue_to_heap(ctx, JS_GetModuleNamespace(ctx, module));
}

HAKOTypeOf HAKO_TypeOf(JSContext *ctx, JSValueConst *value) {
  int32_t tag = JS_VALUE_GET_NORM_TAG(*value);

  switch (tag) {
  case JS_TAG_UNDEFINED:
    return HAKO_TYPE_UNDEFINED;

  case JS_TAG_NULL:
    return HAKO_TYPE_OBJECT;

  case JS_TAG_STRING:
  case JS_TAG_STRING_ROPE:
    return HAKO_TYPE_STRING;

  case JS_TAG_SYMBOL:
    return HAKO_TYPE_SYMBOL;

  case JS_TAG_BOOL:
    return HAKO_TYPE_BOOLEAN;

  case JS_TAG_INT:
  case JS_TAG_FLOAT64:
    return HAKO_TYPE_NUMBER;

  case JS_TAG_BIG_INT:
  case JS_TAG_SHORT_BIG_INT:
    return HAKO_TYPE_BIGINT;

  case JS_TAG_OBJECT:
    if (JS_IsFunction(ctx, *value))
      return HAKO_TYPE_FUNCTION;
    else
      return HAKO_TYPE_OBJECT;

  default:
    return HAKO_TYPE_UNDEFINED;
  }
}

JS_BOOL HAKO_IsNull(JSValueConst *value) { return JS_IsNull(*value); }

JS_BOOL HAKO_IsUndefined(JSValueConst *value) { return JS_IsUndefined(*value); }

JS_BOOL HAKO_IsNullOrUndefined(JSValueConst *value) {
  return JS_IsNull(*value) || JS_IsUndefined(*value);
}

JSAtom HAKO_AtomLength = 0;

int32_t HAKO_GetLength(JSContext *ctx, uint32_t *out_len, JSValueConst *value) {
  JSValue len_val = JS_UNDEFINED;
  int32_t result;

  if (!JS_IsObject(*value))
    return -1;

  if (HAKO_AtomLength == 0) {
    HAKO_AtomLength = JS_NewAtom(ctx, "length");
  }

  len_val = JS_GetProperty(ctx, *value, HAKO_AtomLength);
  if (JS_IsException(len_val)) {
    result = -1;
    goto done;
  }

  result = JS_ToUint32(ctx, out_len, len_val);

done:
  if (!JS_IsUndefined(len_val))
    JS_FreeValue(ctx, len_val);
  return result;
}

JS_BOOL HAKO_IsEqual(JSContext *ctx, JSValueConst *a, JSValueConst *b,
                     IsEqualOp op) {
  switch (op) {
  case HAKO_EqualOp_SameValue:
    return JS_SameValue(ctx, *a, *b);
  case HAKO_EqualOp_SameValueZero:
    return JS_SameValueZero(ctx, *a, *b);
  default:
  case HAKO_EqualOp_StrictEq:
    return JS_StrictEq(ctx, *a, *b);
  }
}

JSValue *HAKO_GetGlobalObject(JSContext *ctx) {
  return jsvalue_to_heap(ctx, JS_GetGlobalObject(ctx));
}

JSValue *HAKO_NewPromiseCapability(JSContext *ctx,
                                   JSValue **resolve_funcs_out) {
  JSValue resolve_funcs[2];
  JSValue promise;

  promise = JS_NewPromiseCapability(ctx, resolve_funcs);
  resolve_funcs_out[0] = jsvalue_to_heap(ctx, resolve_funcs[0]);
  resolve_funcs_out[1] = jsvalue_to_heap(ctx, resolve_funcs[1]);
  return jsvalue_to_heap(ctx, promise);
}

JS_BOOL HAKO_IsPromise(JSContext *ctx, JSValueConst *promise) {
  return JS_IsPromise(*promise);
}

JSPromiseStateEnum HAKO_PromiseState(JSContext *ctx, JSValueConst *promise) {
  return JS_PromiseState(ctx, *promise);
}

JSValue *HAKO_PromiseResult(JSContext *ctx, JSValueConst *promise) {
  return jsvalue_to_heap(ctx, JS_PromiseResult(ctx, *promise));
}

JS_BOOL HAKO_BuildIsDebug(void) {
#ifdef HAKO_DEBUG_MODE
  return 1;
#else
  return 0;
#endif
}

JSValue hako_call_function(JSContext *ctx, JSValueConst this_val, int32_t argc,
                           JSValueConst *argv, int32_t magic) {
  JSValue *result_ptr = NULL;
  JSValue result;

  result_ptr = host_call_function(ctx, &this_val, argc, argv, magic);

  if (result_ptr == NULL)
    return JS_UNDEFINED;

  if (is_static_constant(result_ptr))
    return *result_ptr;

  result = *result_ptr;
  js_free(ctx, result_ptr);
  return result;
}

JSValue *HAKO_NewFunction(JSContext *ctx, int32_t func_id, const char *name) {
  JSValue func_obj = JS_NewCFunctionMagic(ctx, hako_call_function, name, 0,
                                          JS_CFUNC_generic_magic, func_id);
  return jsvalue_to_heap(ctx, func_obj);
}

JSValueConst *HAKO_ArgvGetJSValueConstPointer(JSValueConst *argv, int32_t index) {
  return &argv[index];
}

void HAKO_RuntimeEnableInterruptHandler(JSRuntime *rt, void *opaque) {
  JS_SetInterruptHandler(rt, host_interrupt_handler, opaque);
}

void HAKO_RuntimeDisableInterruptHandler(JSRuntime *rt) {
  JS_SetInterruptHandler(rt, NULL, NULL);
}

static int32_t hako_module_check_attributes(JSContext *ctx, void *opaque,
                                        JSValueConst attributes) {
  JSPropertyEnum *tab = NULL;
  uint32_t i, len;
  int32_t ret = 0;
  const char *cstr = NULL;
  size_t cstr_len;

  if (JS_GetOwnPropertyNames(ctx, &tab, &len, attributes,
                             JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK)) {
    ret = -1;
    goto done;
  }

  for (i = 0; i < len; i++) {
    cstr = JS_AtomToCString(ctx, tab[i].atom);
    if (!cstr) {
      ret = -1;
      goto done;
    }

    cstr_len = strlen(cstr);

    if (!(cstr_len == 4 && !memcmp(cstr, "type", cstr_len))) {
      JS_ThrowTypeError(ctx, "import attribute '%s' is not supported", cstr);
      JS_FreeCString(ctx, cstr);
      cstr = NULL;
      ret = -1;
      goto done;
    }

    JS_FreeCString(ctx, cstr);
    cstr = NULL;
  }

done:
  if (cstr)
    JS_FreeCString(ctx, cstr);
  if (tab)
    JS_FreePropertyEnum(ctx, tab, len);
  return ret;
}

void HAKO_RuntimeEnableModuleLoader(JSRuntime *rt, JS_BOOL use_custom_normalize,
                                    void *opaque) {
  JSModuleNormalizeFunc *module_normalize = NULL;

  if (use_custom_normalize) {
    module_normalize = hako_normalize_module;
  }
  JS_SetModuleLoaderFunc2(rt, module_normalize, hako_load_module,
                          hako_module_check_attributes, opaque);
}

void HAKO_RuntimeDisableModuleLoader(JSRuntime *rt) {
  JS_SetModuleLoaderFunc2(rt, NULL, NULL, NULL, NULL);
}

void *HAKO_BJSON_Encode(JSContext *ctx, JSValueConst *val, size_t *out_length) {
  size_t length = 0;
  uint8_t *buffer = NULL;

  if (!out_length) {
    JS_ThrowTypeError(ctx, "out_length parameter is required");
    return NULL;
  }

  buffer = JS_WriteObject(ctx, &length, *val, JS_WRITE_OBJ_BYTECODE);
  if (!buffer) {
    *out_length = 0;
    return NULL;
  }

  *out_length = length;
  return (void *)buffer;
}

JSValue *HAKO_BJSON_Decode(JSContext *ctx, void *buffer, size_t length) {
  JSValue value;

  if (!buffer || length == 0) {
    return jsvalue_to_heap(ctx,
                           JS_ThrowTypeError(ctx, "Invalid buffer or length"));
  }

  value =
      JS_ReadObject(ctx, (const uint8_t *)buffer, length, JS_READ_OBJ_BYTECODE);
  return jsvalue_to_heap(ctx, value);
}

JS_BOOL HAKO_IsArray(JSContext *ctx, JSValueConst *val) {
  return JS_IsArray(ctx, *val);
}

JS_BOOL HAKO_IsTypedArray(JSContext *ctx, JSValueConst *val) {
  return JS_IsTypedArray(*val);
}

JSTypedArrayEnum HAKO_GetTypedArrayType(JSContext *ctx, JSValueConst *val) {
  return JS_GetTypedArrayType(*val);
}

void *HAKO_CopyTypedArrayBuffer(JSContext *ctx, JSValueConst *val,
                                size_t *out_length) {
  size_t byte_offset = 0;
  size_t byte_length = 0;
  size_t bytes_per_element = 0;
  size_t buffer_length = 0;
  uint8_t *buffer_data = NULL;
  uint8_t *copy = NULL;
  JSValue buffer = JS_UNDEFINED;

  if (out_length) {
    *out_length = 0;
  }

  if (!HAKO_IsTypedArray(ctx, val)) {
    JS_ThrowTypeError(ctx, "Invalid TypedArray");
    goto cleanup;
  }

  buffer = JS_GetTypedArrayBuffer(ctx, *val, &byte_offset, &byte_length,
                                  &bytes_per_element);
  if (JS_IsException(buffer))
    goto cleanup;

  buffer_data = JS_GetArrayBuffer(ctx, &buffer_length, buffer);
  if (!buffer_data)
    goto cleanup;

  if (byte_offset + byte_length > buffer_length) {
    JS_ThrowRangeError(ctx, "TypedArray offset/length out of bounds");
    goto cleanup;
  }

  copy = js_malloc(ctx, byte_length);
  if (!copy) {
    JS_ThrowOutOfMemory(ctx);
    goto cleanup;
  }

  memcpy(copy, buffer_data + byte_offset, byte_length);

  if (out_length) {
    *out_length = byte_length;
  }

cleanup:
  if (!JS_IsUndefined(buffer))
    JS_FreeValue(ctx, buffer);
  return copy;
}

JS_BOOL HAKO_IsArrayBuffer(JSValueConst *val) { return JS_IsArrayBuffer(*val); }

JSValue *HAKO_ToJson(JSContext *ctx, JSValueConst *val, int32_t indent) {
  JSValue indent_val = JS_UNDEFINED;
  JSValue result = JS_UNDEFINED;
  JSValue *ret = NULL;

  if (JS_IsUndefined(*val)) {
    ret = jsvalue_to_heap(ctx, JS_NewString(ctx, "undefined"));
    goto done;
  }
  if (JS_IsNull(*val)) {
    ret = jsvalue_to_heap(ctx, JS_NewString(ctx, "null"));
    goto done;
  }

  indent_val = JS_NewInt32(ctx, indent);
  result = JS_JSONStringify(ctx, *val, JS_UNDEFINED, indent_val);

  if (JS_IsException(result)) {
    ret = jsvalue_to_heap(ctx, result);
    result = JS_UNDEFINED;
    goto done;
  }

  ret = jsvalue_to_heap(ctx, result);
  result = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(indent_val))
    JS_FreeValue(ctx, indent_val);
  if (!JS_IsUndefined(result))
    JS_FreeValue(ctx, result);
  return ret;
}

JSValue *HAKO_ParseJson(JSContext *ctx, const char *json, size_t buf_len,
                        const char *filename) {
  if (!json) {
    return jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "Invalid JSON string"));
  }
  return jsvalue_to_heap(
      ctx, JS_ParseJSON2(ctx, json, buf_len, filename, JS_PARSE_JSON_EXT));
}

JS_BOOL HAKO_IsError(JSContext *ctx, JSValueConst *val) {
  return JS_IsError(ctx, *val);
}

JS_BOOL HAKO_IsException(JSValueConst *val) { return JS_IsException(*val); }

JSValue *HAKO_GetException(JSContext *ctx) {
  return jsvalue_to_heap(ctx, JS_GetException(ctx));
}

void HAKO_SetGCThreshold(JSContext *ctx, int64_t threshold) {
  JS_SetGCThreshold(JS_GetRuntime(ctx), (size_t)threshold);
}

JSValue *HAKO_NewBigInt(JSContext *ctx, int64_t value) {
  return jsvalue_to_heap(ctx, JS_NewBigInt64(ctx, value));
}


int64_t HAKO_GetBigInt(JSContext* ctx, JSValueConst* val) {
  int64_t result = 0;
  if (JS_ToInt64Ext(ctx, &result, *val) < 0) {
    return 0;  // Error - exception already thrown by JS.
  }
  return result;
}

JSValue *HAKO_NewBigUInt(JSContext *ctx, uint64_t value) {
  return jsvalue_to_heap(ctx, JS_NewBigUint64(ctx, value));
}

uint64_t HAKO_GetBigUInt(JSContext* ctx, JSValueConst* val) {
  int64_t result = 0;
  if (JS_ToInt64Ext(ctx, &result, *val) < 0) {
    return 0;  // Error - exception already thrown by JS
  }
  return (uint64_t)result;
}

JSValue *HAKO_NewDate(JSContext *ctx, double time) {
  return jsvalue_to_heap(ctx, JS_NewDate(ctx, time));
}

JS_BOOL HAKO_IsDate(JSValueConst *val) {
  return JS_IsDate(*val);
}

JS_BOOL HAKO_IsMap(JSValueConst *val) {
  return JS_IsMap(*val);
}

JS_BOOL HAKO_IsSet(JSValueConst *val) {
  return JS_IsSet(*val);
}

double HAKO_GetDateTimestamp(JSContext *ctx, JSValueConst *val) {
  return JS_GetDateTimestamp(ctx, *val);
}

JSClassID HAKO_GetClassID(JSValueConst *val) { return JS_GetClassID(*val); }

JS_BOOL HAKO_IsInstanceOf(JSContext *ctx, JSValueConst *val,
                          JSValueConst *obj) {
  return JS_IsInstanceOf(ctx, *val, *obj);
}

HakoBuildInfo *HAKO_BuildInfo(void) { return &build_info; }

void *HAKO_CompileToByteCode(JSContext *ctx, const char *js_code,
                             size_t js_code_length, const char *filename,
                             JS_BOOL detect_module, int32_t flags,
                             size_t *out_bytecode_length) {
  JSValue compiled_obj = JS_UNDEFINED;
  uint8_t *js_bytecode_buf = NULL;
  size_t bytecode_len = 0;
  int32_t is_module;
  int32_t write_flags;
  char *stripped_js = NULL;
  size_t stripped_len = 0;
  const char *code_to_compile = js_code;
  size_t compile_len = js_code_length;
  int32_t should_strip = 0;

  if (!js_code || !filename || !out_bytecode_length) {
    JS_ThrowTypeError(ctx, "Invalid arguments");
    goto done;
  }

  // Check if we should strip TypeScript types
  should_strip = (flags & JS_EVAL_FLAG_STRIP_TYPES) || ends_with_ts(filename);

  if (should_strip) {
    HAKO_Status strip_status =
        HAKO_StripTypes(JS_GetRuntime(ctx),js_code, &stripped_js, &stripped_len);

    if (strip_status == HAKO_STATUS_SUCCESS) {
      code_to_compile = stripped_js;
      compile_len = stripped_len;
    } else if (strip_status == HAKO_STATUS_ERROR_UNSUPPORTED) {
      // Unsupported syntax - use stripped output anyway
      if (stripped_js != NULL) {
        code_to_compile = stripped_js;
        compile_len = stripped_len;
      }
    } else {
      // Fatal stripping error
      if (stripped_js != NULL) {
        js_free_rt(JS_GetRuntime(ctx), stripped_js);
      }
      JS_ThrowSyntaxError(
          ctx, "Failed to strip TypeScript types: %s",
          strip_status == HAKO_STATUS_ERROR_PARSE_FAILED    ? "parse failed"
          : strip_status == HAKO_STATUS_ERROR_OUT_OF_MEMORY ? "out of memory"
                                                            : "invalid input");
      goto done;
    }
  }

  if (detect_module && (flags & JS_EVAL_TYPE_MODULE) == 0) {
    // Check for module extensions (.mjs, .mts, .mtsx) or ES module syntax
    if (ends_with_module_extension(filename) ||
        JS_DetectModule(code_to_compile, compile_len)) {
      flags |= JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_STRICT;
    }
  }

  flags |= JS_EVAL_FLAG_COMPILE_ONLY;
  is_module = (flags & JS_EVAL_TYPE_MODULE) != 0;

  compiled_obj = JS_Eval(ctx, code_to_compile, compile_len, filename, flags);
  if (JS_IsException(compiled_obj))
    goto done;

  if (is_module) {
    if (hako_module_set_import_meta(ctx, compiled_obj, TRUE, TRUE) < 0)
      goto done;
  }

  write_flags = JS_WRITE_OBJ_BYTECODE;
  js_bytecode_buf =
      JS_WriteObject(ctx, &bytecode_len, compiled_obj, write_flags);

  if (!js_bytecode_buf) {
    JS_ThrowInternalError(ctx, "Failed to serialize bytecode");
    goto done;
  }

  *out_bytecode_length = bytecode_len;

done:
  if (stripped_js != NULL)
    js_free(ctx, stripped_js);
  if (!JS_IsUndefined(compiled_obj))
    JS_FreeValue(ctx, compiled_obj);
  return (void *)js_bytecode_buf;
}

JSValue *HAKO_EvalByteCode(JSContext *ctx, void *bytecode_buffer,
                           size_t bytecode_length, JS_BOOL load_only) {
  JSValue obj = JS_UNDEFINED;
  JSValue result = JS_UNDEFINED;
  JSValue module_namespace = JS_UNDEFINED;
  JSModuleDef *module = NULL;
  JSValue *ret = NULL;

  if (!bytecode_buffer || bytecode_length == 0) {
    ret = jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "Invalid bytecode buffer"));
    goto done;
  }

  obj = JS_ReadObject(ctx, (const uint8_t *)bytecode_buffer,
                      bytecode_length, JS_READ_OBJ_BYTECODE);
  
  if (JS_IsException(obj)) {
    ret = jsvalue_to_heap(ctx, obj);
    obj = JS_UNDEFINED;
    goto done;
  }

  if (load_only) {
    ret = jsvalue_to_heap(ctx, obj);
    obj = JS_UNDEFINED;
    goto done;
  }

  if (JS_VALUE_IS_MODULE(obj)) {
    module = JS_VALUE_GET_PTR(obj);
    
    result = JS_EvalFunction(ctx, obj);
    obj = JS_UNDEFINED;
    
    if (JS_IsException(result)) {
      ret = jsvalue_to_heap(ctx, result);
      result = JS_UNDEFINED;
      goto done;
    }
    
    module_namespace = JS_GetModuleNamespace(ctx, module);
    JS_FreeValue(ctx, result);
    result = JS_UNDEFINED;
    
    ret = jsvalue_to_heap(ctx, module_namespace);
    module_namespace = JS_UNDEFINED;
    goto done;
  }

  result = JS_EvalFunction(ctx, obj);
  obj = JS_UNDEFINED;
  
  ret = jsvalue_to_heap(ctx, result);
  result = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(obj))
    JS_FreeValue(ctx, obj);
  if (!JS_IsUndefined(result))
    JS_FreeValue(ctx, result);
  if (!JS_IsUndefined(module_namespace))
    JS_FreeValue(ctx, module_namespace);
  
  return ret;
}

JSModuleDef *HAKO_NewCModule(JSContext *ctx, const char *name_str) {
  return JS_NewCModule(ctx, name_str, host_module_init);
}

int32_t HAKO_AddModuleExport(JSContext *ctx, JSModuleDef *m,
                         const char *export_name) {
  return JS_AddModuleExport(ctx, m, export_name);
}

int32_t HAKO_SetModuleExport(JSContext *ctx, JSModuleDef *m,
                         const char *export_name, JSValueConst *val) {
  return JS_SetModuleExport(ctx, m, export_name, JS_DupValue(ctx, *val));
}

const char *HAKO_GetModuleName(JSContext *ctx, JSModuleDef *m) {
  JSAtom module_name_atom;
  const char *atom_str = NULL;

  if (!m)
    return NULL;

  module_name_atom = JS_GetModuleName(ctx, m);
  if (module_name_atom == JS_ATOM_NULL)
    return NULL;

  atom_str = JS_AtomToCString(ctx, module_name_atom);
  JS_FreeAtom(ctx, module_name_atom);

  return atom_str;
}

static void hako_promise_rejection_tracker_wrapper(JSContext *ctx,
                                                   JSValueConst promise,
                                                   JSValueConst reason,
                                                   JS_BOOL is_handled,
                                                   void *opaque) {
  host_promise_rejection_tracker(ctx, &promise, &reason, is_handled, opaque);
}

static JSValue hako_class_constructor_wrapper(JSContext *ctx,
                                              JSValueConst new_target, int32_t argc,
                                              JSValueConst *argv, int32_t magic) {
  JSClassID class_id = (JSClassID)magic;
  JSValue *result = NULL;
  JSValue ret;

  result = host_class_constructor(ctx, &new_target, argc, argv, class_id);

  if (!result)
    return JS_EXCEPTION;

  if (is_static_constant(result))
    return *result;

  ret = *result;
  js_free(ctx, result);
  return ret;
}

static void hako_class_finalizer_wrapper(JSRuntime *rt, JSValue val) {
  JSClassID class_id;
  void *opaque = NULL;

  class_id = JS_GetClassID(val);

  if (class_id != JS_INVALID_CLASS_ID) {
    opaque = JS_GetOpaque(val, class_id);
    host_class_finalizer(rt, opaque, class_id);
  }
}

static void hako_class_gc_mark_wrapper(JSRuntime *rt, JSValueConst val,
                                       JS_MarkFunc *mark_func) {
  JSClassID class_id;
  void *opaque = NULL;

  class_id = JS_GetClassID(val);

  if (class_id != JS_INVALID_CLASS_ID) {
    opaque = JS_GetOpaque(val, class_id);
    if (opaque) {
      host_class_gc_mark(rt, opaque, class_id, mark_func);
    }
  }
}

JSClassID HAKO_NewClassID(JSClassID *pclass_id) {
  return JS_NewClassID(pclass_id);
}

JSValue *HAKO_NewClass(JSContext *ctx, JSClassID class_id,
                       const char *class_name, JS_BOOL has_finalizer,
                       JS_BOOL has_gc_mark) {
  JSClassDef class_def;
  JSValue constructor = JS_UNDEFINED;
  JSValue *result = NULL;

  class_def.class_name = class_name;
  class_def.finalizer = has_finalizer ? hako_class_finalizer_wrapper : NULL;
  class_def.gc_mark = has_gc_mark ? hako_class_gc_mark_wrapper : NULL;
  class_def.call = NULL;
  class_def.exotic = NULL;

  if (JS_NewClass(JS_GetRuntime(ctx), class_id, &class_def) != 0) {
    result = jsvalue_to_heap(
        ctx,
        JS_ThrowInternalError(ctx, "Failed to create class '%s' with ID %d",
                              class_name, class_id));
    goto done;
  }

  constructor =
      JS_NewCFunctionMagic(ctx, hako_class_constructor_wrapper, class_name, 0,
                           JS_CFUNC_constructor_magic, class_id);

  result = jsvalue_to_heap(ctx, constructor);
  constructor = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(constructor))
    JS_FreeValue(ctx, constructor);
  return result;
}

void HAKO_SetClassProto(JSContext *ctx, JSClassID class_id,
                        JSValueConst *proto) {
  JS_SetClassProto(ctx, class_id, JS_DupValue(ctx, *proto));
}

void HAKO_SetConstructor(JSContext *ctx, JSValueConst *ctor,
                         JSValueConst *proto) {
  JS_SetConstructor(ctx, *ctor, *proto);
}

JSValue *HAKO_NewObjectClass(JSContext *ctx, JSClassID class_id) {
  return jsvalue_to_heap(ctx, JS_NewObjectClass(ctx, class_id));
}

void HAKO_SetOpaque(JSValueConst *obj, void *opaque) {
  JS_SetOpaque(*obj, opaque);
}

void *HAKO_GetOpaque(JSContext *ctx, JSValueConst *obj, JSClassID class_id) {
  return JS_GetOpaque2(ctx, *obj, class_id);
}

JSValue *HAKO_NewObjectProtoClass(JSContext *ctx, JSValueConst *proto,
                                  JSClassID class_id) {
  return jsvalue_to_heap(ctx, JS_NewObjectProtoClass(ctx, *proto, class_id));
}

void HAKO_SetModulePrivateValue(JSContext *ctx, JSModuleDef *module,
                                JSValue *value) {
  JSValue new_value = JS_DupValue(ctx, *value);
  JS_SetModulePrivateValue(ctx, module, new_value);
}

JSValue *HAKO_GetModulePrivateValue(JSContext *ctx, JSModuleDef *module) {
  return jsvalue_to_heap(ctx, JS_GetModulePrivateValue(ctx, module));
}

JSValue *HAKO_NewTypedArray(JSContext *ctx, int32_t length,
                            JSTypedArrayEnum type) {
  JSValue length_arg = JS_UNDEFINED;
  JSValue result = JS_UNDEFINED;
  JSValue *ret = NULL;

  if (type < JS_TYPED_ARRAY_INT8 || type > JS_TYPED_ARRAY_FLOAT64) {
    ret =
        jsvalue_to_heap(ctx, JS_ThrowTypeError(ctx, "Invalid TypedArray type"));
    goto done;
  }

  length_arg = JS_NewUint32(ctx, length);
  result = JS_NewTypedArray(ctx, 1, &length_arg, (JSTypedArrayEnum)type);
  ret = jsvalue_to_heap(ctx, result);
  result = JS_UNDEFINED;

done:
  if (!JS_IsUndefined(length_arg))
    JS_FreeValue(ctx, length_arg);
  if (!JS_IsUndefined(result))
    JS_FreeValue(ctx, result);
  return ret;
}

JSValue *HAKO_NewTypedArrayWithBuffer(JSContext *ctx,
                                      JSValueConst *array_buffer,
                                      int32_t byte_offset, int32_t length,
                                      JSTypedArrayEnum type) {
  if (type < JS_TYPED_ARRAY_INT8 || type > JS_TYPED_ARRAY_FLOAT64) {
    return jsvalue_to_heap(ctx,
                           JS_ThrowTypeError(ctx, "Invalid TypedArray type"));
  }
  return jsvalue_to_heap(
      ctx, JS_NewTypedArrayWithBuffer(ctx, *array_buffer, byte_offset, length,
                                      (JSTypedArrayEnum)type));
}

void HAKO_RunGC(JSRuntime *rt) { JS_RunGC(rt); }

void HAKO_MarkValue(JSRuntime *rt, JSValueConst *val, JS_MarkFunc *mark_func) {
  JS_MarkValue(rt, *val, mark_func);
}

void HAKO_SetPromiseRejectionHandler(JSRuntime *rt, void *opaque) {
  JS_SetHostPromiseRejectionTracker(rt, hako_promise_rejection_tracker_wrapper,
                                    opaque);
}

void HAKO_ClearPromiseRejectionHandler(JSRuntime *rt) {
  JS_SetHostPromiseRejectionTracker(rt, NULL, NULL);
}