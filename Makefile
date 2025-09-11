#
# QuickJS Javascript Engine
#
# Copyright (c) 2017-2021 Fabrice Bellard
# Copyright (c) 2017-2021 Charlie Gordon
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

ifeq ($(shell uname -s),Darwin)
CONFIG_DARWIN=y
endif
ifeq ($(shell uname -s),FreeBSD)
CONFIG_FREEBSD=y
endif
# Windows cross compilation from Linux
# May need to have libwinpthread*.dll alongside the executable
# (On Ubuntu/Debian may be installed with mingw-w64-x86-64-dev
# to /usr/x86_64-w64-mingw32/lib/libwinpthread-1.dll)
#CONFIG_WIN32=y
# use link time optimization (smaller and faster executables but slower build)
#CONFIG_LTO=y
# consider warnings as errors (for development)
#CONFIG_WERROR=y
# force 32 bit build on x86_64
#CONFIG_M32=y
# cosmopolitan build (see https://github.com/jart/cosmopolitan)
#CONFIG_COSMO=y

# installation directory
PREFIX?=/usr/local

# use the gprof profiler
#CONFIG_PROFILE=y
# use address sanitizer
#CONFIG_ASAN=y
# use memory sanitizer
#CONFIG_MSAN=y
# use UB sanitizer
#CONFIG_UBSAN=y

OBJDIR=.obj

ifdef CONFIG_ASAN
OBJDIR:=$(OBJDIR)/asan
endif
ifdef CONFIG_MSAN
OBJDIR:=$(OBJDIR)/msan
endif
ifdef CONFIG_UBSAN
OBJDIR:=$(OBJDIR)/ubsan
endif

ifdef CONFIG_DARWIN
# use clang instead of gcc
CONFIG_CLANG=y
CONFIG_DEFAULT_AR=y
endif
ifdef CONFIG_FREEBSD
# use clang instead of gcc
CONFIG_CLANG=y
CONFIG_DEFAULT_AR=y
CONFIG_LTO=
endif

ifdef CONFIG_WIN32
  ifdef CONFIG_M32
    CROSS_PREFIX?=i686-w64-mingw32-
  else
    CROSS_PREFIX?=x86_64-w64-mingw32-
  endif
  EXE=.exe
else ifdef MSYSTEM
  CONFIG_WIN32=y
  CROSS_PREFIX?=
  EXE=.exe
else
  CROSS_PREFIX?=
  EXE=
endif

ifdef CONFIG_CLANG
  HOST_CC=clang
  CC=$(CROSS_PREFIX)clang
  CFLAGS+=-g -Wall -MMD -MF $(OBJDIR)/$(@F).d
  CFLAGS += -Wextra
  CFLAGS += -Wno-sign-compare
  CFLAGS += -Wno-missing-field-initializers
  CFLAGS += -Wundef -Wuninitialized
  CFLAGS += -Wunused -Wno-unused-parameter
  CFLAGS += -Wwrite-strings
  CFLAGS += -Wchar-subscripts -funsigned-char
  CFLAGS += -MMD -MF $(OBJDIR)/$(@F).d
  ifdef CONFIG_DEFAULT_AR
    AR=$(CROSS_PREFIX)ar
  else
    ifdef CONFIG_LTO
      AR=$(CROSS_PREFIX)llvm-ar
    else
      AR=$(CROSS_PREFIX)ar
    endif
  endif
  LIB_FUZZING_ENGINE ?= "-fsanitize=fuzzer"
else ifdef CONFIG_COSMO
  CONFIG_LTO=
  HOST_CC=gcc
  CC=cosmocc
  # cosmocc does not correct support -MF
  CFLAGS=-g -Wall #-MMD -MF $(OBJDIR)/$(@F).d
  CFLAGS += -Wno-array-bounds -Wno-format-truncation
  AR=cosmoar
else
  HOST_CC=gcc
  CC=$(CROSS_PREFIX)gcc
  CFLAGS+=-g -Wall -MMD -MF $(OBJDIR)/$(@F).d
  CFLAGS += -Wno-array-bounds -Wno-format-truncation -Wno-infinite-recursion
  ifdef CONFIG_LTO
    AR=$(CROSS_PREFIX)gcc-ar
  else
    AR=$(CROSS_PREFIX)ar
  endif
