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
#include "quickjs.h"
#include "quickjs-libc.h"

#include <stdint.h>
#include <stdio.h>

#define CAPTURE_COUNT_MAX 255

FILE *outfile=NULL;
JSRuntime *rt;
JSContext *ctx;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (outfile == NULL) {
        outfile = fopen("/dev/null", "w");
        rt = JS_NewRuntime();
        // 64 Mo
        JS_SetMemoryLimit(rt, 0x4000000);
        // 64 Kb
        JS_SetMaxStackSize(rt, 0x10000);
        ctx = JS_NewContextRaw(rt);
    }
    int len, ret, i;
    uint8_t *bc;
    char error_msg[64];
    const uint8_t *input;
    uint8_t *capture[CAPTURE_COUNT_MAX * 2];
    int capture_count;
    size_t size1 = size;

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
                     size1, 0, ctx);
    if (!bc) {
        return 0;
    }
    input = data + size1 + 1;
    ret = lre_exec(capture, bc, input, 0, size - (size1 + 1), 0, ctx);
    if (ret == 1) {
        capture_count = lre_get_capture_count(bc);
        for(i = 0; i < 2 * capture_count; i++) {
            uint8_t *ptr;
            ptr = capture[i];
            fprintf(outfile, "%d: ", i);
            if (!ptr)
                fprintf(outfile, "<nil>");
            else
                fprintf(outfile, "%u", (int)(ptr - (uint8_t *)input));
            fprintf(outfile, "\n");
        }
    }
    free(bc);

    return 0;
}
