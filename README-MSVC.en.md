# QuickJS – MSVC support

This project can now be built with **MSVC** (`cl.exe` / Visual Studio) in
addition to the existing GNU `Makefile` (gcc/clang/MinGW). All changes are
attached exclusively via `#ifdef _MSC_VER` at the affected spots – nothing
changes for GCC/Clang/MinGW (see "What was tested" below).

## Building on Windows with MSVC

Prerequisite: Visual Studio (2019 or newer, with the "Desktop development
with C++" workload) and CMake.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Or from an "x64 Native Tools Command Prompt for VS":

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This produces `qjs.exe` (REPL/interpreter) and `qjsc.exe` (bytecode
compiler). The build runs in two stages, exactly like the Makefile: first
`qjsc.exe` is built, which is then used to turn `repl.js` into `repl.c`,
and only then is `qjs.exe` finished. CMake handles this automatically.

## What was changed

- **`cutils.h`**: MSVC replacements for `likely`/`unlikely`,
  `force_inline`, `no_inline`, `__maybe_unused`; new macros
  `ATTRIBUTE_PRINTF(a,b)` and `ALIGNED_(n)` as a portable replacement for
  the GCC attributes `__attribute__((format(printf,...)))` and
  `__attribute__((aligned(n)))` respectively; `clz32/64`/`ctz32/64` via
  `_BitScanReverse`/`_BitScanForward` instead of
  `__builtin_clz`/`__builtin_ctz`; `packed_u64/32/16` via
  `#pragma pack(push,1)` instead of `__attribute__((packed))`.
- **`quickjs.c`**:
  - The interpreter dispatch normally uses "computed goto" (`goto
    *label`), a GCC/Clang extension MSVC doesn't know. For `_MSC_VER`,
    `DIRECT_DISPATCH` is set to `0`, which automatically switches to the
    already-existing, portable `switch()`-based dispatch (the same one
    used by the Emscripten build).
  - `CONFIG_ATOMICS` (Atomics.wait/notify, needs POSIX threads via
    `<pthread.h>`) is disabled for MSVC, since MSVC doesn't ship pthreads
    out of the box. `Atomics.*` is therefore unavailable in MSVC builds –
    no other platform is affected.
  - `__builtin_frame_address` → `_AddressOfReturnAddress()` for stack
    overflow detection.
  - All `__attribute__((format(printf,...)))` and
    `__attribute__((aligned(...)))` spots switched to the new macros.
- **`quickjs-libc.c`**: Was already fairly well ported to `_WIN32`
  (MinGW) (dlopen, termios/ioctl, fork/exec/waitpid, symlink/readlink,
  kill, setenv/unsetenv, poll, ... are all already cleanly wrapped in
  `#if defined(_WIN32)` with a Windows alternative). Only the pieces
  MinGW additionally provides on top of `_WIN32`, which MSVC doesn't,
  were added: `<unistd.h>`, `<sys/time.h>`, `<dirent.h>`. `USE_WORKER`
  (os.Worker, needs `<pthread.h>`/`<stdatomic.h>`) is also disabled for
  MSVC.
- **`qjs.c`, `qjsc.c`, `dtoa.c`, `libregexp.c`, `run-test262.c`**:
  analogous smaller adjustments (includes, attribute macro).
- **New: `msvc_compat.h`**: Central shim file, only active for
  `_MSC_VER`. Provides: `struct timeval` + `gettimeofday()`,
  `strcasecmp`/`strncasecmp`, `ssize_t`, aliases for
  `open/close/read/write/lseek/unlink/dup/dup2/isatty/fileno/mkdir/
  popen/pclose/getcwd/chdir`, `S_ISREG`/`S_ISDIR`, `sleep`/`usleep`, as
  well as a minimal `DIR`/`opendir`/`readdir`/`closedir` (via
  `FindFirstFile`/`FindNextFile`).
- **New: `CMakeLists.txt`**: Since previously there was only a GNU
  `Makefile` (which cannot address MSVC directly), an additional build
  system was needed to be able to build with `cl.exe` at all.

## Known limitations under MSVC

- **`Atomics.*`** (JS API) is disabled, since it would need POSIX
  threads, which MSVC doesn't provide.
- **`os.Worker`** (worker threads in `quickjs-libc.c`) is disabled for
  the same reason.
- **`run-test262`** (the test262 conformance runner) is deliberately not
  built under MSVC – it relies heavily on `<ftw.h>`, `<pthread.h>`,
  `<stdatomic.h>`, and `<dirent.h>` for its test infrastructure, which
  isn't needed for a pure build tool to test QuickJS itself. For testing
  purposes on Windows, gcc/clang (e.g. via MinGW or WSL) is still
  recommended.
- Dynamic loading of modules (`import()` of a `.dll`) was, even before
  these changes, only implemented for MinGW-style `_WIN32` builds (via
  `LoadLibrary`/`GetProcAddress`) – that now also applies to the MSVC
  build.

## Addendum after a real MSVC test run (thanks for the build log!)

The first pass was only cross-checked with gcc/pedantic heuristics, not
with real `cl.exe`. A test on an actual Windows machine uncovered four
more problems, which have now also been fixed:

1. **`(JSValue)v`-style casts** (`quickjs.h`, `quickjs.c`, 20 spots in
   total): In C, an explicit cast to a struct/union type isn't actually
   allowed (only to scalar types) – GCC/Clang tolerate `(JSValue)v` as a
   GCC extension when the source and target types are identical (which
   is always the case here, since `JSValueConst` is a pure alias of
   `JSValue` via `#define JSValueConst JSValue`). MSVC rejects this with
   `C2440`. Since the cast was always a no-op anyway, all 20 spots were
   simply removed (`(JSValue)v` → `v`) – this doesn't change behavior on
   any platform.
2. **`optind`** in `qjsc.c`: The code reuses the global variable
   `optind`, normally declared by `<unistd.h>`, for its own hand-rolled
   option parsing, without declaring it itself. Since `<unistd.h>` isn't
   included for MSVC, the declaration was missing – now added via
   `static int optind;` for `_MSC_VER`.
3. **`<utime.h>`** in `quickjs-libc.c`: MinGW provides both `<utime.h>`
   and `<sys/utime.h>`, MSVC's CRT only the latter. `<sys/utime.h>` is
   now included for `_MSC_VER`.
4. **`__attribute((unused))`** (missing second underscore) on
   `dump_token()` in `quickjs.c`: GCC/Clang also accept the short form
   `__attribute` as an alias for `__attribute__` – this instance slipped
   through the initial code search for `__attribute__` (with the double
   underscore). Now switched to `__maybe_unused`.

Additionally, I searched through the entire source tree once more with
`gcc -std=c11 -pedantic-errors` (which catches, among other things,
exactly the "cast to non-scalar type" class of errors MSVC complained
about above) – no further hits of this kind turned up afterward. For all
the other constructs reported by `-pedantic-errors` (computed goto,
`__int128`, address of a label, flexible array members), it's already
known and verified that they're either already disabled for MSVC
(computed goto, `__int128` via `CONFIG_ATOMICS`/`JS_LIMB_BITS`) or are
accepted by MSVC with only a warning (not an error) anyway (flexible
array members, `C4200`).

## Addendum after a second MSVC test run

Another real build log uncovered three additional problems:

5. **`C2011: "timeval": "struct" redefinition`**: The Windows SDK
   (`winsock.h`) already declares `struct timeval` itself, guarded by
   the macro `_TIMEVAL_DEFINED` (not by the header's own include guard).
   Our own definition in `msvc_compat.h` was only guarded against
   `_WINSOCK2API_`, which didn't catch this case. Now `<winsock2.h>` is
   included first (which sets the correct `_TIMEVAL_DEFINED` guard) and
   our own definition only kicks in as a fallback, in case for some
   reason no Windows header had already declared `struct timeval`.
6. **`S_IFIFO`/`S_IFBLK` undeclared** in `quickjs-libc.c`: MSVC's
   `<sys/stat.h>` doesn't know (unlike POSIX) the bit constants for
   named pipes/block devices. Since these are exported to JS as
   `os.S_IFIFO`/`os.S_IFBLK`, the standard POSIX values for them were
   added in `msvc_compat.h` (purely as constants – `stat()` will never
   set these bits on Windows anyway).
7. **`C2124: division or modulo by zero`** in `quickjs.c`: In four
   places, `1.0 / 0.0` was used as a trick (that works on GCC/Clang) to
   produce `+Infinity` at compile time. MSVC rejects a literal division
   by `0.0` in source code. All four spots were replaced with the
   portable, standardized `INFINITY` macro from `<math.h>` (functionally
   identical, but without the division trick) – this affects, among
   other things, `Number.POSITIVE_INFINITY`, `Math.max`/`Math.min`, and
   the global `Infinity`.

## Addendum after a third MSVC test run: winsock.h vs. winsock2.h

The third log showed the classic Windows header conflict between
`<winsock.h>` (old) and `<winsock2.h>` (new) – dozens of "type
redefinition" errors for `sockaddr`, `fd_set`, `timeval`, `hostent`,
etc. Root cause: `quickjs-libc.c` had an existing `#if defined(_WIN32)
#include <windows.h> ...` block that came **before** our `#include
"msvc_compat.h"`. Since `WIN32_LEAN_AND_MEAN` hadn't been set yet at
that point, this `<windows.h>` automatically pulled in the old
`<winsock.h>`. When `msvc_compat.h` then included its own
`<winsock2.h>`, the two headers collided.

Fix: `msvc_compat.h` is now included **first** in `quickjs-libc.c`
(before `<unistd.h>`, `<windows.h>`, etc.), so that `WIN32_LEAN_AND_MEAN`
is set and `<winsock2.h>` is loaded before anything else can touch
`<windows.h>`. In addition, I completely removed our own, redundant
`struct timeval` definition from `msvc_compat.h` – the `_TIMEVAL_DEFINED`
guard I had relied on last time apparently doesn't exist in the expected
form in the current Windows 11 SDK version (10.0.26100.0). Since we now
load `<winsock2.h>` ourselves anyway, `struct timeval` is already
available through that – we no longer need to declare it ourselves, only
`gettimeofday()` (which WinSock doesn't provide) still needs to be
implemented.

## Addendum after a fourth MSVC test run: runtime crash (not a compile error!)

This time everything compiled without errors, but `qjs.exe` crashed at
**runtime** with `abort()` in `quickjs.c` (in the `default:` branch of a
`switch` over `cv->closure_type`). The cause was a subtle, non-obvious
difference between MSVC and GCC/Clang in how **bit-fields with an `enum`
type** behave:

`JSClosureTypeEnum` has 8 values (0–7), so the corresponding bit-field
`JSClosureTypeEnum closure_type : 3;` uses exactly 3 bits – just enough
for values 0–7. The signedness of an `enum` bit-field is
**implementation-defined** in C: GCC/Clang effectively treat it as
unsigned here, whereas MSVC treats it as **signed** (since MSVC's
underlying type for `enum` defaults to `int`, i.e. signed). But a signed
3-bit field can only store values from **-4 to 3**! The upper four enum
values (`JS_CLOSURE_GLOBAL_DECL`=4, `JS_CLOSURE_GLOBAL`=5,
`JS_CLOSURE_MODULE_DECL`=6, `JS_CLOSURE_MODULE_IMPORT`=7) were thus
mangled into `-4, -3, -2, -1` when stored. Every subsequent
`switch(cv->closure_type)` then, quite correctly, failed to find a
matching `case` and fell through to `default: abort();`.

**Fix**: The bit-field was changed from `JSClosureTypeEnum closure_type
: 3` to `uint8_t closure_type : 3` – an explicit, guaranteed-unsigned
storage type sidesteps the implementation-defined ambiguity entirely and
works identically on every compiler (assignments/comparisons with the
enum constants continue to work perfectly normally via implicit
conversion). As a precaution, four other `enum-type : width` bit-fields
in the code were switched to the same safe pattern (`gc_phase`,
`func_kind`, `func_type`, `kind` in `JSIteratorHelperKindEnum`) – in
those cases the width was always 8 bits with only a handful of enum
values, so practically speaking they weren't at risk, but it's better to
be consistently safe than to rely on implementation-defined behavior.

Tested (on gcc/Linux, since I don't have access to a real MSVC):
closures, nested closures, `eval`-level global `var`/`let` declarations,
as well as ES module import/export (which exercises exactly the
previously-mangled values 6/7) all now run correctly.

There is no real MSVC compiler available in this environment (Linux
only). I was therefore **unable** to cross-test with `cl.exe`. Instead,
I did the following:

1. Systematically searched the entire source tree for GCC/Clang-specific
   constructs (computed goto, `__attribute__`, `__builtin_*`, missing
   headers like `unistd.h`/`dirent.h`/`sys/time.h`, `pthread.h`,
   `dlopen`, `fork`/`exec`, etc.) and checked every match individually.
2. After every change, verified that the **existing Linux/gcc build
   keeps working unchanged** – both via the original `Makefile` and via
   the new `CMakeLists.txt` (both build without errors, with the same
   warnings as before).
3. Tested the built `qjs`/`qjsc` binaries with real JS snippets
   (classes, regex, BigInt, async/await, `Math.clz32`, JSON) – everything
   works as expected.

I recommend testing the MSVC build on a real Windows machine and letting
me know about any compiler errors – for a project of this size (~90,000
lines), it's realistic that another spot or two will still need fixing,
even though I've systematically searched for every problem case I'm
aware of.
