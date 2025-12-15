#ifndef HAKO_H
#define HAKO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "quickjs.h"
#include "build.h"

#if defined(__WASI__) || defined(__wasi__)
#define HAKO_EXPORT(name) __attribute__((export_name(name)))
#else
#define HAKO_EXPORT(name)
#endif

typedef int32_t JSModuleInitFunc(JSContext* ctx, JSModuleDef* mod);

#define HAKO_GPN_NUMBER_MASK (1 << 6)
#define HAKO_STANDARD_COMPLIANT_NUMBER (1 << 7)
#define LEPUS_ATOM_TAG_INT (1U << 31)

typedef enum HAKO_Intrinsic {
  HAKO_Intrinsic_BaseObjects = 1 << 0,
  HAKO_Intrinsic_Date = 1 << 1,
  HAKO_Intrinsic_Eval = 1 << 2,
  HAKO_Intrinsic_StringNormalize = 1 << 3,
  HAKO_Intrinsic_RegExp = 1 << 4,
  HAKO_Intrinsic_RegExpCompiler = 1 << 5,
  HAKO_Intrinsic_JSON = 1 << 6,
  HAKO_Intrinsic_Proxy = 1 << 7,
  HAKO_Intrinsic_MapSet = 1 << 8,
  HAKO_Intrinsic_TypedArrays = 1 << 9,
  HAKO_Intrinsic_Promise = 1 << 10,
  HAKO_Intrinsic_BigInt = 1 << 11,
  HAKO_Intrinsic_BigFloat = 1 << 12,
  HAKO_Intrinsic_BigDecimal = 1 << 13,
  HAKO_Intrinsic_OperatorOverloading = 1 << 14,
  HAKO_Intrinsic_BignumExt = 1 << 15,
  HAKO_Intrinsic_Performance = 1 << 16,
  HAKO_Intrinsic_Crypto = 1 << 17,
} HAKO_Intrinsic;

typedef enum HAKO_Status {
  HAKO_STATUS_SUCCESS = 0,
  HAKO_STATUS_ERROR_INVALID_ARGS = 1,
  HAKO_STATUS_ERROR_PARSE_FAILED = 2,
  HAKO_STATUS_ERROR_UNSUPPORTED = 3,
  HAKO_STATUS_ERROR_OUT_OF_MEMORY = 4,
} HAKO_Status;

typedef enum HAKO_ErrorType {
  HAKO_ERROR_RANGE = 0,
  HAKO_ERROR_REFERENCE = 1,
  HAKO_ERROR_SYNTAX = 2,
  HAKO_ERROR_TYPE = 3,
  HAKO_ERROR_URI = 4,
  HAKO_ERROR_INTERNAL = 5,
  HAKO_ERROR_OUT_OF_MEMORY = 6,
} HAKO_ErrorType;

typedef enum IsEqualOp {
  HAKO_EqualOp_StrictEq = 0,
  HAKO_EqualOp_SameValue = 1,
  HAKO_EqualOp_SameValueZero = 2
} IsEqualOp;

typedef enum {
  HAKO_TYPE_UNDEFINED = 0,
  HAKO_TYPE_OBJECT = 1,
  HAKO_TYPE_STRING = 2,
  HAKO_TYPE_SYMBOL = 3,
  HAKO_TYPE_BOOLEAN = 4,
  HAKO_TYPE_NUMBER = 5,
  HAKO_TYPE_BIGINT = 6,
  HAKO_TYPE_FUNCTION = 7
} HAKOTypeOf;

//! Property descriptor flags for HAKO_DefineProp
typedef enum HAKO_PropFlags {
  HAKO_PROP_CONFIGURABLE = 1 << 0,
  HAKO_PROP_ENUMERABLE = 1 << 1,
  HAKO_PROP_WRITABLE = 1 << 2,
  HAKO_PROP_HAS_VALUE = 1 << 3,
  HAKO_PROP_HAS_WRITABLE = 1 << 4,
  HAKO_PROP_HAS_GET = 1 << 5,
  HAKO_PROP_HAS_SET = 1 << 6,
} HAKO_PropFlags;

//! Property descriptor for HAKO_DefineProp
typedef struct HAKO_PropDescriptor {
  union {
    JSValueConst* value; // data descriptor
    struct {
      JSValueConst* get; // accessor descriptor
      JSValueConst* set;
    } accessor;
  };
  uint8_t flags;
} HAKO_PropDescriptor;

//! Creates a new runtime
//! @return New runtime or NULL on failure. Caller owns, free with HAKO_FreeRuntime.
HAKO_EXPORT("HAKO_NewRuntime") extern JSRuntime* HAKO_NewRuntime(void);

//! Frees a runtime and all associated resources
//! @param rt Runtime to free, consumed
HAKO_EXPORT("HAKO_FreeRuntime") extern void HAKO_FreeRuntime(JSRuntime* rt);

//! Configure debug info stripping for compiled code
//! @param rt Runtime to configure
//! @param flags Strip flags
HAKO_EXPORT("HAKO_SetStripInfo") extern void HAKO_SetStripInfo(JSRuntime* rt, int32_t flags);

