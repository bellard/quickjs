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

    // Note: namespace D { declare let x; } is NOT included because it only contains
    // ambient declarations (type-only) and should be blanked, not marked unsupported
    const char* input =
        "\n"
        "        namespace A { 1; }\n"
        "        namespace B { globalThis; }\n"
        "        namespace C { export let x; }\n"
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

    // ts-blank-space behavior: entire type specifier is blanked, including comma
    const char* input =
        "import {\n"
        "    defineGenerator,\n"
        "    type GeneratorArtifact,\n"
        "    type GeneratorContext,\n"
        "} from \"@bebop/sdk/ge\";\n";

    const char* expected =
        "import {\n"
        "    defineGenerator,\n"
        "                           \n"
        "                          \n"
        "} from \"@bebop/sdk/ge\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle inline type import specifiers");
    assert_equal_str(output, expected, "Type import specifiers should be blanked entirely");

    free(output);
    test_end();
}

void test_inline_type_export_specifier(ts_strip_ctx_t* ctx) {
    test_start("inline type export specifier removal");

    // ts-blank-space behavior: entire type specifier is blanked, including comma
    const char* input = "export { type Foo, bar, type Baz } from \"./module\";\n";
    const char* expected = "export {           bar,          } from \"./module\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle inline type export specifiers");
    assert_equal_str(output, expected, "Type export specifiers should be blanked entirely");

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

    // ts-blank-space behavior: entire type specifier is blanked, including comma
    const char* input = "import { createCatName, type Cat, type Dog } from \"./animal\";\n";
    const char* expected = "import { createCatName,                    } from \"./animal\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle mixed import with inline types");
    assert_equal_str(output, expected, "Type import specifiers should be blanked entirely");

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

void test_override_keyword(ts_strip_ctx_t* ctx) {
    test_start("override keyword on class methods");

    const char* input =
        "class D extends C {\n"
        "    override method(...args): any {}\n"
        "}\n";

    const char* expected =
        "class D extends C {\n"
        "             method(...args)      {}\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle override keyword");
    assert_equal_str(output, expected, "Override keyword should be blanked");

    free(output);
    test_end();
}

void test_declare_class_field(ts_strip_ctx_t* ctx) {
    test_start("declare on class fields");

    const char* input =
        "class C {\n"
        "    declare f3: any;\n"
        "    b = 1;\n"
        "}\n";

    const char* expected =
        "class C {\n"
        "    ;               \n"
        "    b = 1;\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle declare class field");
    assert_equal_str(output, expected, "Declared field should be replaced with semicolon");

    free(output);
    test_end();
}

void test_abstract_class(ts_strip_ctx_t* ctx) {
    test_start("abstract class declaration");

    const char* input =
        "abstract class A {\n"
        "    abstract a;\n"
        "    b;\n"
        "    abstract method();\n"
        "}\n";

    const char* expected =
        "         class A {\n"
        "    ;          \n"
        "    b;\n"
        "                      \n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle abstract class");
    assert_equal_str(output, expected, "Abstract keyword and members should be blanked");

    free(output);
    test_end();
}

void test_this_parameter(ts_strip_ctx_t* ctx) {
    test_start("this parameter in functions");

    const char* input = "(function f0(this: any) {});\n";
    const char* expected = "(function f0(         ) {});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle this parameter");
    assert_equal_str(output, expected, "This parameter should be blanked entirely");

    free(output);
    test_end();
}

void test_this_parameter_with_other_params(ts_strip_ctx_t* ctx) {
    test_start("this parameter with other parameters");

    // Note: The space between comma and arg1 is preserved (not part of any node)
    const char* input = "(function f1(this: any, arg1: any) {});\n";
    const char* expected = "(function f1(           arg1     ) {});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle this parameter with other params");
    assert_equal_str(output, expected, "This parameter and trailing comma should be blanked");

    free(output);
    test_end();
}

void test_index_signature(ts_strip_ctx_t* ctx) {
    test_start("index signature in class");

    // Input: "    [key: string]: any;\n" = 24 chars
    // Output should preserve length: 24 spaces
    const char* input =
        "class C {\n"
        "    [key: string]: any;\n"
        "    b = 1;\n"
        "}\n";

    const char* expected =
        "class C {\n"
        "                       \n"
        "    b = 1;\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle index signature");
    assert_equal_str(output, expected, "Index signature should be blanked entirely");

    free(output);
    test_end();
}

