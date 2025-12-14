#include "ts_strip.h"
#include <tree_sitter/api.h>
#include "ts/tree-sitter-typescript.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

// Uncomment to enable AST debugging
//#define DEBUG_AST

enum visit_result {
    VISIT_BLANKED = 0,
    VISITED_JS = 1,
};

enum range_flag {
    FLAG_BLANK = 0,
    FLAG_REPLACE_WITH_SEMI = 3,
};

typedef struct {
    int* data;
    size_t size;
    size_t capacity;
} range_array_t;

typedef struct {
    const char* source;
    size_t source_len;
    range_array_t ranges;
    bool seen_js;
    bool has_unsupported;
    TSNode parent_statement;
    int in_function_body;
    TSTree* tree;
    ts_strip_allocator_t* allocator;
} parse_ctx_t;

struct ts_strip_ctx {
    TSParser* parser;
    ts_strip_allocator_t allocator;
};

// Forward declarations
static int visit_node(parse_ctx_t* ctx, TSNode node);
static int visit_children(parse_ctx_t* ctx, TSNode n);

// Allocator wrappers
static inline void* ctx_malloc(ts_strip_allocator_t* alloc, size_t size) {
    return alloc->malloc_func(alloc->user_data, size);
}

static inline void* ctx_realloc(ts_strip_allocator_t* alloc, void* ptr, size_t size) {
    return alloc->realloc_func(alloc->user_data, ptr, size);
}

static inline void ctx_free(ts_strip_allocator_t* alloc, void* ptr) {
    alloc->free_func(alloc->user_data, ptr);
}

// Default allocator implementations
static void* default_malloc(void* user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void* default_realloc(void* user_data, void* ptr, size_t size) {
    (void)user_data;
    return realloc(ptr, size);
}

static void default_free(void* user_data, void* ptr) {
    (void)user_data;
    free(ptr);
}

// Tree-sitter allocator bridges
static void* ts_malloc_bridge(size_t size);
static void* ts_calloc_bridge(size_t count, size_t size);
static void* ts_realloc_bridge(void* ptr, size_t size);
static void ts_free_bridge(void* ptr);

static ts_strip_allocator_t* g_current_allocator = NULL;

static void* ts_malloc_bridge(size_t size) {
    if (g_current_allocator) {
        return ctx_malloc(g_current_allocator, size);
    }
    return malloc(size);
}

static void* ts_calloc_bridge(size_t count, size_t size) {
    if (g_current_allocator) {
        size_t total = count * size;
        void* ptr = ctx_malloc(g_current_allocator, total);
        if (ptr) {
            memset(ptr, 0, total);
        }
        return ptr;
    }
    return calloc(count, size);
}

static void* ts_realloc_bridge(void* ptr, size_t size) {
    if (g_current_allocator) {
        return ctx_realloc(g_current_allocator, ptr, size);
    }
    return realloc(ptr, size);
}

static void ts_free_bridge(void* ptr) {
    if (g_current_allocator) {
        ctx_free(g_current_allocator, ptr);
    } else {
        free(ptr);
    }
}

// Range array management
static bool range_array_init(range_array_t* arr, ts_strip_allocator_t* alloc) {
    arr->data = ctx_malloc(alloc, 128 * sizeof(int));
    if (!arr->data) {
        return false;
    }
    arr->size = 0;
    arr->capacity = 128;
    return true;
}

static void range_array_free(range_array_t* arr, ts_strip_allocator_t* alloc) {
    ctx_free(alloc, arr->data);
    arr->data = NULL;
    arr->size = 0;
    arr->capacity = 0;
}

static bool range_array_push(range_array_t* arr, ts_strip_allocator_t* alloc, int flag, int start, int end) {
    if (arr->size + 3 > arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        int* new_data = ctx_realloc(alloc, arr->data, new_cap * sizeof(int));
        if (!new_data) {
            return false;
        }
        arr->data = new_data;
        arr->capacity = new_cap;
    }
    arr->data[arr->size++] = flag;
    arr->data[arr->size++] = start;
    arr->data[arr->size++] = end;
    return true;
}

// Node type checking
static inline bool is_type(TSNode n, const char* t) {
    return !ts_node_is_null(n) && strcmp(ts_node_type(n), t) == 0;
}

static inline TSNode get_child_by_field(TSNode n, const char* f) {
    return ts_node_child_by_field_name(n, f, strlen(f));
}

static TSNode find_child_type(TSNode n, const char* t) {
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(n, i);
        if (is_type(child, t)) {
            return child;
        }
    }
    return (TSNode){0};
}

