#include "ts_strip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Test framework
typedef struct {
    int passed;
    int failed;
    int total;
    bool current_test_failed;
} test_stats_t;

static test_stats_t g_stats = {0, 0, 0, false};

void test_start(const char* test_name) {
    printf("Running: %s\n", test_name);
    g_stats.current_test_failed = false;
    g_stats.total++;
}

void test_end() {
    if (g_stats.current_test_failed) {
        g_stats.failed++;
        printf("  FAILED\n\n");
    } else {
        g_stats.passed++;
        printf("  PASSED\n\n");
    }
}

void assert_equal_str(const char* actual, const char* expected, const char* message) {
    if (strcmp(actual, expected) != 0) {
        printf("  ASSERTION FAILED: %s\n", message);
        printf("  Expected (%zu bytes): \"%s\"\n", strlen(expected), expected);
        printf("  Actual   (%zu bytes): \"%s\"\n", strlen(actual), actual);
        g_stats.current_test_failed = true;
    }
}

void assert_equal_size(size_t actual, size_t expected, const char* message) {
    if (actual != expected) {
        printf("  ASSERTION FAILED: %s\n", message);
        printf("  Expected: %zu\n", expected);
        printf("  Actual:   %zu\n", actual);
        g_stats.current_test_failed = true;
    }
}

void assert_true(bool condition, const char* message) {
    if (!condition) {
        printf("  ASSERTION FAILED: %s\n", message);
        g_stats.current_test_failed = true;
    }
}

void assert_contains(const char* haystack, const char* needle, const char* message) {
    if (!strstr(haystack, needle)) {
        printf("  ASSERTION FAILED: %s\n", message);
        printf("  Expected to contain \"%s\"\n", needle);
        g_stats.current_test_failed = true;
    }
}

void print_test_summary() {
    printf("\n=== Test Summary ===\n");
    printf("Total:   %d\n", g_stats.total);
    printf("Passed:  %d\n", g_stats.passed);
    printf("Failed:  %d\n", g_stats.failed);
    printf("Success: %.1f%%\n", g_stats.total > 0 ? (float)g_stats.passed / g_stats.total * 100.0f : 0.0f);
}

// Helper functions
bool test_strip_success(ts_strip_ctx_t* ctx, const char* input, char** output, size_t* output_len) {
    ts_strip_result_t result = ts_strip_with_ctx(ctx, input, output, output_len);
    return result == TS_STRIP_SUCCESS;
}

bool test_strip_unsupported(ts_strip_ctx_t* ctx, const char* input, char** output, size_t* output_len) {
    ts_strip_result_t result = ts_strip_with_ctx(ctx, input, output, output_len);
    return result == TS_STRIP_ERROR_UNSUPPORTED;
}

int count_newlines(const char* str) {
    int count = 0;
    for (const char* p = str; *p; p++) {
        if (*p == '\n') count++;
    }
    return count;
}

// ============================================================================
// ERROR TESTS - These should return UNSUPPORTED and leave input unchanged
// ============================================================================

void test_errors_on_enums(ts_strip_ctx_t* ctx) {
    test_start("errors on enums");
    
    const char* input = 
        "\n"
        "       enum E1 {}\n"
        "       export enum E2 {}\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Enum should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_parameter_properties(ts_strip_ctx_t* ctx) {
    test_start("errors on parameter properties");
    
    const char* input = 
        "\n"
        "        class C {\n"
        "            constructor(public a, private b, protected c, readonly d) {}\n"
        "        }\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Parameter properties should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_typescript_module_declarations(ts_strip_ctx_t* ctx) {
    test_start("errors on TypeScript module declarations");
    
    const char* input = 
        "\n"
        "        module A {}\n"
        "        module B { export type T = string; }\n"
        "        module C { export const V = \"\"; }\n"
        "        module D.E {}\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Module declarations should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_instantiated_namespaces(ts_strip_ctx_t* ctx) {
    test_start("errors on instantiated namespaces");
    
    const char* input = 
        "\n"
        "        namespace A { 1; }\n"
        "        namespace B { globalThis; }\n"
        "        namespace C { export let x; }\n"
        "        namespace D { declare let x; }\n"
        "        namespace E { export type T = any; 2; }\n"
        "        namespace F { export namespace Inner { 3; } }\n"
        "        namespace G.H { 4; }\n"
        "        namespace I { export import X = E.T }\n"
        "        namespace J { {} }\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Instantiated namespaces should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_importing_instantiated_namespace(ts_strip_ctx_t* ctx) {
    test_start("importing instantiated namespace");
    
    const char* input = 
        "\n"
        "        namespace A { export let x = 1; }\n"
        "        namespace B { import x = A.x; }\n"
        "        namespace C { export import x = A.x; }\n"
        "        ";
    
    const char* expected = 
        "\n"
        "        namespace A { export let x = 1; }\n"
        "        ;                              \n"
        "        namespace C { export import x = A.x; }\n"
        "        ";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    if (success) {
        assert_equal_str(output, expected, "Should blank non-instantiated namespace B only");
    } else {
        bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
        assert_true(is_unsupported, "Mixed case should be handled consistently");
    }
    
    free(output);
    test_end();
}