void test_accessibility_modifier_on_method(ts_strip_ctx_t* ctx) {
    test_start("accessibility modifier on method");

    const char* input =
        "class C {\n"
        "    private method() {}\n"
        "}\n";

    const char* expected =
        "class C {\n"
        "            method() {}\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle accessibility modifier on method");
    assert_equal_str(output, expected, "Private keyword should be blanked");

    free(output);
    test_end();
}

void test_optional_method(ts_strip_ctx_t* ctx) {
    test_start("optional method");

    const char* input = "(class { optionalMethod?(v: any) {} });\n";
    const char* expected = "(class { optionalMethod (v     ) {} });\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should successfully handle optional method");
    assert_equal_str(output, expected, "Question mark and type annotation should be blanked");

    free(output);
    test_end();
}

void test_asi_type_before_call(ts_strip_ctx_t* ctx) {
    test_start("ASI: type declaration before call expression");

    const char* input = "foo\ntype x = 1;\n(1);\n";
    const char* expected = "foo\n;          \n(1);\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI before call");
    assert_equal_str(output, expected, "Should insert semicolon for ASI safety");

    free(output);
    test_end();
}

void test_asi_type_before_template(ts_strip_ctx_t* ctx) {
    test_start("ASI: type declaration before template literal");

    const char* input = "foo\ntype y = 1;\n``;\n";
    const char* expected = "foo\n;          \n``;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI before template");
    assert_equal_str(output, expected, "Should insert semicolon for ASI safety");

    free(output);
    test_end();
}

void test_asi_interface_before_call(ts_strip_ctx_t* ctx) {
    test_start("ASI: interface before call expression");

    const char* input = "foo\ninterface I {}\n(1);\n";
    const char* expected = "foo\n;             \n(1);\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI with interface");
    assert_equal_str(output, expected, "Should insert semicolon for ASI safety");

    free(output);
    test_end();
}

void test_asi_as_expression_before_call(ts_strip_ctx_t* ctx) {
    test_start("ASI: as expression before call");

    // Input: "foo as string\n(1);\n" = 19 bytes
    // " as string" = 10 chars, replace with 9 spaces + semicolon
    const char* input = "foo as string\n(1);\n";
    const char* expected = "foo         ;\n(1);\n";  // 9 spaces before ;

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI after as expression");
    assert_equal_str(output, expected, "Should insert semicolon for ASI safety");

    free(output);
    test_end();
}

void test_asi_satisfies_before_call(ts_strip_ctx_t* ctx) {
    test_start("ASI: satisfies expression before call");

    // Input: "foo satisfies string\n(1);\n" = 26 bytes
    // " satisfies string" = 17 chars, replace with 16 spaces + semicolon
    const char* input = "foo satisfies string\n(1);\n";
    const char* expected = "foo                ;\n(1);\n";  // 16 spaces before ;

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI after satisfies");
    assert_equal_str(output, expected, "Should insert semicolon for ASI safety");

    free(output);
    test_end();
}

void test_no_asi_before_plus(ts_strip_ctx_t* ctx) {
    test_start("No ASI: satisfies before + operator");

    // No ASI needed before + since it's a binary operator continuation
    const char* input = "foo satisfies string\n+ \"\";\n";
    const char* expected = "foo                 \n+ \"\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should not insert ASI before binary op");
    assert_equal_str(output, expected, "Should not insert semicolon before +");

    free(output);
    test_end();
}

void test_async_generic_arrow(ts_strip_ctx_t* ctx) {
    test_start("async generic arrow function");

    const char* input = "const a = async<T>(v: T) => {};\n";
    const char* expected = "const a = async   (v   ) => {};\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle async generic arrow");
    assert_equal_str(output, expected, "Generic and type annotation should be blanked");

    free(output);
    test_end();
}