// Blanking operations
static inline bool blank_range(parse_ctx_t* ctx, uint32_t start, uint32_t end) {
    return range_array_push(&ctx->ranges, ctx->allocator, FLAG_BLANK, start, end);
}

static inline bool blank_semi(parse_ctx_t* ctx, uint32_t start, uint32_t end) {
    return range_array_push(&ctx->ranges, ctx->allocator, FLAG_REPLACE_WITH_SEMI, start, end);
}

static inline bool blank_node(parse_ctx_t* ctx, TSNode n) {
    return blank_range(ctx, ts_node_start_byte(n), ts_node_end_byte(n));
}

static bool blank_stmt(parse_ctx_t* ctx, TSNode n) {
    uint32_t start = ts_node_start_byte(n);
    uint32_t end = ts_node_end_byte(n);
    
    if (ctx->seen_js && ctx->in_function_body == 0) {
        return blank_semi(ctx, start, end);
    }
    return blank_range(ctx, start, end);
}

static bool blank_type_anno(parse_ctx_t* ctx, TSNode n) {
    uint32_t start = ts_node_start_byte(n);
    uint32_t end = ts_node_end_byte(n);
    
    if (start > 0 && ctx->source[start - 1] == ':') {
        start--;
    }
    return blank_range(ctx, start, end);
}

#ifdef DEBUG_AST
static void print_ast(parse_ctx_t* ctx, TSNode n, int depth) {
    if (ts_node_is_null(n)) {
        return;
    }
    
    for (int i = 0; i < depth; i++) {
        printf("  ");
    }
    
    uint32_t start = ts_node_start_byte(n);
    uint32_t end = ts_node_end_byte(n);
    char snippet[50] = {0};
    int len = end - start;
    
    if (len > 40) {
        len = 40;
    }
    
    memcpy(snippet, ctx->source + start, len);
    for (int i = 0; i < len; i++) {
        if (snippet[i] == '\n') {
            snippet[i] = ' ';
        }
    }
    
    printf("%s [%u:%u] \"%s\"\n", ts_node_type(n), start, end, snippet);
    
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        print_ast(ctx, ts_node_child(n, i), depth + 1);
    }
}
#endif

// Runtime value detection for namespaces
static bool has_runtime_values(parse_ctx_t* ctx, TSNode n) {
    if (ts_node_is_null(n)) {
        return false;
    }
    
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(n, i);
        const char* type = ts_node_type(child);
        
        // Skip ambient declarations - they don't count as runtime
        if (strcmp(type, "ambient_declaration") == 0) {
            continue;
        }
        
        // Runtime statements
        if (strcmp(type, "expression_statement") == 0 ||
            strcmp(type, "statement_block") == 0) {
            return true;
        }
        
        // Variable/function/class declarations
        if (strcmp(type, "lexical_declaration") == 0 ||
            strcmp(type, "variable_declaration") == 0 ||
            strcmp(type, "class_declaration") == 0 ||
            strcmp(type, "function_declaration") == 0) {
            return true;
        }
        
        // Nested namespace that's instantiated
        if (strcmp(type, "internal_module") == 0) {
            if (has_runtime_values(ctx, child)) {
                return true;
            }
        }
        
        // Export statement with runtime value
        if (strcmp(type, "export_statement") == 0) {
            TSNode decl = get_child_by_field(child, "declaration");
            if (!ts_node_is_null(decl)) {
                const char* decl_type = ts_node_type(decl);
                if (strcmp(decl_type, "lexical_declaration") == 0 ||
                    strcmp(decl_type, "variable_declaration") == 0 ||
                    strcmp(decl_type, "class_declaration") == 0 ||
                    strcmp(decl_type, "function_declaration") == 0) {
                    return true;
                }
                
                // Nested internal module
                if (strcmp(decl_type, "internal_module") == 0) {
                    if (has_runtime_values(ctx, decl)) {
                        return true;
                    }
                }
            }
            
            // export import alias
            if (!ts_node_is_null(find_child_type(child, "import_alias"))) {
                return true;
            }
        }
    }
    return false;
}