//! Get debug info stripping configuration
//! @param rt Runtime to query
//! @return Current strip flags
HAKO_EXPORT("HAKO_GetStripInfo") extern int32_t HAKO_GetStripInfo(JSRuntime* rt);

//! Sets memory limit for runtime
//! @param rt Runtime to configure
//! @param limit Limit in bytes, -1 to disable
HAKO_EXPORT("HAKO_RuntimeSetMemoryLimit") extern void HAKO_RuntimeSetMemoryLimit(JSRuntime* rt, size_t limit);

//! Computes memory usage statistics
//! @param rt Runtime to query
//! @param ctx Context for creating result
//! @return New object with memory stats. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_RuntimeComputeMemoryUsage") extern JSValue* HAKO_RuntimeComputeMemoryUsage(JSRuntime* rt, JSContext* ctx);

//! Dumps memory usage as string
//! @param rt Runtime to query
//! @return String with memory stats. Caller owns, free with HAKO_RuntimeFree.
HAKO_EXPORT("HAKO_RuntimeDumpMemoryUsage") extern char* HAKO_RuntimeDumpMemoryUsage(JSRuntime* rt);

//! Checks if promise jobs are pending
//! @param rt Runtime to check
//! @return True if jobs pending, false otherwise
HAKO_EXPORT("HAKO_IsJobPending") extern JS_BOOL HAKO_IsJobPending(JSRuntime* rt);

//! Executes pending promise jobs
//! @param rt Runtime to execute in
//! @param max_jobs_to_execute Maximum jobs to execute, 0 for unlimited
//! @param out_last_job_ctx Output parameter, set to last job's context. Can be NULL. Host borrows.
//! @return Number of jobs executed, or -1 on error
HAKO_EXPORT("HAKO_ExecutePendingJob") extern int32_t HAKO_ExecutePendingJob(JSRuntime* rt, int32_t max_jobs_to_execute, JSContext** out_last_job_ctx);

//! Enables interrupt handler for runtime
//! @param rt Runtime to configure
//! @param opaque User data passed to host handler. Host borrows.
HAKO_EXPORT("HAKO_RuntimeEnableInterruptHandler") extern void HAKO_RuntimeEnableInterruptHandler(JSRuntime* rt, void* opaque);

//! Disables interrupt handler for runtime
//! @param rt Runtime to configure
HAKO_EXPORT("HAKO_RuntimeDisableInterruptHandler") extern void HAKO_RuntimeDisableInterruptHandler(JSRuntime* rt);

//! Sets promise rejection handler for runtime
//! @param rt Runtime to configure
//! @param opaque User data passed to host handler. Host borrows.
HAKO_EXPORT("HAKO_SetPromiseRejectionHandler") extern void HAKO_SetPromiseRejectionHandler(JSRuntime* rt, void* opaque);

//! Clears promise rejection handler for runtime
//! @param rt Runtime to configure
HAKO_EXPORT("HAKO_ClearPromiseRejectionHandler") extern void HAKO_ClearPromiseRejectionHandler(JSRuntime* rt);

//! Enables module loader for runtime
//! @param rt Runtime to configure
//! @param use_custom_normalize True to call host normalize function
//! @param opaque User data passed to host load/normalize functions. Host borrows.
HAKO_EXPORT("HAKO_RuntimeEnableModuleLoader") extern void HAKO_RuntimeEnableModuleLoader(JSRuntime* rt, JS_BOOL use_custom_normalize, void* opaque);

//! Disables module loader for runtime
//! @param rt Runtime to configure
HAKO_EXPORT("HAKO_RuntimeDisableModuleLoader") extern void HAKO_RuntimeDisableModuleLoader(JSRuntime* rt);

//! Throws a reference error
//! @param ctx Context to throw in
//! @param message Error message
HAKO_EXPORT("HAKO_RuntimeJSThrow") extern void HAKO_RuntimeJSThrow(JSContext* ctx, const char* message);

//! Creates a new context
//! @param rt Runtime to create context in
//! @param intrinsics Intrinsic flags, 0 for all standard intrinsics
//! @return New context or NULL on failure. Caller owns, free with HAKO_FreeContext.
HAKO_EXPORT("HAKO_NewContext") extern JSContext* HAKO_NewContext(JSRuntime* rt, HAKO_Intrinsic intrinsics);

//! Sets opaque data for context
//! @param ctx Context to configure
//! @param data User data. Host owns, responsible for freeing.
HAKO_EXPORT("HAKO_SetContextData") extern void HAKO_SetContextData(JSContext* ctx, void* data);

//! Gets opaque data from context
//! @param ctx Context to query
//! @return User data previously set, or NULL. Host owns.
HAKO_EXPORT("HAKO_GetContextData") extern void* HAKO_GetContextData(JSContext* ctx);

//! Frees a context and associated resources
//! @param ctx Context to free, consumed
HAKO_EXPORT("HAKO_FreeContext") extern void HAKO_FreeContext(JSContext* ctx);

