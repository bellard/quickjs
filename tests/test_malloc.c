#include "../quickjs.h"

#include <stdint.h>

int main(void)
{
    JSRuntime *rt = JS_NewRuntime();
    uint8_t *ptr;

    if (rt == NULL)
        return 1;

    if (js_malloc_rt(rt, SIZE_MAX) != NULL)
        return 2;
    if (js_mallocz_rt(rt, SIZE_MAX) != NULL)
        return 3;

    ptr = js_malloc_rt(rt, 16);
    if (ptr == NULL)
        return 4;
    ptr[0] = 0x41;

    if (js_realloc_rt(rt, ptr, SIZE_MAX) != NULL)
        return 5;
    if (ptr[0] != 0x41)
        return 6;

    js_free_rt(rt, ptr);
    JS_FreeRuntime(rt);
    return 0;
}