void test_errors_on_declared_legacy_modules(ts_strip_ctx_t* ctx) {
    test_start("errors on declared legacy modules");
    
    const char* input = "declare module M {}\n";
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Declared legacy modules should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_non_instantiated_legacy_modules(ts_strip_ctx_t* ctx) {
    test_start("errors on non-instantiated legacy modules");
    
    const char* input = "module M {}\n";
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Non-instantiated legacy modules should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_cjs_export_assignment(ts_strip_ctx_t* ctx) {
    test_start("errors on CJS export assignment syntax");
    
    const char* input = 
        "\n"
        "        export = 1;\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "CJS export assignment should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_cjs_import(ts_strip_ctx_t* ctx) {
    test_start("errors on CJS import syntax");
    
    const char* input = 
        "\n"
        "        import lib = require(\"\");\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "CJS import should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_prefix_type_assertion(ts_strip_ctx_t* ctx) {
    test_start("errors on prefix type assertion");
    
    const char* input = "let x = <string>\"test\";";
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Prefix type assertion should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

void test_errors_on_prefix_type_assertion_in_arrow(ts_strip_ctx_t* ctx) {
    test_start("errors on prefix type assertion in arrow body");
    
    const char* input = "(()=><any>{p:null}.p ?? 1);";
    char* output;
    size_t output_len;
    bool is_unsupported = test_strip_unsupported(ctx, input, &output, &output_len);
    
    assert_true(is_unsupported, "Prefix type assertion in arrow should be reported as unsupported");
    assert_equal_str(output, input, "Output should be unchanged when unsupported");
    
    free(output);
    test_end();
}

// ============================================================================
// SUCCESS TESTS - These should process successfully
// ============================================================================

void test_handles_arrow_on_new_line(ts_strip_ctx_t* ctx) {
    test_start("handles arrow function with newlines");
    
    const char* input = "[1].map((v)\n:number[\n]=>[v]);";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle arrow on new line");
    
    int input_newlines = count_newlines(input);
    int output_newlines = count_newlines(output);
    assert_equal_size(output_newlines, input_newlines, "Line count should not change");
    
    assert_contains(output, "(v)", "Parameter should be preserved");
    assert_contains(output, "[v]", "Return expression should be preserved");
    
    free(output);
    test_end();
}

void test_handles_blanking_multibyte_characters(ts_strip_ctx_t* ctx) {
    test_start("handles blanking multibyte UTF-8 characters");
    
    // Test with a 4-byte UTF-8 character (U+1F4A5 COLLISION SYMBOL)
    const char* input = "function f(): \"\xF0\x9F\x92\xA5\" {}";
    const char* expected = "function f()         {}"; // 9 spaces (8 from type annotation + 1 existing)
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle multibyte characters");
    assert_equal_str(output, expected, "Should blank preserving byte length");
    assert_equal_size(strlen(output), strlen(input), "Byte length should match input");
    
    free(output);
    test_end();
}

void test_handles_default_export(ts_strip_ctx_t* ctx) {
    test_start("handles default export");
    
    const char* input = 
        "\n"
        "        export default/**/1/**/;\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle default export");
    assert_equal_str(output, input, "Default export should be unchanged");
    
    free(output);
    test_end();
}

void test_allows_ambient_enum(ts_strip_ctx_t* ctx) {
    test_start("allows ambient enum");
    
    const char* input = "declare enum E1 {}\n";
    const char* expected = "                  \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle ambient enum");
    assert_equal_str(output, expected, "Ambient enum should be completely blanked");
    
    free(output);
    test_end();
}

void test_allows_declared_namespace(ts_strip_ctx_t* ctx) {
    test_start("allows declared namespace");
    
    const char* input = "declare namespace N {}\n";
    const char* expected = "                      \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle declared namespace");
    assert_equal_str(output, expected, "Declared namespace should be completely blanked");
    
    free(output);
    test_end();
}

void test_allows_declared_module_augmentation(ts_strip_ctx_t* ctx) {
    test_start("allows declared module augmentation");
    
    const char* input = "declare module \"\" {}\n";
    const char* expected = "                    \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle declared module augmentation");
    assert_equal_str(output, expected, "Declared module should be completely blanked");
    
    free(output);
    test_end();
}

void test_allows_declared_global_augmentation(ts_strip_ctx_t* ctx) {
    test_start("allows declared global augmentation");
    
    const char* input = "declare global {}\n";
    const char* expected = "                 \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle declared global augmentation");
    assert_equal_str(output, expected, "Declared global should be completely blanked");
    
    free(output);
    test_end();
}