void test_multiline_generic_arrow(ts_strip_ctx_t* ctx) {
    test_start("multi-line generic arrow function");

    const char* input = "const b = async <\n    T\n>(v: T) => {};\n";
    const char* expected = "const b = async  \n     \n (v   ) => {};\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle multi-line generic arrow");
    assert_equal_str(output, expected, "Multi-line generic should be blanked correctly");
    // Line count should be preserved
    int input_lines = count_newlines(input);
    int output_lines = count_newlines(output);
    assert_equal_size(output_lines, input_lines, "Line count should match");

    free(output);
    test_end();
}

void test_arrow_with_multiline_return_type(ts_strip_ctx_t* ctx) {
    test_start("arrow with multi-line return type");

    const char* input = "const c = async <\n    T\n>(v: T): Promise<\n    T\n> => v;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle multi-line return type");
    int input_lines = count_newlines(input);
    int output_lines = count_newlines(output);
    assert_equal_size(output_lines, input_lines, "Line count should match");
    assert_contains(output, "=> v", "Arrow and return value should be preserved");

    free(output);
    test_end();
}

void test_destructuring_with_type_annotation(ts_strip_ctx_t* ctx) {
    test_start("destructuring with type annotation");

    const char* input = "const { a, b }: { a: number; b: string } = obj;\n";
    const char* expected = "const { a, b }                           = obj;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle destructuring with type");
    assert_equal_str(output, expected, "Type annotation should be blanked");

    free(output);
    test_end();
}

void test_destructuring_with_default_and_as(ts_strip_ctx_t* ctx) {
    test_start("destructuring with default value and as expression");

    const char* input = "const { x = {} as any } = obj;\n";
    const char* expected = "const { x = {}        } = obj;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle destructuring with as");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_function_param_destructuring_with_type(ts_strip_ctx_t* ctx) {
    test_start("function parameter destructuring with type");

    const char* input = "(function({ name, value }: { name: string; value: number }) {});\n";
    // 34 spaces to match ": { name: string; value: number }"
    const char* expected = "(function({ name, value }                                 ) {});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle param destructuring with type");
    assert_equal_str(output, expected, "Type annotation should be blanked");

    free(output);
    test_end();
}

void test_decorator_with_as_expression(ts_strip_ctx_t* ctx) {
    test_start("decorator with as expression");

    const char* input = "@(Object.freeze as any)\nclass A {}\n";
    const char* expected = "@(Object.freeze       )\nclass A {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle decorator with as");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_decorator_with_generic(ts_strip_ctx_t* ctx) {
    test_start("decorator with generic argument");

    const char* input = "@Object.freeze<any>\nclass B {}\n";
    const char* expected = "@Object.freeze     \nclass B {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle decorator with generic");
    assert_equal_str(output, expected, "Generic argument should be blanked");

    free(output);
    test_end();
}

void test_decorated_declared_class(ts_strip_ctx_t* ctx) {
    test_start("decorated declare class");

    const char* input = "@(Object.freeze<any>) declare class D {}\n";
    const char* expected = "@(Object.freeze     )                   \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle decorated declare class");
    assert_equal_str(output, expected, "Declare class should be blanked");

    free(output);
    test_end();
}

void test_decorator_on_field(ts_strip_ctx_t* ctx) {
    test_start("decorator on class field");

    const char* input = "class E {\n    @Object.freeze<any>\n    field;\n}\n";
    const char* expected = "class E {\n    @Object.freeze     \n    field;\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle decorator on field");
    assert_equal_str(output, expected, "Generic argument should be blanked");

    free(output);
    test_end();
}

void test_empty_namespace_erasure(ts_strip_ctx_t* ctx) {
    test_start("empty namespace erasure");

    // Empty namespace is blanked entirely (no semicolon needed since it ends with })
    const char* input = "namespace Empty {}\n";
    const char* expected = "                  \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle empty namespace");
    assert_equal_str(output, expected, "Empty namespace should be blanked");

    free(output);
    test_end();
}

void test_type_only_namespace_erasure(ts_strip_ctx_t* ctx) {
    test_start("type-only namespace erasure");

    const char* input = "namespace TypeOnly {\n    type A = string;\n    export type B = A;\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle type-only namespace");
    // The namespace should be completely blanked
    int input_lines = count_newlines(input);
    int output_lines = count_newlines(output);
    assert_equal_size(output_lines, input_lines, "Line count should match");

    free(output);
    test_end();
}

