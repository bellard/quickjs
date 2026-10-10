/*
 * QuickJS Temporal.Now implementation (minimal)
 */

#include <time.h>

#include "../quickjs.h"
#include "../cutils.h"
#include "temporal_private.h"

static int now_get_system_time(JSTimeSpec *ts)
{
    struct timespec t;
    if (clock_gettime(CLOCK_REALTIME, &t) != 0)
        return -1;
    ts->secs = t.tv_sec;
    ts->nsecs = (uint32_t)t.tv_nsec;
    return 0;
}

/* Temporal.Now.instant() */
static JSValue js_now_instant(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    (void)argc;
    (void)argv;
    (void)this_val;
    JSTimeSpec ts;
    if (now_get_system_time(&ts) < 0)
        return JS_ThrowInternalError(ctx, "cannot get system time");
    return js_create_instant(ctx, &ts);
}

/* Get canonical timezone string. Returns a newly allocated C string
   that must be freed with js_free, or NULL on error/exception. */
static char *now_get_canonical_timezone(JSContext *ctx, JSValueConst val)
{
    const char *str;
    char buf[64];
    int64_t offset_ns;
    int ret;

    if (JS_IsUndefined(val)) {
        char tz[TI_TIMEZONE_BUF_SIZE];
        ti_get_default_timezone(tz, sizeof(tz));
        return js_strdup(ctx, tz);
    }

    if (!JS_IsString(val)) {
        JS_ThrowTypeError(ctx, "invalid time zone");
        return NULL;
    }

    str = JS_ToCString(ctx, val);
    if (!str)
        return NULL;

    ret = ti_parse_temporal_timezone_string(str, &offset_ns);
    if (ret < 0) {
        JS_FreeCString(ctx, str);
        JS_ThrowRangeError(ctx, "invalid time zone");
        return NULL;
    }

    if (ti_canonicalize_timezone_identifier(str, offset_ns, buf, sizeof(buf)) < 0) {
        JS_FreeCString(ctx, str);
        JS_ThrowRangeError(ctx, "invalid time zone");
        return NULL;
    }

    JS_FreeCString(ctx, str);
    return js_strdup(ctx, buf);
}

/* Temporal.Now.plainDateISO(timeZone) */
static JSValue js_now_plainDateISO(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    JSTimeSpec ts;
    int64_t year;
    int month, day;
    char *tz_str;

    tz_str = now_get_canonical_timezone(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!tz_str)
        return JS_EXCEPTION;

    if (now_get_system_time(&ts) < 0) {
        js_free(ctx, tz_str);
        return JS_ThrowInternalError(ctx, "cannot get system time");
    }

    ti_zoneddatetime_to_local(tz_str, ts.secs, (int)ts.nsecs,
                               &year, &month, &day, NULL, NULL, NULL, NULL, NULL, NULL);
    js_free(ctx, tz_str);
    return js_create_plaindate(ctx, (int)year, month, day);
}

/* Temporal.Now.plainTimeISO(timeZone) */
static JSValue js_now_plainTimeISO(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    JSTimeSpec ts;
    JSPlainTimeData pt = {0};
    char *tz_str;

    tz_str = now_get_canonical_timezone(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!tz_str)
        return JS_EXCEPTION;

    if (now_get_system_time(&ts) < 0) {
        js_free(ctx, tz_str);
        return JS_ThrowInternalError(ctx, "cannot get system time");
    }

    ti_zoneddatetime_to_local(tz_str, ts.secs, (int)ts.nsecs,
                               NULL, NULL, NULL,
                               &pt.hour, &pt.minute, &pt.second,
                               &pt.millisecond, &pt.microsecond, &pt.nanosecond);
    js_free(ctx, tz_str);
    return js_create_plaintime(ctx, &pt);
}

/* Temporal.Now.plainDateTimeISO(timeZone) */
static JSValue js_now_plainDateTimeISO(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val;
    JSTimeSpec ts;
    int64_t year;
    int month, day, hour, minute, second, ms, us, ns;
    char *tz_str;

    tz_str = now_get_canonical_timezone(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!tz_str)
        return JS_EXCEPTION;

    if (now_get_system_time(&ts) < 0) {
        js_free(ctx, tz_str);
        return JS_ThrowInternalError(ctx, "cannot get system time");
    }

    ti_zoneddatetime_to_local(tz_str, ts.secs, (int)ts.nsecs,
                               &year, &month, &day,
                               &hour, &minute, &second,
                               &ms, &us, &ns);
    js_free(ctx, tz_str);
    return js_create_plaindatetime(ctx, (int)year, month, day,
                                    hour, minute, second, ms, us, ns);
}

/* Temporal.Now.zonedDateTimeISO([timeZone]) */
static JSValue js_now_zonedDateTimeISO(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val;
    JSTimeSpec ts;
    JSValue result;
    char *tz_str;

    tz_str = now_get_canonical_timezone(ctx, argc > 0 ? argv[0] : JS_UNDEFINED);
    if (!tz_str)
        return JS_EXCEPTION;

    if (now_get_system_time(&ts) < 0) {
        js_free(ctx, tz_str);
        return JS_ThrowInternalError(ctx, "cannot get system time");
    }

    result = js_create_zoneddatetime(ctx, &ts, tz_str, CAL_ISO8601);
    js_free(ctx, tz_str);
    return result;
}

/* Temporal.Now.timeZoneId() */
static JSValue js_now_timeZoneId(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)ctx;
    (void)this_val;
    JSValue r;
    char tz[TI_TIMEZONE_BUF_SIZE];
    (void)argc; (void)argv;
    ti_get_default_timezone(tz, sizeof(tz));
    r = JS_NewString(ctx, tz);
    return r;
}

static const JSCFunctionListEntry js_now_funcs[] = {
    JS_CFUNC_DEF("instant", 0, js_now_instant),
    JS_CFUNC_DEF("plainDateISO", 0, js_now_plainDateISO),
    JS_CFUNC_DEF("plainDateTimeISO", 0, js_now_plainDateTimeISO),
    JS_CFUNC_DEF("plainTimeISO", 0, js_now_plainTimeISO),
    JS_CFUNC_DEF("zonedDateTimeISO", 0, js_now_zonedDateTimeISO),
    JS_CFUNC_DEF("timeZoneId", 0, js_now_timeZoneId),
    JS_PROP_STRING_DEF("[Symbol.toStringTag]", "Temporal.Now", JS_PROP_CONFIGURABLE),
};

int js_add_now(JSContext *ctx, JSValue temporal_obj)
{
    JSValue now_obj;
    now_obj = JS_NewObject(ctx);
    if (JS_IsException(now_obj))
        return -1;
    if (JS_SetPropertyFunctionList(ctx, now_obj, js_now_funcs,
                                    countof(js_now_funcs)) < 0) {
        JS_FreeValue(ctx, now_obj);
        return -1;
    }
    if (JS_DefinePropertyValueStr(ctx, temporal_obj, "Now", now_obj,
                                   JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE) < 0) {
        JS_FreeValue(ctx, now_obj);
        return -1;
    }
    return 0;
}