endif
STRIP?=$(CROSS_PREFIX)strip
ifdef CONFIG_M32
CFLAGS+=-msse2 -mfpmath=sse # use SSE math for correct FP rounding
ifndef CONFIG_WIN32
CFLAGS+=-m32
LDFLAGS+=-m32
endif
endif
CFLAGS+=-fwrapv # ensure that signed overflows behave as expected
ifdef CONFIG_WERROR
CFLAGS+=-Werror
endif
DEFINES:=-D_GNU_SOURCE -DCONFIG_VERSION=\"$(shell cat VERSION)\"
ifdef CONFIG_WIN32
DEFINES+=-D__USE_MINGW_ANSI_STDIO # for standard snprintf behavior
endif
ifndef CONFIG_WIN32
ifeq ($(shell $(CC) -o /dev/null compat/test-closefrom.c 2>/dev/null && echo 1),1)
DEFINES+=-DHAVE_CLOSEFROM
endif
endif

CFLAGS+=$(DEFINES)
CFLAGS_DEBUG=$(CFLAGS) -O0
CFLAGS_SMALL=$(CFLAGS) -Os
CFLAGS_OPT=$(CFLAGS) -O2
CFLAGS_NOLTO:=$(CFLAGS_OPT)
ifdef CONFIG_COSMO
LDFLAGS+=-s # better to strip by default
else
LDFLAGS+=-g
endif
ifdef CONFIG_LTO
CFLAGS_SMALL+=-flto
CFLAGS_OPT+=-flto
LDFLAGS+=-flto
endif
ifdef CONFIG_PROFILE
CFLAGS+=-p
LDFLAGS+=-p
endif
ifdef CONFIG_ASAN
CFLAGS+=-fsanitize=address -fno-omit-frame-pointer
LDFLAGS+=-fsanitize=address -fno-omit-frame-pointer
endif
ifdef CONFIG_MSAN
CFLAGS+=-fsanitize=memory -fno-omit-frame-pointer
LDFLAGS+=-fsanitize=memory -fno-omit-frame-pointer
endif
ifdef CONFIG_UBSAN
CFLAGS+=-fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS+=-fsanitize=undefined -fno-omit-frame-pointer
endif
ifdef CONFIG_WIN32
LDEXPORT=
else
LDEXPORT=-rdynamic
endif

ifndef CONFIG_COSMO
ifndef CONFIG_DARWIN
ifndef CONFIG_WIN32
CONFIG_SHARED_LIBS=y # building shared libraries is supported
endif
endif
endif

PROGS=qjs$(EXE) qjsc$(EXE) run-test262$(EXE)

ifneq ($(CROSS_PREFIX),)
QJSC_CC=gcc
QJSC=./host-qjsc
PROGS+=$(QJSC)
else
QJSC_CC=$(CC)
QJSC=./qjsc$(EXE)
endif
PROGS+=libquickjs.a
ifdef CONFIG_LTO
PROGS+=libquickjs.lto.a
endif

# examples
ifeq ($(CROSS_PREFIX),)
ifndef CONFIG_ASAN
ifndef CONFIG_MSAN
ifndef CONFIG_UBSAN
PROGS+=examples/hello examples/test_fib
# no -m32 option in qjsc
ifndef CONFIG_M32
ifndef CONFIG_WIN32
PROGS+=examples/hello_module
endif
endif
ifdef CONFIG_SHARED_LIBS
PROGS+=examples/fib.so examples/point.so
endif
endif
endif
endif
endif

all: $(OBJDIR) $(OBJDIR)/quickjs.check.o $(OBJDIR)/qjs.check.o $(PROGS)

QJS_LIB_OBJS=$(OBJDIR)/quickjs.o $(OBJDIR)/dtoa.o $(OBJDIR)/libregexp.o $(OBJDIR)/libunicode.o $(OBJDIR)/cutils.o $(OBJDIR)/quickjs-libc.o

QJS_OBJS=$(OBJDIR)/qjs.o $(OBJDIR)/repl.o $(QJS_LIB_OBJS)