// Parameter property detection
static bool has_param_props(TSNode params) {
    uint32_t count = ts_node_child_count(params);
    for (uint32_t i = 0; i < count; i++) {
        TSNode param = ts_node_child(params, i);
        uint32_t param_count = ts_node_child_count(param);
        
        for (uint32_t j = 0; j < param_count; j++) {
            TSNode child = ts_node_child(param, j);
            const char* type = ts_node_type(child);
            
            if (strcmp(type, "accessibility_modifier") == 0 ||
                strcmp(type, "public") == 0 ||
                strcmp(type, "private") == 0 ||
                strcmp(type, "protected") == 0 ||
                strcmp(type, "readonly") == 0) {
                return true;
            }
        }
    }
    return false;
}

// Visit parameter list and blank type annotations
static void visit_formal_parameters(parse_ctx_t* ctx, TSNode params) {
    uint32_t count = ts_node_child_count(params);
    for (uint32_t i = 0; i < count; i++) {
        TSNode param = ts_node_child(params, i);
        uint32_t param_count = ts_node_child_count(param);
        
        for (uint32_t j = 0; j < param_count; j++) {
            TSNode child = ts_node_child(param, j);
            const char* type = ts_node_type(child);
            
            if (strcmp(type, "type_annotation") == 0 || strcmp(type, "?") == 0) {
                blank_node(ctx, child);
            } else {
                visit_node(ctx, child);
            }
        }
    }
}

// Handle function-like nodes
static int visit_function_like(parse_ctx_t* ctx, TSNode n) {
    TSNode body = get_child_by_field(n, "body");
    if (ts_node_is_null(body)) {
        // Function signature/overload
        blank_stmt(ctx, n);
        return VISIT_BLANKED;
    }
    
    // Check for parameter properties
    TSNode params = get_child_by_field(n, "parameters");
    if (!ts_node_is_null(params) && has_param_props(params)) {
        ctx->has_unsupported = true;
        return VISITED_JS;
    }
    
    // Enter function body context
    int prev_depth = ctx->in_function_body;
    ctx->in_function_body++;
    
    // Visit children, blanking types
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(n, i);
        const char* type = ts_node_type(child);
        
        if (strcmp(type, "type_annotation") == 0 ||
            strcmp(type, "type_parameters") == 0) {
            blank_node(ctx, child);
        } else if (strcmp(type, "formal_parameters") == 0) {
            visit_formal_parameters(ctx, child);
        } else {
            visit_node(ctx, child);
        }
    }
    
    ctx->in_function_body = prev_depth;
    return VISITED_JS;
}

// Handle arrow functions
static int visit_arrow_function(parse_ctx_t* ctx, TSNode n) {
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(n, i);
        const char* type = ts_node_type(child);
        
        if (strcmp(type, "type_annotation") == 0 ||
            strcmp(type, "type_parameters") == 0) {
            blank_node(ctx, child);
        } else if (strcmp(type, "formal_parameters") == 0) {
            visit_formal_parameters(ctx, child);
        } else {
            visit_node(ctx, child);
        }
    }
    return VISITED_JS;
}

// Handle class declarations
static int visit_class_declaration(parse_ctx_t* ctx, TSNode n) {
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(n, i);
        const char* type = ts_node_type(child);
        
        if (strcmp(type, "type_parameters") == 0) {
            blank_node(ctx, child);
        } else if (strcmp(type, "class_heritage") == 0) {
            // Blank implements, keep extends
            uint32_t heritage_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < heritage_count; j++) {
                TSNode heritage = ts_node_child(child, j);
                if (is_type(heritage, "implements_clause")) {
                    blank_node(ctx, heritage);
                } else {
                    visit_node(ctx, heritage);
                }
            }
        } else {
            visit_node(ctx, child);
        }
    }
    return VISITED_JS;
}