void test_tsx_is_preserved(ts_strip_ctx_t* ctx) {
    test_start("TSX is preserved in output");
    
    const char* input = "const elm = <div>{x as string}</div>;\n";
    const char* expected = "const elm = <div>{x          }</div>;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle TSX");
    assert_equal_str(output, expected, "TSX should be preserved with type assertion blanked");
    
    free(output);
    test_end();
}

void test_handles_variable_definite_assignment(ts_strip_ctx_t* ctx) {
    test_start("handles variable definite assignment assertions");
    
    const char* input = "let x: any, y! : string, z: any;\n";
    const char* expected = "let x     , y          , z     ;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle definite assignment assertions");
    assert_equal_str(output, expected, "Should remove ! and type annotations");
    
    free(output);
    test_end();
}

void test_parse_generic_arrow_rather_than_left_shift(ts_strip_ctx_t* ctx) {
    test_start("parseGenericArrowRatherThanLeftShift");
    
    const char* input = 
        "\n"
        "        function foo<T>(_x: T) {}\n"
        "        const b = foo<<T>(x: T) => number>(() => 1);\n"
        "    ";
    
    const char* expected = 
        "\n"
        "        function foo   (_x   ) {}\n"
        "        const b = foo                     (() => 1);\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle generic arrow vs left shift");
    assert_equal_str(output, expected, "Should properly parse and blank generic types");
    
    free(output);
    test_end();
}

void test_preserves_strict_directive(ts_strip_ctx_t* ctx) {
    test_start("preserves strict directive after type declaration");
    
    const char* input = 
        "\n"
        "interface I {}\n"
        "\"use strict\"\n"
        "export {}\n"
        "    ";
    
    const char* expected = 
        "\n"
        "              \n"
        "\"use strict\"\n"
        "export {}\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully preserve strict directive");
    assert_equal_str(output, expected, "Should blank interface but preserve strict directive");
    
    free(output);
    test_end();
}

void test_preserves_nested_strict_directive(ts_strip_ctx_t* ctx) {
    test_start("preserves nested strict directive");
    
    const char* input = 
        "\n"
        "    function foo() {\n"
        "        interface I {}\n"
        "        \"use strict\"\n"
        "        return 1;\n"
        "    }\n"
        "    ";
    
    const char* expected = 
        "\n"
        "    function foo() {\n"
        "                      \n"
        "        \"use strict\"\n"
        "        return 1;\n"
        "    }\n"
        "    ";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully preserve nested strict directive");
    assert_equal_str(output, expected, "Should blank interface but preserve nested strict directive");
    
    free(output);
    test_end();
}

void test_basic_type_stripping(ts_strip_ctx_t* ctx) {
    test_start("basic type stripping");
    
    const char* input = "let x: number = 1;\n";
    const char* expected = "let x         = 1;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully strip basic types");
    assert_equal_str(output, expected, "Type annotation should be blanked");
    
    free(output);
    test_end();
}

void test_interface_removal(ts_strip_ctx_t* ctx) {
    test_start("interface removal");
    
    const char* input = "interface Foo { x: number; }\n";
    const char* expected = "                            \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully remove interfaces");
    assert_equal_str(output, expected, "Interface should be completely blanked");
    
    free(output);
    test_end();
}

void test_type_alias_removal(ts_strip_ctx_t* ctx) {
    test_start("type alias removal");
    
    const char* input = "type Foo = number;\n";
    const char* expected = "                  \n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully remove type aliases");
    assert_equal_str(output, expected, "Type alias should be completely blanked");
    
    free(output);
    test_end();
}