//! Gets pointer to undefined constant
//! @return Pointer to static undefined. Never free.
HAKO_EXPORT("HAKO_GetUndefined") extern JSValueConst* HAKO_GetUndefined(void);

//! Gets pointer to null constant
//! @return Pointer to static null. Never free.
HAKO_EXPORT("HAKO_GetNull") extern JSValueConst* HAKO_GetNull(void);

//! Gets pointer to false constant
//! @return Pointer to static false. Never free.
HAKO_EXPORT("HAKO_GetFalse") extern JSValueConst* HAKO_GetFalse(void);

//! Gets pointer to true constant
//! @return Pointer to static true. Never free.
HAKO_EXPORT("HAKO_GetTrue") extern JSValueConst* HAKO_GetTrue(void);

//! Duplicates a value, incrementing refcount
//! @param ctx Context to use
//! @param val Value to duplicate
//! @return New value pointer. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_DupValuePointer") extern JSValue* HAKO_DupValuePointer(JSContext* ctx, JSValueConst* val);

//! Frees a value pointer
//! @param ctx Context that owns the value
//! @param val Value to free, consumed
HAKO_EXPORT("HAKO_FreeValuePointer") extern void HAKO_FreeValuePointer(JSContext* ctx, JSValue* val);

//! Frees a value pointer using runtime
//! @param rt Runtime that owns the value
//! @param val Value to free, consumed
HAKO_EXPORT("HAKO_FreeValuePointerRuntime") extern void HAKO_FreeValuePointerRuntime(JSRuntime* rt, JSValue* val);

//! Allocates memory from context allocator
//! @param ctx Context to allocate from
//! @param size Bytes to allocate
//! @return Allocated memory or NULL. Caller owns, free with HAKO_Free.
HAKO_EXPORT("HAKO_Malloc") extern void* HAKO_Malloc(JSContext* ctx, size_t size);

//! Allocates memory from runtime allocator
//! @param rt Runtime to allocate from
//! @param size Bytes to allocate
//! @return Allocated memory or NULL. Caller owns, free with HAKO_RuntimeFree.
HAKO_EXPORT("HAKO_RuntimeMalloc") extern void* HAKO_RuntimeMalloc(JSRuntime* rt, size_t size);

//! Frees memory allocated by context
//! @param ctx Context that allocated the memory
//! @param ptr Memory to free, consumed
HAKO_EXPORT("HAKO_Free") extern void HAKO_Free(JSContext* ctx, void* ptr);

//! Frees memory allocated by runtime
//! @param rt Runtime that allocated the memory
//! @param ptr Memory to free, consumed
HAKO_EXPORT("HAKO_RuntimeFree") extern void HAKO_RuntimeFree(JSRuntime* rt, void* ptr);

//! Frees a C string returned from JS
//! @param ctx Context that created the string
//! @param str String to free, consumed
HAKO_EXPORT("HAKO_FreeCString") extern void HAKO_FreeCString(JSContext* ctx, const char* str);

//! Throws an error value
//! @param ctx Context to throw in
//! @param error Error value, will be duplicated and consumed by throw
//! @return Exception sentinel. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_Throw") extern JSValue* HAKO_Throw(JSContext* ctx, JSValueConst* error);

//! Throws an error with specified type and message
//! @param ctx Context to throw in
//! @param error_type Error type to throw
//! @param message Error message
//! @return Exception sentinel. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_ThrowError") extern JSValue* HAKO_ThrowError(JSContext* ctx, HAKO_ErrorType error_type, const char* message);

//! Creates a new Error object
//! @param ctx Context to create in
//! @return New Error object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewError") extern JSValue* HAKO_NewError(JSContext* ctx);

//! Gets pending exception from context
//! @param ctx Context to query
//! @param maybe_exception Value to check, or NULL to get any pending exception. Host owns.
//! @return Error object or NULL if no exception. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetLastError") extern JSValue* HAKO_GetLastError(JSContext* ctx, JSValue* maybe_exception);

//! Creates a new empty object
//! @param ctx Context to create in
//! @return New object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewObject") extern JSValue* HAKO_NewObject(JSContext* ctx);

//! Creates a new object with prototype
//! @param ctx Context to create in
//! @param proto Prototype object
//! @return New object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewObjectProto") extern JSValue* HAKO_NewObjectProto(JSContext* ctx, JSValueConst* proto);

//! Creates a new array
//! @param ctx Context to create in
//! @return New array. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewArray") extern JSValue* HAKO_NewArray(JSContext* ctx);

//! Creates an ArrayBuffer from existing memory
//! @param ctx Context to create in
//! @param buffer Memory buffer. Ownership transferred to JS, freed by runtime.
//! @param len Buffer length in bytes
//! @return New ArrayBuffer. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewArrayBuffer") extern JSValue* HAKO_NewArrayBuffer(JSContext* ctx, void* buffer, size_t len);

//! Creates a new typed array with specified length
//! @param ctx Context to create in
//! @param len Array length (element count)
//! @param type Typed array type
//! @return New typed array. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewTypedArray") extern JSValue* HAKO_NewTypedArray(JSContext* ctx, int32_t len, JSTypedArrayEnum type);