HOST_LIBS=-lm -ldl -lpthread
LIBS=-lm -lpthread
ifndef CONFIG_WIN32
LIBS+=-ldl
endif
LIBS+=$(EXTRA_LIBS)

$(OBJDIR):
	mkdir -p $(OBJDIR) $(OBJDIR)/examples $(OBJDIR)/tests

qjs$(EXE): $(QJS_OBJS)
	$(CC) $(LDFLAGS) $(LDEXPORT) -o $@ $^ $(LIBS)

qjs-debug$(EXE): $(patsubst %.o, %.debug.o, $(QJS_OBJS))
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

qjsc$(EXE): $(OBJDIR)/qjsc.o $(QJS_LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

fuzz_eval: $(OBJDIR)/fuzz_eval.o $(OBJDIR)/fuzz_common.o libquickjs.fuzz.a
	$(CC) $(CFLAGS_OPT) $^ -o fuzz_eval $(LIB_FUZZING_ENGINE)

fuzz_compile: $(OBJDIR)/fuzz_compile.o $(OBJDIR)/fuzz_common.o libquickjs.fuzz.a
	$(CC) $(CFLAGS_OPT) $^ -o fuzz_compile $(LIB_FUZZING_ENGINE)

fuzz_regexp: $(OBJDIR)/fuzz_regexp.o $(OBJDIR)/libregexp.fuzz.o $(OBJDIR)/cutils.fuzz.o $(OBJDIR)/libunicode.fuzz.o
	$(CC) $(CFLAGS_OPT) $^ -o fuzz_regexp $(LIB_FUZZING_ENGINE)

libfuzzer: fuzz_eval fuzz_compile fuzz_regexp

ifneq ($(CROSS_PREFIX),)

$(QJSC): $(OBJDIR)/qjsc.host.o \
    $(patsubst %.o, %.host.o, $(QJS_LIB_OBJS))
	$(HOST_CC) $(LDFLAGS) -o $@ $^ $(HOST_LIBS)

endif #CROSS_PREFIX

QJSC_DEFINES:=-DCONFIG_CC=\"$(QJSC_CC)\" -DCONFIG_PREFIX=\"$(PREFIX)\"
ifdef CONFIG_LTO
QJSC_DEFINES+=-DCONFIG_LTO
endif
QJSC_HOST_DEFINES:=-DCONFIG_CC=\"$(HOST_CC)\" -DCONFIG_PREFIX=\"$(PREFIX)\"

$(OBJDIR)/qjsc.o: CFLAGS+=$(QJSC_DEFINES)
$(OBJDIR)/qjsc.host.o: CFLAGS+=$(QJSC_HOST_DEFINES)

ifdef CONFIG_LTO
LTOEXT=.lto
else
LTOEXT=
endif

libquickjs$(LTOEXT).a: $(QJS_LIB_OBJS)
	$(AR) rcs $@ $^

ifdef CONFIG_LTO
libquickjs.a: $(patsubst %.o, %.nolto.o, $(QJS_LIB_OBJS))
	$(AR) rcs $@ $^
endif # CONFIG_LTO

libquickjs.fuzz.a: $(patsubst %.o, %.fuzz.o, $(QJS_LIB_OBJS))
	$(AR) rcs $@ $^

repl.c: $(QJSC) repl.js
	$(QJSC) -s -c -o $@ -m repl.js

ifneq ($(wildcard unicode/UnicodeData.txt),)
$(OBJDIR)/libunicode.o $(OBJDIR)/libunicode.nolto.o: libunicode-table.h

libunicode-table.h: unicode_gen
	./unicode_gen unicode $@
endif

run-test262$(EXE): $(OBJDIR)/run-test262.o $(QJS_LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

run-test262-debug: $(patsubst %.o, %.debug.o, $(OBJDIR)/run-test262.o $(QJS_LIB_OBJS))
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# object suffix order: nolto

$(OBJDIR)/%.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS_OPT) -c -o $@ $<

$(OBJDIR)/fuzz_%.o: fuzz/fuzz_%.c | $(OBJDIR)
	$(CC) $(CFLAGS_OPT) -c -I. -o $@ $<

$(OBJDIR)/%.host.o: %.c | $(OBJDIR)
	$(HOST_CC) $(CFLAGS_OPT) -c -o $@ $<

$(OBJDIR)/%.pic.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS_OPT) -fPIC -DJS_SHARED_LIBRARY -c -o $@ $<