// Handle binary expressions (check for << ambiguity with generics)
static int visit_binary_expression(parse_ctx_t* ctx, TSNode n) {
    TSNode left = get_child_by_field(n, "left");
    TSNode op = get_child_by_field(n, "operator");
    TSNode right = get_child_by_field(n, "right");
    
    // Check if this looks like (foo<<T...) > (...) which should be foo<generic>(args)
    if (!ts_node_is_null(op) && is_type(op, ">") && 
        !ts_node_is_null(left) && is_type(left, "binary_expression")) {
        
        TSNode inner_op = get_child_by_field(left, "operator");
        if (!ts_node_is_null(inner_op) && is_type(inner_op, "<<")) {
            TSNode inner_left = get_child_by_field(left, "left");
            if (!ts_node_is_null(inner_left) && is_type(inner_left, "identifier")) {
                visit_node(ctx, inner_left);
                
                // Find where the actual call arguments start
                TSNode actual_call = right;
                
                if (is_type(actual_call, "arrow_function")) {
                    uint32_t count = ts_node_child_count(actual_call);
                    for (uint32_t i = count; i > 0; i--) {
                        TSNode child = ts_node_child(actual_call, i - 1);
                        if (is_type(child, "binary_expression")) {
                            TSNode be_right = get_child_by_field(child, "right");
                            if (!ts_node_is_null(be_right) && is_type(be_right, "parenthesized_expression")) {
                                actual_call = be_right;
                                break;
                            }
                        } else if (is_type(child, "parenthesized_expression")) {
                            actual_call = child;
                            break;
                        }
                    }
                }
                
                // Blank from end of identifier to start of actual call
                uint32_t blank_start = ts_node_end_byte(inner_left);
                uint32_t blank_end = ts_node_start_byte(actual_call);
                blank_range(ctx, blank_start, blank_end);
                
                visit_node(ctx, actual_call);
                return VISITED_JS;
            }
        }
    }
    
    visit_children(ctx, n);
    return VISITED_JS;
}