//! Creates a typed array view on an ArrayBuffer
//! @param ctx Context to create in
//! @param array_buffer ArrayBuffer to view
//! @param byte_offset Byte offset into buffer
//! @param len Array length (element count)
//! @param type Typed array type
//! @return New typed array. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewTypedArrayWithBuffer") extern JSValue* HAKO_NewTypedArrayWithBuffer(JSContext* ctx, JSValueConst* array_buffer, int32_t byte_offset, int32_t len, JSTypedArrayEnum type);

//! Gets a property by name
//! @param ctx Context to use
//! @param this_val Object to get property from
//! @param prop_name Property name value
//! @return Property value or NULL on error. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetProp") extern JSValue* HAKO_GetProp(JSContext* ctx, JSValueConst* this_val, JSValueConst* prop_name);

//! Gets a property by numeric index
//! @param ctx Context to use
//! @param this_val Object to get property from
//! @param prop_index Property index
//! @return Property value or NULL on error. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetPropNumber") extern JSValue* HAKO_GetPropNumber(JSContext* ctx, JSValueConst* this_val, int32_t prop_index);

//! Sets a property value
//! @param ctx Context to use
//! @param this_val Object to set property on. Host owns.
//! @param prop_name Property name value. Host owns.
//! @param prop_val Property value. Host owns.
//! @return 1 on success, 0 on failure, -1 on exception
HAKO_EXPORT("HAKO_SetProp") extern JS_BOOL HAKO_SetProp(JSContext* ctx, JSValueConst* this_val, JSValueConst* prop_name, JSValueConst* prop_val);

//! Defines a property with descriptor
//! @param ctx Context to use
//! @param this_val Object to define property on
//! @param prop_name Property name value
//! @param desc Property descriptor with value/accessors and flags
//! @return 1 on success, 0 on failure, -1 on exception
HAKO_EXPORT("HAKO_DefineProp") extern JS_BOOL HAKO_DefineProp(JSContext* ctx, JSValueConst* this_val, JSValueConst* prop_name, HAKO_PropDescriptor* desc);

//! Gets own property names from object
//! @param ctx Context to use
//! @param out_prop_ptrs Output array of property name pointers. Caller owns, free each with HAKO_FreeValuePointer, then array with HAKO_Free.
//! @param out_prop_len Output property count
//! @param obj Object to enumerate
//! @param flags Property enumeration flags
//! @return NULL on success, exception value on error. Caller owns exception, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetOwnPropertyNames") extern JSValue* HAKO_GetOwnPropertyNames(JSContext* ctx, JSValue*** out_prop_ptrs, uint32_t* out_prop_len, JSValueConst* obj, int32_t flags);

//! Gets the global object
//! @param ctx Context to use
//! @return Global object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetGlobalObject") extern JSValue* HAKO_GetGlobalObject(JSContext* ctx);

//! Gets length of array or string
//! @param ctx Context to use
//! @param out_len Output length value
//! @param val Object to get length from
//! @return 0 on success, negative on error
HAKO_EXPORT("HAKO_GetLength") extern int32_t HAKO_GetLength(JSContext* ctx, uint32_t* out_len, JSValueConst* val);

//! Creates a new number value
//! @param ctx Context to create in
//! @param num Number value
//! @return New number. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewFloat64") extern JSValue* HAKO_NewFloat64(JSContext* ctx, double num);

//! Creates a new BigInt from 64-bit signed value
//! @param ctx Context to create in
//! @param value 64-bit signed integer value
//! @return New BigInt. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewBigInt") extern JSValue* HAKO_NewBigInt(JSContext* ctx, int64_t value);

//! Creates a new BigInt from 64-bit unsigned value
//! @param ctx Context to create in
//! @param value 64-bit unsigned integer value
//! @return New BigInt. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewBigUInt") extern JSValue* HAKO_NewBigUInt(JSContext* ctx, uint64_t value);

//! Converts a JavaScript value to a 64-bit signed integer
//! @param ctx Context
//! @param val Value to convert. Host owns.
//! @return 64-bit signed integer value, or 0 on error (check for exceptions)
HAKO_EXPORT("HAKO_GetBigInt") extern int64_t HAKO_GetBigInt(JSContext* ctx, JSValueConst* val);

//! Converts a JavaScript value to a 64-bit unsigned integer
//! @param ctx Context
//! @param val Value to convert. Host owns.
//! @return 64-bit unsigned integer value, or 0 on error (check for exceptions)
HAKO_EXPORT("HAKO_GetBigUInt") extern uint64_t HAKO_GetBigUInt(JSContext* ctx, JSValueConst* val);

//! Sets GC threshold for context
//! @param ctx Context to configure
//! @param threshold Threshold in bytes
HAKO_EXPORT("HAKO_SetGCThreshold") extern void HAKO_SetGCThreshold(JSContext* ctx, int64_t threshold);

//! Runs garbage collection
//! @param rt Runtime to run GC on
HAKO_EXPORT("HAKO_RunGC") extern void HAKO_RunGC(JSRuntime* rt);