$(OBJDIR)/%.nolto.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS_NOLTO) -c -o $@ $<

$(OBJDIR)/%.debug.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS_DEBUG) -c -o $@ $<

$(OBJDIR)/%.fuzz.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS_OPT) -fsanitize=fuzzer-no-link -c -o $@ $<

$(OBJDIR)/%.check.o: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -DCONFIG_CHECK_JSVALUE -c -o $@ $<

regexp_test: libregexp.c libunicode.c cutils.c
	$(CC) $(LDFLAGS) $(CFLAGS) -DTEST -o $@ libregexp.c libunicode.c cutils.c $(LIBS)

unicode_gen: $(OBJDIR)/unicode_gen.host.o $(OBJDIR)/cutils.host.o libunicode.c unicode_gen_def.h
	$(HOST_CC) $(LDFLAGS) $(CFLAGS) -o $@ $(OBJDIR)/unicode_gen.host.o $(OBJDIR)/cutils.host.o

clean:
	rm -f repl.c out.c
	rm -f *.a *.o *.d *~ unicode_gen regexp_test fuzz_eval fuzz_compile fuzz_regexp $(PROGS)
	rm -f hello.c test_fib.c
	rm -f examples/*.so tests/*.so
	rm -rf $(OBJDIR)/ *.dSYM/ qjs-debug$(EXE)
	rm -rf run-test262-debug$(EXE)
	rm -f run_octane run_sunspider_like

install: all
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	$(STRIP) qjs$(EXE) qjsc$(EXE)
	install -m755 qjs$(EXE) qjsc$(EXE) "$(DESTDIR)$(PREFIX)/bin"
	mkdir -p "$(DESTDIR)$(PREFIX)/lib/quickjs"
	install -m644 libquickjs.a "$(DESTDIR)$(PREFIX)/lib/quickjs"
ifdef CONFIG_LTO
	install -m644 libquickjs.lto.a "$(DESTDIR)$(PREFIX)/lib/quickjs"
endif
	mkdir -p "$(DESTDIR)$(PREFIX)/include/quickjs"
	install -m644 quickjs.h quickjs-libc.h "$(DESTDIR)$(PREFIX)/include/quickjs"

###############################################################################
# examples

# example of static JS compilation
HELLO_SRCS=examples/hello.js
HELLO_OPTS=-fno-string-normalize -fno-map -fno-promise -fno-typedarray \
           -fno-typedarray -fno-regexp -fno-json -fno-eval -fno-proxy \
           -fno-date -fno-module-loader

hello.c: $(QJSC) $(HELLO_SRCS)
	$(QJSC) -e $(HELLO_OPTS) -o $@ $(HELLO_SRCS)

examples/hello: $(OBJDIR)/hello.o $(QJS_LIB_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

# example of static JS compilation with modules
HELLO_MODULE_SRCS=examples/hello_module.js
HELLO_MODULE_OPTS=-fno-string-normalize -fno-map -fno-typedarray \
           -fno-typedarray -fno-regexp -fno-json -fno-eval -fno-proxy \
           -fno-date -m
examples/hello_module: $(QJSC) libquickjs$(LTOEXT).a $(HELLO_MODULE_SRCS)
	$(QJSC) $(HELLO_MODULE_OPTS) -o $@ $(HELLO_MODULE_SRCS)

# use of an external C module (static compilation)

test_fib.c: $(QJSC) examples/test_fib.js
	$(QJSC) -e -M examples/fib.so,fib -m -o $@ examples/test_fib.js

examples/test_fib: $(OBJDIR)/test_fib.o $(OBJDIR)/examples/fib.o libquickjs$(LTOEXT).a
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

examples/fib.so: $(OBJDIR)/examples/fib.pic.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

examples/point.so: $(OBJDIR)/examples/point.pic.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

###############################################################################
# documentation

DOCS=doc/quickjs.pdf doc/quickjs.html

build_doc: $(DOCS)

clean_doc:
	rm -f $(DOCS)

doc/version.texi: VERSION
	@echo "@set VERSION `cat $<`" > $@

doc/%.pdf: doc/%.texi doc/version.texi
	texi2pdf --clean -o $@ -q $<

doc/%.html.pre: doc/%.texi doc/version.texi
	makeinfo --html --no-headers --no-split --number-sections -o $@ $<

doc/%.html: doc/%.html.pre
	sed -e 's|</style>|</style>\n<meta name="viewport" content="width=device-width, initial-scale=1.0">|' < $< > $@

###############################################################################
# tests

ifdef CONFIG_SHARED_LIBS
test: tests/bjson.so examples/point.so
endif

test: qjs$(EXE)
	$(WINE) ./qjs$(EXE) tests/test_closure.js
	$(WINE) ./qjs$(EXE) tests/test_language.js
	$(WINE) ./qjs$(EXE) --std tests/test_builtin.js
	$(WINE) ./qjs$(EXE) tests/test_loop.js
	$(WINE) ./qjs$(EXE) tests/test_bigint.js
	$(WINE) ./qjs$(EXE) tests/test_cyclic_import.js
	$(WINE) ./qjs$(EXE) tests/test_worker.js
ifndef CONFIG_WIN32
	$(WINE) ./qjs$(EXE) tests/test_std.js
endif
ifdef CONFIG_SHARED_LIBS
	$(WINE) ./qjs$(EXE) tests/test_bjson.js
	$(WINE) ./qjs$(EXE) examples/test_point.js
endif

stats: qjs$(EXE)
	$(WINE) ./qjs$(EXE) -qd

microbench: qjs$(EXE)
	$(WINE) ./qjs$(EXE) --std tests/microbench.js

ifeq ($(wildcard test262o/tests.txt),)
test2o test2o-update:
	@echo test262o tests not installed
else
# ES5 tests (obsolete)
test2o: run-test262
	time ./run-test262 -t -m -c test262o.conf

test2o-update: run-test262
	./run-test262 -t -u -c test262o.conf
endif

ifeq ($(wildcard test262/features.txt),)
test2 test2-update test2-default test2-check:
	@echo test262 tests not installed
else
# Test262 tests
test2-default: run-test262
	time ./run-test262 -t -m -c test262.conf

test2: run-test262
	time ./run-test262 -t -m -c test262.conf -a

test2-update: run-test262
	./run-test262 -t -u -c test262.conf -a

test2-check: run-test262
	time ./run-test262 -t -m -c test262.conf -E -a
endif

testall: all test microbench test2o test2

testall-complete: testall

node-test:
	node tests/test_closure.js
	node tests/test_language.js
	node tests/test_builtin.js
	node tests/test_loop.js
	node tests/test_bigint.js

node-microbench:
	node tests/microbench.js -s microbench-node.txt
	node --jitless tests/microbench.js -s microbench-node-jitless.txt

bench-v8: qjs
	make -C tests/bench-v8
	./qjs -d tests/bench-v8/combined.js

node-bench-v8:
	make -C tests/bench-v8
	node --jitless tests/bench-v8/combined.js

tests/bjson.so: $(OBJDIR)/tests/bjson.pic.o
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(LIBS)

BENCHMARKDIR=../quickjs-benchmarks

run_sunspider_like: $(BENCHMARKDIR)/run_sunspider_like.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DNO_INCLUDE_DIR -I. -o $@ $< libquickjs$(LTOEXT).a $(LIBS)

run_octane: $(BENCHMARKDIR)/run_octane.c
	$(CC) $(CFLAGS) $(LDFLAGS) -DNO_INCLUDE_DIR -I. -o $@ $< libquickjs$(LTOEXT).a $(LIBS)

benchmarks: run_sunspider_like run_octane
	./run_sunspider_like $(BENCHMARKDIR)/kraken-1.0/
	./run_sunspider_like $(BENCHMARKDIR)/kraken-1.1/
	./run_sunspider_like $(BENCHMARKDIR)/sunspider-1.0/
	./run_octane $(BENCHMARKDIR)/

-include $(wildcard $(OBJDIR)/*.d)