void test_generic_call_expression(ts_strip_ctx_t* ctx) {
    test_start("generic call expression");

    const char* input = "const x = foo<string>();\n";
    const char* expected = "const x = foo        ();\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle generic call");
    assert_equal_str(output, expected, "Generic argument should be blanked");

    free(output);
    test_end();
}

void test_generic_method_chain(ts_strip_ctx_t* ctx) {
    test_start("generic method chain");

    const char* input = "arr.map<number>(x => x).filter<number>(x => x > 0);\n";
    const char* expected = "arr.map        (x => x).filter        (x => x > 0);\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle generic method chain");
    assert_equal_str(output, expected, "Generic arguments should be blanked");

    free(output);
    test_end();
}

void test_new_expression_with_generic(ts_strip_ctx_t* ctx) {
    test_start("new expression with generic");

    const char* input = "let m = new Map<string, number>();\n";
    const char* expected = "let m = new Map                ();\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle new with generic");
    assert_equal_str(output, expected, "Generic arguments should be blanked");

    free(output);
    test_end();
}

void test_computed_property_with_as(ts_strip_ctx_t* ctx) {
    test_start("computed property with as expression");

    const char* input = "class A {\n    [(\"A\" + \"B\") as \"AB\"] = 1;\n}\n";
    const char* expected = "class A {\n    [(\"A\" + \"B\")        ] = 1;\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle computed property with as");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_object_computed_property_with_as(ts_strip_ctx_t* ctx) {
    test_start("object literal computed property with as");

    const char* input = "const obj = {\n    [(\"A\" + \"B\") as \"AB\"]: null\n};\n";
    const char* expected = "const obj = {\n    [(\"A\" + \"B\")        ]: null\n};\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle object computed property");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_parenthesized_return_type(ts_strip_ctx_t* ctx) {
    test_start("parenthesized return type");

    const char* input = "var t = (): (void) => { }\n";
    const char* expected = "var t = ()         => { }\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle parenthesized return type");
    assert_equal_str(output, expected, "Return type should be blanked");

    free(output);
    test_end();
}

void test_union_return_type(ts_strip_ctx_t* ctx) {
    test_start("union return type");

    const char* input = "var t1 = (): (void | string) => { }\n";
    const char* expected = "var t1 = ()                  => { }\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle union return type");
    assert_equal_str(output, expected, "Return type should be blanked");

    free(output);
    test_end();
}

void test_function_parenthesized_return_type(ts_strip_ctx_t* ctx) {
    test_start("function with parenthesized return type");

    const char* input = "function f(): (void) { }\n";
    const char* expected = "function f()         { }\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle function parenthesized return type");
    assert_equal_str(output, expected, "Return type should be blanked");

    free(output);
    test_end();
}

void test_non_null_on_new_expression(ts_strip_ctx_t* ctx) {
    test_start("non-null assertion on new expression");

    const char* input = "let m = new (Map!)<string, number>([]!);\n";
    const char* expected = "let m = new (Map )                ([] );\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle non-null on new");
    assert_equal_str(output, expected, "Non-null and generics should be blanked");

    free(output);
    test_end();
}

void test_function_overload(ts_strip_ctx_t* ctx) {
    test_start("function overload signature");

    const char* input = "function overload(): number;\nfunction overload(): any {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle function overload");
    // First line should be blanked (overload signature)
    // Second line should keep function but blank return type
    int input_lines = count_newlines(input);
    int output_lines = count_newlines(output);
    assert_equal_size(output_lines, input_lines, "Line count should match");

    free(output);
    test_end();
}

void test_getter_setter_types(ts_strip_ctx_t* ctx) {
    test_start("getter and setter with types");

    const char* input = "class C {\n    get g(): any { return 1 };\n    set g(v: any) { };\n}\n";
    const char* expected = "class C {\n    get g()      { return 1 };\n    set g(v     ) { };\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle getter/setter types");
    assert_equal_str(output, expected, "Type annotations should be blanked");

    free(output);
    test_end();
}