// Main node visitor
static int visit_node(parse_ctx_t* ctx, TSNode n) {
    if (ts_node_is_null(n)) {
        return VISITED_JS;
    }
    
    const char* type = ts_node_type(n);
    
    // ERROR nodes indicate parse ambiguity
    if (strcmp(type, "ERROR") == 0) {
        ctx->has_unsupported = true;
        return VISITED_JS;
    }
    
    // Ambient declarations
    if (strcmp(type, "ambient_declaration") == 0) {
        uint32_t count = ts_node_child_count(n);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(n, i);
            if (is_type(child, "module")) {
                TSNode name = get_child_by_field(child, "name");
                if (!ts_node_is_null(name) && !is_type(name, "string")) {
                    // declare module Identifier - unsupported
                    ctx->has_unsupported = true;
                    return VISITED_JS;
                }
                break;
            }
        }
        blank_stmt(ctx, n);
        return VISIT_BLANKED;
    }
    
    // Type-only declarations
    if (strcmp(type, "type_alias_declaration") == 0 ||
        strcmp(type, "interface_declaration") == 0) {
        blank_stmt(ctx, n);
        return VISIT_BLANKED;
    }
    
    // Import statement
    if (strcmp(type, "import_statement") == 0) {
        if (!ts_node_is_null(find_child_type(n, "import_require_clause"))) {
            ctx->has_unsupported = true;
            return VISITED_JS;
        }

        // Check for "import type ..." - type keyword is direct child of import_statement
        if (!ts_node_is_null(find_child_type(n, "type"))) {
            blank_stmt(ctx, n);
            return VISIT_BLANKED;
        }
        // Visit children to handle inline type specifiers
        visit_children(ctx, n);
        return VISITED_JS;
    }

    // Import specifier - blank inline type keyword
    if (strcmp(type, "import_specifier") == 0) {
        TSNode type_keyword = find_child_type(n, "type");
        if (!ts_node_is_null(type_keyword)) {
            blank_node(ctx, type_keyword);
        }
        return VISITED_JS;
    }

    // Export specifier - blank inline type keyword
    if (strcmp(type, "export_specifier") == 0) {
        TSNode type_keyword = find_child_type(n, "type");
        if (!ts_node_is_null(type_keyword)) {
            blank_node(ctx, type_keyword);
        }
        return VISITED_JS;
    }

    // Import alias
    if (strcmp(type, "import_alias") == 0) {
        return VISITED_JS;
    }
    
    // Export statement
    if (strcmp(type, "export_statement") == 0) {
        if (!ts_node_is_null(find_child_type(n, "type"))) {
            blank_stmt(ctx, n);
            return VISIT_BLANKED;
        }
        
        TSNode eq = find_child_type(n, "=");
        if (!ts_node_is_null(eq)) {
            ctx->has_unsupported = true;
            return VISITED_JS;
        }
        
        visit_children(ctx, n);
        return VISITED_JS;
    }
    
    // Variable declarator
    if (strcmp(type, "variable_declarator") == 0) {
        uint32_t count = ts_node_child_count(n);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(n, i);
            const char* child_type = ts_node_type(child);
            
            if (strcmp(child_type, "type_annotation") == 0) {
                blank_type_anno(ctx, child);
            } else if (strcmp(child_type, "!") == 0) {
                blank_node(ctx, child);
            } else {
                visit_node(ctx, child);
            }
        }
        return VISITED_JS;
    }
    
    // Lexical/variable declaration
    if (strcmp(type, "lexical_declaration") == 0 ||
        strcmp(type, "variable_declaration") == 0) {
        visit_children(ctx, n);
        return VISITED_JS;
    }
    
    // Function-like declarations
    if (strcmp(type, "function_declaration") == 0 ||
        strcmp(type, "function_signature") == 0 ||
        strcmp(type, "method_definition") == 0 ||
        strcmp(type, "method_signature") == 0) {
        return visit_function_like(ctx, n);
    }
    
    // Arrow function
    if (strcmp(type, "arrow_function") == 0) {
        return visit_arrow_function(ctx, n);
    }
    
    // Binary expression
    if (strcmp(type, "binary_expression") == 0) {
        return visit_binary_expression(ctx, n);
    }
    
    // Class declaration
    if (strcmp(type, "class_declaration") == 0 ||
        strcmp(type, "abstract_class_declaration") == 0) {
        return visit_class_declaration(ctx, n);
    }
    
    // Public field definition
    if (strcmp(type, "public_field_definition") == 0 ||
        strcmp(type, "property_signature") == 0) {
        uint32_t count = ts_node_child_count(n);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(n, i);
            const char* child_type = ts_node_type(child);
            
            if (strcmp(child_type, "type_annotation") == 0 ||
                strcmp(child_type, "!") == 0 ||
                strcmp(child_type, "?") == 0 ||
                strcmp(child_type, "accessibility_modifier") == 0 ||
                strcmp(child_type, "readonly") == 0) {
                blank_node(ctx, child);
            } else {
                visit_node(ctx, child);
            }
        }
        return VISITED_JS;
    }
    
    // Enum declaration
    if (strcmp(type, "enum_declaration") == 0) {
        ctx->has_unsupported = true;
        return VISITED_JS;
    }
    
    // Module declarations (legacy syntax)
    if (strcmp(type, "module") == 0) {
        ctx->has_unsupported = true;
        return VISITED_JS;
    }
    
    // Namespace (internal_module)
    if (strcmp(type, "internal_module") == 0) {
        if (has_runtime_values(ctx, n)) {
            ctx->has_unsupported = true;
            return VISITED_JS;
        }
        blank_stmt(ctx, n);
        return VISIT_BLANKED;
    }
    
    // As expression / satisfies
    if (strcmp(type, "as_expression") == 0 ||
        strcmp(type, "satisfies_expression") == 0) {
        TSNode expr = ts_node_child(n, 0);
        if (!ts_node_is_null(expr)) {
            visit_node(ctx, expr);
            uint32_t expr_end = ts_node_end_byte(expr);
            uint32_t node_end = ts_node_end_byte(n);
            
            // Check for ASI
            if (!ts_node_is_null(ctx->parent_statement) &&
                node_end == ts_node_end_byte(ctx->parent_statement) &&
                node_end < ctx->source_len && ctx->source[node_end] != ';') {
                blank_semi(ctx, expr_end, node_end);
            } else {
                blank_range(ctx, expr_end, node_end);
            }
        }
        return VISITED_JS;
    }
    
    // Type assertion
    if (strcmp(type, "type_assertion") == 0) {
        ctx->has_unsupported = true;
        return VISITED_JS;
    }
    
    // Non-null expression
    if (strcmp(type, "non_null_expression") == 0) {
        TSNode expr = ts_node_child(n, 0);
        if (!ts_node_is_null(expr)) {
            visit_node(ctx, expr);
        }
        uint32_t node_end = ts_node_end_byte(n);
        blank_range(ctx, node_end - 1, node_end);
        return VISITED_JS;
    }
    
    // Call/new expression
    if (strcmp(type, "call_expression") == 0 ||
        strcmp(type, "new_expression") == 0) {
        uint32_t count = ts_node_child_count(n);
        for (uint32_t i = 0; i < count; i++) {
            TSNode child = ts_node_child(n, i);
            if (is_type(child, "type_arguments")) {
                blank_node(ctx, child);
            } else {
                visit_node(ctx, child);
            }
        }
        return VISITED_JS;
    }
    
    // Type-only nodes
    if (strcmp(type, "type_annotation") == 0 ||
        strcmp(type, "type_arguments") == 0 ||
        strcmp(type, "type_parameters") == 0) {
        blank_node(ctx, n);
        return VISIT_BLANKED;
    }
    
    // Default: visit children
    visit_children(ctx, n);
    return VISITED_JS;
}

