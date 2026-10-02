/* RegExp stress with one independent runtime and context per native thread. */

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "quickjs.h"

#define THREAD_COUNT 4

typedef struct ThreadState {
    int index;
    int failed;
} ThreadState;

static const char regexp_stress_source[] =
    "for (let i = 0; i < 500; i++) {\n"
    "  let re = /abcdef[0-9]+/g;\n"
    "  re.compile(i & 1 ? '(a|b)+' : '[a-z]bcdef', i & 1 ? 'g' : '');\n"
    "  let match = re.exec(i & 1 ? 'bbbb' : '00xbcdef');\n"
    "  if (!match || (i & 1 ? match[0] !== 'bbbb' : match[0] !== 'xbcdef'))\n"
    "    throw new Error('compile transition mismatch');\n"
    "  if ('00\\\"x\\\" \\\\\\\"x\\\"'.replace(/(^|[^\\\\])\\\"x\\\"/g, 'X') !== '0X \\\\\\\"x\\\"')\n"
    "    throw new Error('leading candidate mismatch');\n"
    "}\n";

static void *regexp_thread(void *opaque)
{
    ThreadState *state = opaque;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue result;

    rt = JS_NewRuntime();
    if (!rt) {
        state->failed = 1;
        return NULL;
    }
    JS_UpdateStackTop(rt);
    ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        state->failed = 1;
        return NULL;
    }
    result = JS_Eval(ctx, regexp_stress_source,
                     strlen(regexp_stress_source),
                     "regexp-thread-test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *message = JS_ToCString(ctx, exception);

        fprintf(stderr, "regexp thread %d failed: %s\n", state->index,
                message ? message : "unknown exception");
        JS_FreeCString(ctx, message);
        JS_FreeValue(ctx, exception);
        state->failed = 1;
    }
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return NULL;
}

int main(void)
{
    pthread_t threads[THREAD_COUNT];
    ThreadState states[THREAD_COUNT];
    int i, created;

    created = 0;
    for(i = 0; i < THREAD_COUNT; i++) {
        states[i].index = i;
        states[i].failed = 0;
        if (pthread_create(&threads[i], NULL, regexp_thread, &states[i])) {
            fprintf(stderr, "could not create regexp thread %d\n", i);
            break;
        }
        created++;
    }
    for(i = 0; i < created; i++)
        pthread_join(threads[i], NULL);
    if (created != THREAD_COUNT)
        return 1;
    for(i = 0; i < THREAD_COUNT; i++) {
        if (states[i].failed)
            return 1;
    }
    return 0;
}
