cmake_minimum_required(VERSION 3.16)
project(quickjs C)

# ---------------------------------------------------------------------------
# This CMakeLists.txt exists specifically to make it possible to build
# QuickJS with MSVC (cl.exe / Visual Studio). The project historically only
# ships a GNU Makefile, which assumes a GCC/Clang-compatible compiler.
#
# GCC/Clang users can keep using the plain `make` build exactly as before;
# this file is an *additional*, MSVC-friendly way to build the same sources.
# ---------------------------------------------------------------------------

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" QJS_VERSION LIMIT_COUNT 1)
string(STRIP "${QJS_VERSION}" QJS_VERSION)

set(QUICKJS_LIB_SOURCES
    quickjs.c
    dtoa.c
    libregexp.c
    libunicode.c
    cutils.c
    quickjs-libc.c
)

add_library(quickjs STATIC ${QUICKJS_LIB_SOURCES})
target_include_directories(quickjs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(quickjs PUBLIC CONFIG_VERSION="${QJS_VERSION}")

if(MSVC)
    # /wd4200: nonstandard extension used: zero-sized array (flexible array
    #          members, used intentionally throughout quickjs.c)
    # /wd4244, /wd4267: implicit narrowing conversions - the codebase relies
    #          on these throughout and they are warnings, not bugs, on the
    #          GCC/Clang side either.
    # /wd4996: "deprecated" POSIX names (open/read/write/strcasecmp/...) -
    #          intentionally used via msvc_compat.h aliases.
    target_compile_options(quickjs PUBLIC /wd4200 /wd4244 /wd4267 /wd4996 /utf-8)
    target_compile_definitions(quickjs PUBLIC
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
        _USE_MATH_DEFINES
    )
else()
    target_compile_definitions(quickjs PUBLIC _GNU_SOURCE)
    target_compile_options(quickjs PUBLIC -Wall -fwrapv)
endif()

find_library(LIBM m)
if(LIBM)
    target_link_libraries(quickjs PUBLIC ${LIBM})
endif()

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(quickjs PUBLIC Threads::Threads)
    find_library(LIBDL dl)
    if(LIBDL)
        target_link_libraries(quickjs PUBLIC ${LIBDL})
    endif()
elseif(MSVC)
    # Windows sockets/console helpers used by quickjs-libc.c's os module.
    target_link_libraries(quickjs PUBLIC ws2_32 bcrypt)
endif()

# --------------------------------------------------------------------- qjsc
add_executable(qjsc qjsc.c)
target_link_libraries(qjsc PRIVATE quickjs)

# repl.c is normally generated ahead of time by running the freshly built
# qjsc against repl.js (see the "repl.c: $(QJSC) repl.js" rule in the
# Makefile). Reproduce that bootstrap step here.
set(QJS_REPL_C "${CMAKE_CURRENT_BINARY_DIR}/repl.c")
add_custom_command(
    OUTPUT ${QJS_REPL_C}
    COMMAND qjsc -s -c -o ${QJS_REPL_C} -m "${CMAKE_CURRENT_SOURCE_DIR}/repl.js"
    DEPENDS qjsc "${CMAKE_CURRENT_SOURCE_DIR}/repl.js"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating repl.c from repl.js with qjsc"
    VERBATIM
)

# ---------------------------------------------------------------------- qjs
add_executable(qjs qjs.c ${QJS_REPL_C})
target_link_libraries(qjs PRIVATE quickjs)

# run-test262 relies on POSIX-only APIs (ftw.h, pthread.h/stdatomic.h test
# harness threads, dirent.h) that have no MSVC equivalent shipped here; it
# is only a conformance-test driver, not part of the QuickJS engine/CLI
# itself, so it is intentionally left out of the MSVC build.
if(NOT MSVC)
    add_executable(run-test262 run-test262.c)
    target_link_libraries(run-test262 PRIVATE quickjs)
endif()

message(STATUS "QuickJS version: ${QJS_VERSION}")
message(STATUS "Compiler:        ${CMAKE_C_COMPILER_ID}")