void test_as_expression(ts_strip_ctx_t* ctx) {
    test_start("as expression");
    
    const char* input = "const x = foo as string;\n";
    const char* expected = "const x = foo          ;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle as expressions");
    assert_equal_str(output, expected, "As expression should be blanked");
    
    free(output);
    test_end();
}

void test_satisfies_expression(ts_strip_ctx_t* ctx) {
    test_start("satisfies expression");
    
    const char* input = "const x = foo satisfies string;\n";
    const char* expected = "const x = foo                 ;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle satisfies expressions");
    assert_equal_str(output, expected, "Satisfies expression should be blanked");
    
    free(output);
    test_end();
}

void test_non_null_assertion(ts_strip_ctx_t* ctx) {
    test_start("non-null assertion");
    
    const char* input = "const x = foo!;\n";
    const char* expected = "const x = foo ;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle non-null assertions");
    assert_equal_str(output, expected, "Non-null assertion should be blanked");
    
    free(output);
    test_end();
}

void test_inline_type_import_specifier(ts_strip_ctx_t* ctx) {
    test_start("inline type import specifier removal");

    const char* input =
        "import {\n"
        "    defineGenerator,\n"
        "    type GeneratorArtifact,\n"
        "    type GeneratorContext,\n"
        "} from \"@bebop/sdk/ge\";\n";

    const char* expected =
        "import {\n"
        "    defineGenerator,\n"
        "         GeneratorArtifact,\n"
        "         GeneratorContext,\n"
        "} from \"@bebop/sdk/ge\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle inline type import specifiers");
    assert_equal_str(output, expected, "Type keyword before import specifiers should be blanked");

    free(output);
    test_end();
}

void test_inline_type_export_specifier(ts_strip_ctx_t* ctx) {
    test_start("inline type export specifier removal");

    const char* input = "export { type Foo, bar, type Baz } from \"./module\";\n";
    const char* expected = "export {      Foo, bar,      Baz } from \"./module\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle inline type export specifiers");
    assert_equal_str(output, expected, "Type keyword before export specifiers should be blanked");

    free(output);
    test_end();
}

void test_import_type_named(ts_strip_ctx_t* ctx) {
    test_start("import type { Foo } blanked entirely");

    const char* input = "import type { Foo } from \"./module\";\n";
    const char* expected = "                                    \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle import type named");
    assert_equal_str(output, expected, "Full type import should be blanked entirely");

    free(output);
    test_end();
}

void test_import_type_default(ts_strip_ctx_t* ctx) {
    test_start("import type Bar blanked entirely");

    const char* input = "import type Bar from \"./module\";\n";
    const char* expected = "                                \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle import type default");
    assert_equal_str(output, expected, "Default type import should be blanked entirely");

    free(output);
    test_end();
}

void test_export_type_named(ts_strip_ctx_t* ctx) {
    test_start("export type { Foo } blanked entirely");

    const char* input = "export type { Foo };\n";
    const char* expected = "                    \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle export type named");
    assert_equal_str(output, expected, "Full type export should be blanked entirely");

    free(output);
    test_end();
}

void test_mixed_import_with_inline_types(ts_strip_ctx_t* ctx) {
    test_start("mixed import with inline type specifiers");

    const char* input = "import { createCatName, type Cat, type Dog } from \"./animal\";\n";
    const char* expected = "import { createCatName,      Cat,      Dog } from \"./animal\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle mixed import with inline types");
    assert_equal_str(output, expected, "Inline type keywords should be blanked, values preserved");

    free(output);
    test_end();
}

void test_import_type_with_alias(ts_strip_ctx_t* ctx) {
    test_start("import type with alias blanked entirely");

    const char* input = "import type { UserId as IdType } from './types';\n";
    const char* expected = "                                                \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle import type with alias");
    assert_equal_str(output, expected, "Import type with alias should be blanked entirely");

    free(output);
    test_end();
}

// ============================================================================
// PURE JAVASCRIPT TESTS - Should pass through unchanged
// ============================================================================

