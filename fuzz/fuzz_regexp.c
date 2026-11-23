/* Copyright 2020 Google Inc.

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "libregexp.h"
#include "quickjs-libc.h"

static int nbinterrupts = 0;

/* same check as js_check_stack_overflow() in quickjs.c: the regexp parser
   is recursive, so deeply nested patterns must fail instead of crashing */
#define FUZZ_STACK_SIZE (1024 * 1024)

static uintptr_t stack_limit;

/* Note: OS and CPU dependent */
static inline uintptr_t get_stack_pointer(void)
{
    return (uintptr_t)__builtin_frame_address(0);
}

int lre_check_stack_overflow(void *opaque, size_t alloca_size)
{
    return get_stack_pointer() - alloca_size < stack_limit;
}

void *lre_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

int lre_check_timeout(void *opaque)
 {
    nbinterrupts++;
    return (nbinterrupts > 100);
 }

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int len, ret, i;
    uint8_t *bc;
    char error_msg[64];
    const uint8_t *input;
    /* lre_exec() needs lre_get_alloc_count() entries: 2 per capture plus
       the registers, each count being at most 255 */
    uint8_t *capture[255 * 3];
    size_t size1 = size;

    stack_limit = get_stack_pointer() - FUZZ_STACK_SIZE;

    //Splits buffer into 2 sub buffers delimited by null character
    for (i = 0; i < size; i++) {
        if (data[i] == 0) {
            size1 = i;
            break;
        }
    }
    if (size1 == size) {
        //missing delimiter
        return 0;
    }
    bc = lre_compile(&len, error_msg, sizeof(error_msg), (const char *) data,
                     size1, 0, NULL);
    if (!bc) {
        return 0;
    }
    input = data + size1 + 1;
    ret = lre_exec(capture, bc, input, 0, size - (size1 + 1), 0, NULL);
    if (ret == 1) {
        lre_get_capture_count(bc);
    }
    free(bc);

    return 0;
}