//! Marks a JavaScript value during garbage collection
//! @param rt Runtime context
//! @param val Value to mark as reachable. Host owns.
//! @param mark_func Mark function provided by QuickJS
HAKO_EXPORT("HAKO_MarkValue") extern void HAKO_MarkValue(JSRuntime* rt, JSValueConst* val, JS_MarkFunc* mark_func);

//! Converts a JavaScript value to a double
//! @param ctx Context
//! @param val Value to convert. Host owns.
//! @return Double value, or NAN on error
HAKO_EXPORT("HAKO_GetFloat64") extern double HAKO_GetFloat64(JSContext* ctx, JSValueConst* val);

//! Creates a new JavaScript string value
//! @param ctx Context
//! @param str C string. Host owns.
//! @return New string value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewString") extern JSValue* HAKO_NewString(JSContext* ctx, const char* str);

//! Converts a JavaScript value to a C string
//! @param ctx Context
//! @param val Value to convert. Host owns.
//! @return C string or NULL on error. Caller owns, free with HAKO_FreeCString.
HAKO_EXPORT("HAKO_ToCString") extern const char* HAKO_ToCString(JSContext* ctx, JSValueConst* val);

//! Creates a new symbol
//! @param ctx Context to create in
//! @param description Symbol description
//! @param is_global True for global symbol (Symbol.for), false for unique
//! @return New symbol. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewSymbol") extern JSValue* HAKO_NewSymbol(JSContext* ctx, const char* description, int32_t is_global);

//! Gets symbol description or key
//! @param ctx Context to use
//! @param val Symbol to query
//! @return Symbol description string. Caller owns, free with HAKO_FreeCString.
HAKO_EXPORT("HAKO_GetSymbolDescriptionOrKey") extern const char* HAKO_GetSymbolDescriptionOrKey(JSContext* ctx, JSValueConst* val);

//! Checks if symbol is global
//! @param ctx Context to use
//! @param val Symbol to check
//! @return True if global symbol
HAKO_EXPORT("HAKO_IsGlobalSymbol") extern JS_BOOL HAKO_IsGlobalSymbol(JSContext* ctx, JSValueConst* val);

//! Gets the type of a JavaScript value
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return HAKOTypeOf enum value
HAKO_EXPORT("HAKO_TypeOf") extern HAKOTypeOf HAKO_TypeOf(JSContext* ctx, JSValueConst* val);

//! Checks if a value is an array
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return 1 if array, 0 if not, -1 on error
HAKO_EXPORT("HAKO_IsArray") extern JS_BOOL HAKO_IsArray(JSContext* ctx, JSValueConst* val);

//! Checks if a value is a typed array
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return 1 if typed array, 0 if not, -1 on error
HAKO_EXPORT("HAKO_IsTypedArray") extern JS_BOOL HAKO_IsTypedArray(JSContext* ctx, JSValueConst* val);

//! Gets the typed array type
//! @param ctx Context
//! @param val Typed array. Host owns.
//! @return JSTypedArrayEnum value (Int8Array, Uint32Array, etc.)
HAKO_EXPORT("HAKO_GetTypedArrayType") extern JSTypedArrayEnum HAKO_GetTypedArrayType(JSContext* ctx, JSValueConst* val);

//! Checks if a value is an ArrayBuffer
//! @param val Value to check. Host owns.
//! @return 1 if ArrayBuffer, 0 if not
HAKO_EXPORT("HAKO_IsArrayBuffer") extern JS_BOOL HAKO_IsArrayBuffer(JSValueConst* val);

//! Checks if two values are equal
//! @param ctx Context
//! @param a First value. Host owns.
//! @param b Second value. Host owns.
//! @param op Equality operation (SameValue, SameValueZero, or StrictEqual)
//! @return 1 if equal, 0 if not, -1 on error
HAKO_EXPORT("HAKO_IsEqual") extern JS_BOOL HAKO_IsEqual(JSContext* ctx, JSValueConst* a, JSValueConst* b, IsEqualOp op);

//! Copies data from an ArrayBuffer
//! @param ctx Context
//! @param val ArrayBuffer value. Host owns.
//! @param out_len Output pointer for buffer length
//! @return New buffer copy or NULL on error. Caller owns, free with HAKO_Free.
HAKO_EXPORT("HAKO_CopyArrayBuffer") extern void* HAKO_CopyArrayBuffer(JSContext* ctx, JSValueConst* val, size_t* out_len);

//! Copies data from a TypedArray
//! @param ctx Context
//! @param val TypedArray value. Host owns.
//! @param out_len Output pointer for buffer length in bytes
//! @return New buffer copy or NULL on error. Caller owns, free with HAKO_Free.
HAKO_EXPORT("HAKO_CopyTypedArrayBuffer") extern void* HAKO_CopyTypedArrayBuffer(JSContext* ctx, JSValueConst* val, size_t* out_len);

//! Creates a new JavaScript function that calls back to host
//! @param ctx Context
//! @param func_id Host function ID to invoke when called
//! @param name Function name. Host owns.
//! @return New function value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewFunction") extern JSValue* HAKO_NewFunction(JSContext* ctx, int32_t func_id, const char* name);