static int visit_children(parse_ctx_t* ctx, TSNode n) {
    uint32_t count = ts_node_child_count(n);
    for (uint32_t i = 0; i < count; i++) {
        if (visit_node(ctx, ts_node_child(n, i)) == VISITED_JS) {
            ctx->seen_js = true;
        }
    }
    return ctx->seen_js ? VISITED_JS : VISIT_BLANKED;
}

// Output building
static inline char get_space_char(char c) {
    return (c == '\n' || c == '\r') ? c : ' ';
}

static char* build_output(parse_ctx_t* ctx, size_t* out_len) {
    const range_array_t* ranges = &ctx->ranges;
    const char* in = ctx->source;
    size_t in_len = ctx->source_len;
    
    if (ranges->size == 0) {
        char* out = ctx_malloc(ctx->allocator, in_len + 1);
        if (!out) {
            return NULL;
        }
        memcpy(out, in, in_len);
        out[in_len] = '\0';
        *out_len = in_len;
        return out;
    }
    
    char* out = ctx_malloc(ctx->allocator, in_len + 1);
    if (!out) {
        return NULL;
    }
    
    size_t pos = 0;
    size_t prev = 0;
    
    for (size_t i = 0; i < ranges->size; i += 3) {
        int flag = ranges->data[i];
        size_t start = (size_t)ranges->data[i + 1];
        size_t end = (size_t)ranges->data[i + 2];
        
        if (start < prev) {
            start = prev;
        }
        
        // Copy unchanged
        memcpy(out + pos, in + prev, start - prev);
        pos += start - prev;
        
        // Handle flags
        if (flag == FLAG_REPLACE_WITH_SEMI) {
            out[pos++] = ';';
            start++;
        }
        
        // Blank with spaces/newlines
        for (size_t j = start; j < end && j < in_len; j++) {
            out[pos++] = get_space_char(in[j]);
        }
        
        prev = end;
    }
    
    // Copy remaining
    memcpy(out + pos, in + prev, in_len - prev);
    pos += in_len - prev;
    out[pos] = '\0';
    
    *out_len = pos;
    return out;
}

// Public API
ts_strip_ctx_t* ts_strip_ctx_new(void) {
    ts_strip_allocator_t default_alloc = {
        .malloc_func = default_malloc,
        .realloc_func = default_realloc,
        .free_func = default_free,
        .user_data = NULL
    };
    return ts_strip_ctx_new_with_allocator(&default_alloc);
}