void test_class_expression_generic(ts_strip_ctx_t* ctx) {
    test_start("class expression with generic in extends");

    const char* input = "class E extends (function() {} as any) {}\n";
    const char* expected = "class E extends (function() {}       ) {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle class extends with as");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_declare_let(ts_strip_ctx_t* ctx) {
    test_start("declare let statement");

    const char* input = "declare let a;\n";
    const char* expected = ";             \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle declare let");
    assert_equal_str(output, expected, "Declare let should be blanked");

    free(output);
    test_end();
}

void test_declare_class(ts_strip_ctx_t* ctx) {
    test_start("declare class statement");

    const char* input = "declare class DeclaredClass {}\n";
    const char* expected = "                              \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle declare class");
    assert_equal_str(output, expected, "Declare class should be blanked");

    free(output);
    test_end();
}

void test_declare_function(ts_strip_ctx_t* ctx) {
    test_start("declare function statement");

    const char* input = "declare function DeclaredFunction(): void;\n";
    const char* expected = "                                          \n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle declare function");
    assert_equal_str(output, expected, "Declare function should be blanked");

    free(output);
    test_end();
}

void test_export_type_star(ts_strip_ctx_t* ctx) {
    test_start("export type * from");

    // Input: "export type * from \"node:buffer\";\n" = 34 bytes
    const char* input = "export type * from \"node:buffer\";\n";
    const char* expected = "                                 \n";  // 33 spaces + newline = 34 bytes

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle export type *");
    assert_equal_str(output, expected, "Export type * should be blanked");

    free(output);
    test_end();
}

void test_tagged_template_with_generic(ts_strip_ctx_t* ctx) {
    test_start("tagged template with generic");

    // From b.ts: (<T>(...args: any[]) => {})<any>`tagged ${"template" as any}`;
    const char* input = "(<T>(...args: any[]) => {})<any>`tagged ${\"template\" as any}`;\n";
    // <T> = 3, : any[] = 7, <any> = 5, as any = 6
    const char* expected = "(   (...args       ) => {})     `tagged ${\"template\"       }`;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle tagged template with generic");
    assert_equal_str(output, expected, "Generics and as expressions should be blanked");

    free(output);
    test_end();
}

void test_return_generic_arrow(ts_strip_ctx_t* ctx) {
    test_start("return with generic arrow spanning lines");

    // From arrow-functions.ts
    const char* input = "(function () {\n    return<T>\n        (v: T) => v\n});\n";
    // <T> is blanked, ": T" is blanked
    const char* expected = "(function () {\n    return   \n        (v   ) => v\n});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle return with generic arrow");
    assert_equal_str(output, expected, "Generic and type should be blanked");

    free(output);
    test_end();
}

void test_yield_generic_arrow(ts_strip_ctx_t* ctx) {
    test_start("yield with generic arrow");

    // From arrow-functions.ts
    const char* input = "(function* () {\n    yield<T>\n(v: T)=>v;\n});\n";
    const char* expected = "(function* () {\n    yield   \n(v   )=>v;\n});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle yield with generic arrow");
    assert_equal_str(output, expected, "Generic and type should be blanked");

    free(output);
    test_end();
}

void test_throw_generic_arrow(ts_strip_ctx_t* ctx) {
    test_start("throw with generic arrow");

    // From arrow-functions.ts
    const char* input = "(function* () {\n    throw<T>\n(v: T)=>v;\n});\n";
    const char* expected = "(function* () {\n    throw   \n(v   )=>v;\n});\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle throw with generic arrow");
    assert_equal_str(output, expected, "Generic and type should be blanked");

    free(output);
    test_end();
}

void test_asi_satisfies_before_array_access(ts_strip_ctx_t* ctx) {
    test_start("ASI: satisfies before array access");

    // From asi.ts: foo satisfies string\n[0];
    const char* input = "foo satisfies string\n[0];\n";
    const char* expected = "foo                ;\n[0];\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI before array access");
    assert_equal_str(output, expected, "Should insert semicolon before [0]");

    free(output);
    test_end();
}

void test_import_with_emoji_specifier(ts_strip_ctx_t* ctx) {
    test_start("import with emoji specifier");

    // From modules.ts
    const char* input = "import { \"\xF0\x9F\x99\x82\" as C2 } from \"./modules\";\n";
    // No type annotations, should be unchanged
    const char* expected = "import { \"\xF0\x9F\x99\x82\" as C2 } from \"./modules\";\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle import with emoji");
    assert_equal_str(output, expected, "Import with emoji should be unchanged");

    free(output);
    test_end();
}