//! Calls a JavaScript function
//! @param ctx Context
//! @param func_obj Function to call. Host owns.
//! @param this_obj This binding. Host owns.
//! @param argc Argument count
//! @param argv_ptrs Array of argument pointers. Host owns.
//! @return Function result. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_Call") extern JSValue* HAKO_Call(JSContext* ctx, JSValueConst* func_obj, JSValueConst* this_obj, int32_t argc, JSValueConst** argv_ptrs);

//! Gets a pointer to an argv element
//! @param argv Arguments array. Host owns.
//! @param index Index to access
//! @return Pointer to value at index. Host owns.
HAKO_EXPORT("HAKO_ArgvGetJSValueConstPointer") extern JSValueConst* HAKO_ArgvGetJSValueConstPointer(JSValueConst* argv, int32_t index);

//! Evaluates JavaScript code
//! @param ctx Context
//! @param js_code JavaScript code string. Host owns.
//! @param js_code_len Code length in bytes
//! @param filename Filename for stack traces. Host owns.
//! @param detect_module Whether to auto-detect ES module syntax
//! @param eval_flags Evaluation flags (JS_EVAL_*)
//! @return Evaluation result. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_Eval") extern JSValue* HAKO_Eval(JSContext* ctx, const char* js_code, size_t js_code_len, const char* filename, JS_BOOL detect_module, int32_t eval_flags);

//! Creates a new promise with resolve/reject functions
//! @param ctx Context
//! @param out_resolve_funcs Output array [resolve, reject]. Caller owns both, free with HAKO_FreeValuePointer.
//! @return New promise. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewPromiseCapability") extern JSValue* HAKO_NewPromiseCapability(JSContext* ctx, JSValue** out_resolve_funcs);

//! Checks if a value is a promise
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return 1 if promise, 0 if not
HAKO_EXPORT("HAKO_IsPromise") extern JS_BOOL HAKO_IsPromise(JSContext* ctx, JSValueConst* val);

//! Gets the state of a promise
//! @param ctx Context
//! @param val Promise value. Host owns.
//! @return JSPromiseStateEnum (pending, fulfilled, or rejected)
HAKO_EXPORT("HAKO_PromiseState") extern JSPromiseStateEnum HAKO_PromiseState(JSContext* ctx, JSValueConst* val);

//! Gets the result/reason of a settled promise
//! @param ctx Context
//! @param val Promise value. Host owns.
//! @return Fulfillment value or rejection reason. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_PromiseResult") extern JSValue* HAKO_PromiseResult(JSContext* ctx, JSValueConst* val);

//! Gets the namespace object of a module
//! @param ctx Context
//! @param module_func_obj Module function. Host owns.
//! @return Module namespace object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetModuleNamespace") extern JSValue* HAKO_GetModuleNamespace(JSContext* ctx, JSValueConst* module_func_obj);

//! Dumps a value to string (for debugging)
//! @param ctx Context
//! @param obj Value to dump. Host owns.
//! @return String representation. Caller owns, free with HAKO_FreeCString.
HAKO_EXPORT("HAKO_Dump") extern const char* HAKO_Dump(JSContext* ctx, JSValueConst* obj);

//! Checks if this is a debug build
//! @return 1 if debug build, 0 if release build
HAKO_EXPORT("HAKO_BuildIsDebug") extern JS_BOOL HAKO_BuildIsDebug();

//! Encodes a value to binary JSON (QuickJS bytecode format)
//! @param ctx Context
//! @param val Value to encode. Host owns.
//! @param out_len Output pointer for buffer length
//! @return Encoded buffer. Caller owns, free with HAKO_Free.
HAKO_EXPORT("HAKO_BJSON_Encode") extern void* HAKO_BJSON_Encode(JSContext* ctx, JSValueConst* val, size_t* out_len);

//! Decodes a value from binary JSON
//! @param ctx Context
//! @param buffer Binary JSON buffer. Host owns.
//! @param len Buffer length in bytes
//! @return Decoded value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_BJSON_Decode") extern JSValue* HAKO_BJSON_Decode(JSContext* ctx, void* buffer, size_t len);

//! Converts a value to JSON string
//! @param ctx Context
//! @param val Value to stringify. Host owns.
//! @param indent Indentation level for formatting
//! @return JSON string value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_ToJson") extern JSValue* HAKO_ToJson(JSContext* ctx, JSValueConst* val, int32_t indent);

//! Parses a JSON string
//! @param ctx Context
//! @param json JSON string. Host owns.
//! @param json_len String length in bytes
//! @param filename Filename for error reporting. Host owns.
//! @return Parsed value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_ParseJson") extern JSValue* HAKO_ParseJson(JSContext* ctx, const char* json, size_t json_len, const char* filename);

//! Checks if a value is an Error object
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return 1 if Error, 0 if not
HAKO_EXPORT("HAKO_IsError") extern JS_BOOL HAKO_IsError(JSContext* ctx, JSValueConst* val);