ts_strip_ctx_t* ts_strip_ctx_new_with_allocator(const ts_strip_allocator_t* allocator) {
    if (!allocator) {
        return NULL;
    }
    
    ts_strip_ctx_t* ctx = allocator->malloc_func(allocator->user_data, sizeof(ts_strip_ctx_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->allocator = *allocator;
    
    // Set tree-sitter to use our allocator
    g_current_allocator = &ctx->allocator;
    ts_set_allocator(ts_malloc_bridge, ts_calloc_bridge, ts_realloc_bridge, ts_free_bridge);
    
    ctx->parser = ts_parser_new();
    if (!ctx->parser) {
        ctx_free(&ctx->allocator, ctx);
        g_current_allocator = NULL;
        return NULL;
    }
    
    if (!ts_parser_set_language(ctx->parser, tree_sitter_tsx())) {
        ts_parser_delete(ctx->parser);
        ctx_free(&ctx->allocator, ctx);
        g_current_allocator = NULL;
        return NULL;
    }
    
    g_current_allocator = NULL;
    
    return ctx;
}

void ts_strip_ctx_delete(ts_strip_ctx_t* ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->parser) {
        g_current_allocator = &ctx->allocator;
        ts_parser_delete(ctx->parser);
        g_current_allocator = NULL;
    }
    
    ts_strip_allocator_t alloc = ctx->allocator;
    ctx_free(&alloc, ctx);
}

ts_strip_result_t ts_strip_with_ctx(
    ts_strip_ctx_t* ctx,
    const char* src,
    char** js_out,
    size_t* js_len
) {
    if (!ctx || !src || !js_out || !js_len) {
        return TS_STRIP_ERROR_INVALID_INPUT;
    }
    
    *js_out = NULL;
    *js_len = 0;
    
    size_t len = strlen(src);
    if (len == 0) {
        *js_out = ctx_malloc(&ctx->allocator, 1);
        if (!*js_out) {
            return TS_STRIP_ERROR_OUT_OF_MEMORY;
        }
        (*js_out)[0] = '\0';
        return TS_STRIP_SUCCESS;
    }
    
    // Reset parser for reuse
    ts_parser_reset(ctx->parser);
    
    // Set tree-sitter to use our allocator
    g_current_allocator = &ctx->allocator;
    
    TSTree* tree = ts_parser_parse_string(ctx->parser, NULL, src, (uint32_t)len);
    if (!tree) {
        g_current_allocator = NULL;
        return TS_STRIP_ERROR_PARSE_FAILED;
    }
    
    parse_ctx_t parse_ctx = {
        .source = src,
        .source_len = len,
        .ranges = {0},
        .seen_js = false,
        .has_unsupported = false,
        .parent_statement = {0},
        .in_function_body = 0,
        .tree = tree,
        .allocator = &ctx->allocator
    };
    
    if (!range_array_init(&parse_ctx.ranges, &ctx->allocator)) {
        ts_tree_delete(tree);
        g_current_allocator = NULL;
        return TS_STRIP_ERROR_OUT_OF_MEMORY;
    }
    
    TSNode root = ts_tree_root_node(tree);
    
#ifdef DEBUG_AST
    printf("\n=== AST ===\n");
    print_ast(&parse_ctx, root, 0);
    printf("==========\n\n");
#endif
    
    uint32_t count = ts_node_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        TSNode child = ts_node_child(root, i);
        parse_ctx.parent_statement = child;
        if (visit_node(&parse_ctx, child) == VISITED_JS) {
            parse_ctx.seen_js = true;
        }
    }
    
    char* out = build_output(&parse_ctx, js_len);
    
    range_array_free(&parse_ctx.ranges, &ctx->allocator);
    ts_tree_delete(tree);
    g_current_allocator = NULL;
    
    if (!out) {
        return TS_STRIP_ERROR_OUT_OF_MEMORY;
    }
    
    *js_out = out;
    return parse_ctx.has_unsupported ? TS_STRIP_ERROR_UNSUPPORTED : TS_STRIP_SUCCESS;
}

ts_strip_result_t ts_strip(const char* src, char** js_out, size_t* js_len) {
    ts_strip_ctx_t* ctx = ts_strip_ctx_new();
    if (!ctx) {
        return TS_STRIP_ERROR_OUT_OF_MEMORY;
    }
    
    ts_strip_result_t result = ts_strip_with_ctx(ctx, src, js_out, js_len);
    
    ts_strip_ctx_delete(ctx);
    
    return result;
}

const char* ts_strip_error_message(ts_strip_result_t result) {
    switch (result) {
        case TS_STRIP_SUCCESS:
            return "Success";
        case TS_STRIP_ERROR_INVALID_INPUT:
            return "Invalid input parameters";
        case TS_STRIP_ERROR_PARSE_FAILED:
            return "Failed to parse TypeScript source";
        case TS_STRIP_ERROR_UNSUPPORTED:
            return "Source contains unsupported/non-erasable syntax";
        case TS_STRIP_ERROR_OUT_OF_MEMORY:
            return "Out of memory";
        default:
            return "Unknown error";
    }
}