void test_pure_js_variables(ts_strip_ctx_t* ctx) {
    test_start("pure JS: variable declarations");

    const char* input = "let x = 1;\nconst y = 2;\nvar z = 3;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_function(ts_strip_ctx_t* ctx) {
    test_start("pure JS: function declaration");
    
    const char* input = "function add(a, b) {\n    return a + b;\n}\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_arrow_function(ts_strip_ctx_t* ctx) {
    test_start("pure JS: arrow functions");
    
    const char* input = "const fn = (x) => x * 2;\nconst fn2 = x => x + 1;\n";
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_control_flow(ts_strip_ctx_t* ctx) {
    test_start("pure JS: control flow");
    
    const char* input = 
        "if (x > 0) {\n"
        "    console.log('positive');\n"
        "} else {\n"
        "    console.log('negative');\n"
        "}\n"
        "for (let i = 0; i < 10; i++) {\n"
        "    sum += i;\n"
        "}\n";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_objects_arrays(ts_strip_ctx_t* ctx) {
    test_start("pure JS: objects and arrays");
    
    const char* input = 
        "const obj = { a: 1, b: 2 };\n"
        "const arr = [1, 2, 3];\n"
        "arr.map(x => x * 2);\n";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_class(ts_strip_ctx_t* ctx) {
    test_start("pure JS: class declaration");
    
    const char* input = 
        "class MyClass {\n"
        "    constructor(value) {\n"
        "        this.value = value;\n"
        "    }\n"
        "    getValue() {\n"
        "        return this.value;\n"
        "    }\n"
        "}\n";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_strings_and_templates(ts_strip_ctx_t* ctx) {
    test_start("pure JS: strings and templates");
    
    const char* input = 
        "const str1 = 'hello';\n"
        "const str2 = \"world\";\n"
        "const template = `value: ${x}`;\n";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

void test_pure_js_comments(ts_strip_ctx_t* ctx) {
    test_start("pure JS: comments");
    
    const char* input = 
        "// Single line comment\n"
        "let x = 1; // inline comment\n"
        "/* Multi-line\n"
        "   comment */\n"
        "let y = 2;\n";
    
    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);
    
    assert_true(success, "Should successfully handle pure JS");
    assert_equal_str(output, input, "Pure JS should be unchanged");
    
    free(output);
    test_end();
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    printf("=== TypeScript Type Stripper Test Suite ===\n\n");
    
    // Create reusable context
    ts_strip_ctx_t* ctx = ts_strip_ctx_new();
    if (!ctx) {
        fprintf(stderr, "Failed to create ts_strip context\n");
        return 1;
    }
    
    // Error case tests
    test_errors_on_enums(ctx);
    test_errors_on_parameter_properties(ctx);
    test_errors_on_typescript_module_declarations(ctx);
    test_errors_on_instantiated_namespaces(ctx);
    test_importing_instantiated_namespace(ctx);
    test_errors_on_declared_legacy_modules(ctx);
    test_errors_on_non_instantiated_legacy_modules(ctx);
    test_errors_on_cjs_export_assignment(ctx);
    test_errors_on_cjs_import(ctx);
    test_errors_on_prefix_type_assertion(ctx);
    test_errors_on_prefix_type_assertion_in_arrow(ctx);
    
    // Success case tests
    test_handles_arrow_on_new_line(ctx);
    test_handles_blanking_multibyte_characters(ctx);
    test_handles_default_export(ctx);
    test_allows_ambient_enum(ctx);
    test_allows_declared_namespace(ctx);
    test_allows_declared_module_augmentation(ctx);
    test_allows_declared_global_augmentation(ctx);
    test_tsx_is_preserved(ctx);
    test_handles_variable_definite_assignment(ctx);
    test_parse_generic_arrow_rather_than_left_shift(ctx);
    test_preserves_strict_directive(ctx);
    test_preserves_nested_strict_directive(ctx);
    
    // Additional basic tests
    test_basic_type_stripping(ctx);
    test_interface_removal(ctx);
    test_type_alias_removal(ctx);
    test_as_expression(ctx);
    test_satisfies_expression(ctx);
    test_non_null_assertion(ctx);
    test_inline_type_import_specifier(ctx);
    test_inline_type_export_specifier(ctx);
    test_import_type_named(ctx);
    test_import_type_default(ctx);
    test_export_type_named(ctx);
    test_mixed_import_with_inline_types(ctx);
    test_import_type_with_alias(ctx);

    // Pure JavaScript tests
    test_pure_js_variables(ctx);
    test_pure_js_function(ctx);
    test_pure_js_arrow_function(ctx);
    test_pure_js_control_flow(ctx);
    test_pure_js_objects_arrays(ctx);
    test_pure_js_class(ctx);
    test_pure_js_strings_and_templates(ctx);
    test_pure_js_comments(ctx);
    
    // Clean up context
    ts_strip_ctx_delete(ctx);
    
    print_test_summary();
    
    return g_stats.failed > 0 ? 1 : 0;
}