//! Checks if a value is an exception
//! @param val Value to check. Host owns.
//! @return 1 if exception, 0 if not
HAKO_EXPORT("HAKO_IsException") extern JS_BOOL HAKO_IsException(JSValueConst* val);

//! Checks if a value is a Map object
//! @param val Value to check. Host owns.
//! @return 1 if Map, 0 if not
HAKO_EXPORT("HAKO_IsMap") extern JS_BOOL HAKO_IsMap(JSValueConst* val);

//! Checks if a value is a Set object
//! @param val Value to check. Host owns.
//! @return 1 if Set, 0 if not
HAKO_EXPORT("HAKO_IsSet") extern JS_BOOL HAKO_IsSet(JSValueConst* val);

//! Creates a new Date object
//! @param ctx Context
//! @param time Time value in milliseconds since epoch
//! @return New Date object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewDate") extern JSValue* HAKO_NewDate(JSContext* ctx, double time);

//! Checks if a value is a Date object
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @return 1 if Date, 0 if not
HAKO_EXPORT("HAKO_IsDate") extern JS_BOOL HAKO_IsDate(JSValueConst* val);

//! Gets the timestamp from a Date object
//! @param ctx Context
//! @param val Date value. Host owns.
//! @return Milliseconds since Unix epoch (UTC), or NAN if not a Date
HAKO_EXPORT("HAKO_GetDateTimestamp") extern double HAKO_GetDateTimestamp(JSContext* ctx, JSValueConst* val);

//! Gets the class ID of a value
//! @param val Value to check. Host owns.
//! @return Class ID, or 0 if not an object with a class
HAKO_EXPORT("HAKO_GetClassID") extern JSClassID HAKO_GetClassID(JSValueConst* val);

//! Checks if a value is an instance of a constructor
//! @param ctx Context
//! @param val Value to check. Host owns.
//! @param obj Constructor/class to check against. Host owns.
//! @return 1 if instance, 0 if not, -1 on error
HAKO_EXPORT("HAKO_IsInstanceOf") extern JS_BOOL HAKO_IsInstanceOf(JSContext* ctx, JSValueConst* val, JSValueConst* obj);

//! Gets build information
//! @return Pointer to build info struct. Host owns.
HAKO_EXPORT("HAKO_BuildInfo") extern HakoBuildInfo* HAKO_BuildInfo();

//! Compiles JavaScript code to bytecode
//! @param ctx Context
//! @param js_code JavaScript source code. Host owns.
//! @param js_code_len Code length in bytes
//! @param filename Filename for error reporting. Host owns.
//! @param detect_module Whether to auto-detect ES module syntax
//! @param flags Compilation flags (JS_EVAL_*)
//! @param out_bytecode_len Output pointer for bytecode length
//! @return Bytecode buffer or NULL on error. Caller owns, free with HAKO_Free.
HAKO_EXPORT("HAKO_CompileToByteCode") extern void* HAKO_CompileToByteCode(JSContext* ctx, const char* js_code, size_t js_code_len, const char* filename, JS_BOOL detect_module, int32_t flags, size_t* out_bytecode_len);

//! Evaluates compiled bytecode
//! @param ctx Context
//! @param bytecode_buf Bytecode from HAKO_CompileToByteCode. Host owns.
//! @param bytecode_len Bytecode length in bytes
//! @param load_only If true, loads but doesn't execute (for modules)
//! @return Evaluation result. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_EvalByteCode") extern JSValue* HAKO_EvalByteCode(JSContext* ctx, void* bytecode_buf, size_t bytecode_len, JS_BOOL load_only);

//! Creates a new C module
//! @param ctx Context
//! @param name Module name. Host owns.
//! @return New module definition. Freed when context is freed.
HAKO_EXPORT("HAKO_NewCModule") extern JSModuleDef* HAKO_NewCModule(JSContext* ctx, const char* name);

//! Adds an export declaration to a C module
//! @param ctx Context
//! @param mod Module definition
//! @param export_name Export name. Host owns.
//! @return 0 on success, -1 on error
HAKO_EXPORT("HAKO_AddModuleExport") extern int32_t HAKO_AddModuleExport(JSContext* ctx, JSModuleDef* mod, const char* export_name);

//! Sets the value of a module export
//! @param ctx Context
//! @param mod Module definition
//! @param export_name Export name. Host owns.
//! @param val Export value. Host owns.
//! @return 0 on success, -1 on error
HAKO_EXPORT("HAKO_SetModuleExport") extern int32_t HAKO_SetModuleExport(JSContext* ctx, JSModuleDef* mod, const char* export_name, JSValueConst* val);

//! Gets the name of a module
//! @param ctx Context
//! @param mod Module definition
//! @return Module name. Host owns.
HAKO_EXPORT("HAKO_GetModuleName") extern const char* HAKO_GetModuleName(JSContext* ctx, JSModuleDef* mod);

//! Allocates a new class ID
//! @param out_class_id Output pointer for the new class ID
//! @return The allocated class ID
HAKO_EXPORT("HAKO_NewClassID") extern JSClassID HAKO_NewClassID(JSClassID* out_class_id);