void test_export_with_emoji_specifier(ts_strip_ctx_t* ctx) {
    test_start("export with emoji specifier");

    // From modules.ts
    const char* input = "export {\n    C,\n    type T,\n    C as \"\xF0\x9F\x99\x82\"\n}\n";
    // type T should be blanked
    const char* expected = "export {\n    C,\n           \n    C as \"\xF0\x9F\x99\x82\"\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle export with emoji");
    assert_equal_str(output, expected, "Type specifier should be blanked");

    free(output);
    test_end();
}

void test_decorator_on_accessor(ts_strip_ctx_t* ctx) {
    test_start("decorator on accessor");

    // From decorators.ts
    const char* input = "class E {\n    @(null as any)\n    accessor x;\n}\n";
    const char* expected = "class E {\n    @(null       )\n    accessor x;\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle decorator on accessor");
    assert_equal_str(output, expected, "As expression should be blanked");

    free(output);
    test_end();
}

void test_class_field_asi_with_public_computed(ts_strip_ctx_t* ctx) {
    test_start("class field ASI with public computed field");

    // From asi.ts
    const char* input =
        "class ASI {\n"
        "    g = 2\n"
        "    public [\"computed-field\"] = 1\n"
        "}\n";

    // 'public' needs to be replaced with ';' + spaces for ASI
    const char* expected =
        "class ASI {\n"
        "    g = 2\n"
        "    ;      [\"computed-field\"] = 1\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI with public computed field");
    assert_equal_str(output, expected, "Public should be replaced with semicolon");

    free(output);
    test_end();
}

void test_class_field_asi_with_public_computed_method(ts_strip_ctx_t* ctx) {
    test_start("class field ASI with public computed method");

    // From asi.ts
    const char* input =
        "class ASI {\n"
        "    h = 3\n"
        "    public [\"computed-method\"]() {}\n"
        "}\n";

    const char* expected =
        "class ASI {\n"
        "    h = 3\n"
        "    ;      [\"computed-method\"]() {}\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle ASI with public computed method");
    assert_equal_str(output, expected, "Public should be replaced with semicolon");

    free(output);
    test_end();
}

void test_class_readonly_computed_field(ts_strip_ctx_t* ctx) {
    test_start("class static readonly computed field");

    // From asi.ts - NoASI case (readonly is not a problem)
    const char* input =
        "class NoASI {\n"
        "    f = 1\n"
        "    static readonly [\"computed-field\"] = 1\n"
        "}\n";

    const char* expected =
        "class NoASI {\n"
        "    f = 1\n"
        "    static          [\"computed-field\"] = 1\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle static readonly computed field");
    assert_equal_str(output, expected, "Readonly should be blanked");

    free(output);
    test_end();
}

void test_generic_in_function_bar(ts_strip_ctx_t* ctx) {
    test_start("generic type instantiation then call");

    // From asi.ts: bar<T>;(1); where bar is called with generic then (1) is separate
    const char* input = "function bar<T>() {\n    bar\n    <T>;\n    (1);\n}\n";
    // The <T>; should be blanked (it's a type instantiation expression)
    const char* expected = "function bar   () {\n    bar\n       ;\n    (1);\n}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle generic followed by call");
    assert_equal_str(output, expected, "Generic should be blanked with semicolon");

    free(output);
    test_end();
}

void test_nested_namespace_path(ts_strip_ctx_t* ctx) {
    test_start("nested namespace with type annotation");

    // From namespaces.ts
    const char* input = "export const x: With.Imports.Foo = 1;\n";
    const char* expected = "export const x                   = 1;\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle nested namespace type");
    assert_equal_str(output, expected, "Type annotation should be blanked");

    free(output);
    test_end();
}

void test_computed_method_with_generic(ts_strip_ctx_t* ctx) {
    test_start("computed method with generics");

    // From b.ts: [foo<string>("")]<T>(a: T)
    const char* input =
        "class A {\n"
        "    [foo<string>(\"\")]<T>(a: T) {}\n"
        "}\n";

    const char* expected =
        "class A {\n"
        "    [foo        (\"\")]   (a   ) {}\n"
        "}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle computed method with generics");
    assert_equal_str(output, expected, "Generic arguments should be blanked");

    free(output);
    test_end();
}