//! Creates and registers a new class
//! @param ctx Context
//! @param class_id Class ID from HAKO_NewClassID
//! @param class_name Class name. Host owns.
//! @param has_finalizer Whether to call finalizer callback on GC
//! @param has_gc_mark Whether to call GC mark callback
//! @return Constructor function. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewClass") extern JSValue* HAKO_NewClass(JSContext* ctx, JSClassID class_id, const char* class_name, JS_BOOL has_finalizer, JS_BOOL has_gc_mark);

//! Sets the prototype for a class
//! @param ctx Context
//! @param class_id Class ID
//! @param proto Prototype object. Host owns.
HAKO_EXPORT("HAKO_SetClassProto") extern void HAKO_SetClassProto(JSContext* ctx, JSClassID class_id, JSValueConst* proto);

//! Links constructor and prototype (sets .prototype and .constructor)
//! @param ctx Context
//! @param constructor Constructor function. Host owns.
//! @param proto Prototype object. Host owns.
HAKO_EXPORT("HAKO_SetConstructor") extern void HAKO_SetConstructor(JSContext* ctx, JSValueConst* constructor, JSValueConst* proto);

//! Creates a new instance of a class
//! @param ctx Context
//! @param class_id Class ID
//! @return New object instance. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewObjectClass") extern JSValue* HAKO_NewObjectClass(JSContext* ctx, JSClassID class_id);

//! Sets opaque data on a class instance
//! @param obj Object instance. Host owns.
//! @param opaque Opaque data pointer. Host owns.
HAKO_EXPORT("HAKO_SetOpaque") extern void HAKO_SetOpaque(JSValueConst* obj, void* opaque);

//! Gets opaque data from a class instance
//! @param ctx Context
//! @param obj Object instance. Host owns.
//! @param class_id Expected class ID (for type safety)
//! @return Opaque data pointer, or NULL if wrong class. Host owns.
HAKO_EXPORT("HAKO_GetOpaque") extern void* HAKO_GetOpaque(JSContext* ctx, JSValueConst* obj, JSClassID class_id);

//! Creates a new object with prototype and class
//! @param ctx Context
//! @param proto Prototype object. Host owns.
//! @param class_id Class ID
//! @return New object. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_NewObjectProtoClass") extern JSValue* HAKO_NewObjectProtoClass(JSContext* ctx, JSValueConst* proto, JSClassID class_id);

//! Sets a private value on a module
//! @param ctx Context
//! @param mod Module definition
//! @param val Private value. Caller should free with HAKO_FreeValuePointer after calling.
HAKO_EXPORT("HAKO_SetModulePrivateValue") extern void HAKO_SetModulePrivateValue(JSContext* ctx, JSModuleDef* mod, JSValue* val);

//! Gets the private value from a module
//! @param ctx Context
//! @param mod Module definition
//! @return Private value. Caller owns, free with HAKO_FreeValuePointer.
HAKO_EXPORT("HAKO_GetModulePrivateValue") extern JSValue* HAKO_GetModulePrivateValue(JSContext* ctx, JSModuleDef* mod);

//! Checks if a value is null
//! @param val Value to check. Host owns.
//! @return 1 if null, 0 if not
HAKO_EXPORT("HAKO_IsNull") extern JS_BOOL HAKO_IsNull(JSValueConst* val);

//! Checks if a value is undefined
//! @param val Value to check. Host owns.
//! @return 1 if undefined, 0 if not
HAKO_EXPORT("HAKO_IsUndefined") extern JS_BOOL HAKO_IsUndefined(JSValueConst* val);

//! Checks if a value is null or undefined
//! @param val Value to check. Host owns.
//! @return 1 if null or undefined, 0 otherwise
HAKO_EXPORT("HAKO_IsNullOrUndefined") extern JS_BOOL HAKO_IsNullOrUndefined(JSValueConst* val);

//! Initializes the TypeScript type stripper
//! Must be called before using HAKO_StripTypes
//! @param rt Runtime to associate with the stripper
//! @return HAKO_STATUS_SUCCESS on success, error code on failure
HAKO_EXPORT("HAKO_InitTypeStripper") extern HAKO_Status HAKO_InitTypeStripper(JSRuntime* rt);

//! Cleans up the TypeScript type stripper
//! Should be called during shutdown
//! @param rt Runtime associated with the stripper
HAKO_EXPORT("HAKO_CleanupTypeStripper") extern void HAKO_CleanupTypeStripper(JSRuntime* rt);

//! Strips TypeScript type annotations from source code
//! @param rt Runtime to use
//! @param typescript_source Input TypeScript source code. Host owns.
//! @param javascript_out Output parameter for JavaScript code. Caller owns, free with HAKO_RuntimeFree.
//! @param javascript_len Output parameter for JavaScript length
//! @return HAKO_Status indicating success or specific error type
HAKO_EXPORT("HAKO_StripTypes") extern HAKO_Status HAKO_StripTypes(JSRuntime* rt, const char* typescript_source, char** javascript_out, size_t* javascript_len);

#ifdef __cplusplus
}
#endif

#endif /* HAKO_H */