void test_export_decorator_on_class(ts_strip_ctx_t* ctx) {
    test_start("export with decorator on class");

    // From decorators.ts
    const char* input = "@Object.freeze<any>export class B {}\n";
    const char* expected = "@Object.freeze     export class B {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle export with decorator");
    assert_equal_str(output, expected, "Generic should be blanked");

    free(output);
    test_end();
}

void test_export_after_decorator(ts_strip_ctx_t* ctx) {
    test_start("export after decorator on separate line");

    // From decorators.ts
    const char* input = "export\n@Object.freeze<any>\nclass C {}\n";
    const char* expected = "export\n@Object.freeze     \nclass C {}\n";

    char* output;
    size_t output_len;
    bool success = test_strip_success(ctx, input, &output, &output_len);

    assert_true(success, "Should handle export after decorator");
    assert_equal_str(output, expected, "Generic should be blanked");

    free(output);
    test_end();
}

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

    // New ts-blank-space feature tests
    test_override_keyword(ctx);
    test_declare_class_field(ctx);
    test_abstract_class(ctx);
    test_this_parameter(ctx);
    test_this_parameter_with_other_params(ctx);
    test_index_signature(ctx);
    test_accessibility_modifier_on_method(ctx);
    test_optional_method(ctx);

    // ASI tests
    test_asi_type_before_call(ctx);
    test_asi_type_before_template(ctx);
    test_asi_interface_before_call(ctx);
    test_asi_as_expression_before_call(ctx);
    test_asi_satisfies_before_call(ctx);
    test_no_asi_before_plus(ctx);

    // Arrow function edge cases
    test_async_generic_arrow(ctx);
    test_multiline_generic_arrow(ctx);
    test_arrow_with_multiline_return_type(ctx);

    // Destructuring with types
    test_destructuring_with_type_annotation(ctx);
    test_destructuring_with_default_and_as(ctx);
    test_function_param_destructuring_with_type(ctx);

    // Decorator tests
    test_decorator_with_as_expression(ctx);
    test_decorator_with_generic(ctx);
    test_decorated_declared_class(ctx);
    test_decorator_on_field(ctx);

    // Namespace tests
    test_empty_namespace_erasure(ctx);
    test_type_only_namespace_erasure(ctx);

    // Generic call expressions
    test_generic_call_expression(ctx);
    test_generic_method_chain(ctx);
    test_new_expression_with_generic(ctx);

    // Computed property names
    test_computed_property_with_as(ctx);
    test_object_computed_property_with_as(ctx);

    // Parenthesized return types
    test_parenthesized_return_type(ctx);
    test_union_return_type(ctx);
    test_function_parenthesized_return_type(ctx);

    // Additional edge cases
    test_non_null_on_new_expression(ctx);
    test_function_overload(ctx);
    test_getter_setter_types(ctx);
    test_class_expression_generic(ctx);
    test_declare_let(ctx);
    test_declare_class(ctx);
    test_declare_function(ctx);
    test_export_type_star(ctx);

    // Additional ts-blank-space fixture tests
    test_tagged_template_with_generic(ctx);
    test_return_generic_arrow(ctx);
    test_yield_generic_arrow(ctx);
    test_throw_generic_arrow(ctx);
    test_asi_satisfies_before_array_access(ctx);
    test_import_with_emoji_specifier(ctx);
    test_export_with_emoji_specifier(ctx);
    test_decorator_on_accessor(ctx);
    test_class_field_asi_with_public_computed(ctx);
    test_class_field_asi_with_public_computed_method(ctx);
    test_class_readonly_computed_field(ctx);
    test_generic_in_function_bar(ctx);
    test_nested_namespace_path(ctx);
    test_computed_method_with_generic(ctx);
    test_export_decorator_on_class(ctx);
    test_export_after_decorator(ctx);

    // Clean up context
    ts_strip_ctx_delete(ctx);
    
    print_test_summary();
    
    return g_stats.failed > 0 ? 1 : 0;
}