# QuickJS – MSVC-Unterstützung

Dieses Projekt lässt sich jetzt zusätzlich zum bestehenden GNU-`Makefile`
(gcc/clang/MinGW) auch mit **MSVC** (`cl.exe` / Visual Studio) bauen. Alle
Änderungen sind ausschließlich über `#ifdef _MSC_VER` an die betroffenen
Stellen angehängt – für GCC/Clang/MinGW ändert sich nichts (siehe
"Was getestet wurde" unten).

## Build unter Windows mit MSVC

Voraussetzung: Visual Studio (2019 oder neuer, mit "Desktop development
with C++" Workload) und CMake.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Oder aus einer "x64 Native Tools Command Prompt for VS":

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Das erzeugt `qjs.exe` (REPL/Interpreter) und `qjsc.exe` (Compiler nach
Bytecode). Der Build läuft zweistufig, genau wie im Makefile: zuerst wird
`qjsc.exe` gebaut, damit läuft `repl.js` -> `repl.c`, und erst dann wird
`qjs.exe` fertig gebaut. Das übernimmt CMake automatisch.

## Was geändert wurde

- **`cutils.h`**: MSVC-Ersatz für `likely`/`unlikely`, `force_inline`,
  `no_inline`, `__maybe_unused`; neue Makros `ATTRIBUTE_PRINTF(a,b)` und
  `ALIGNED_(n)` als portabler Ersatz für die GCC-Attribute
  `__attribute__((format(printf,...)))` bzw. `__attribute__((aligned(n)))`;
  `clz32/64`/`ctz32/64` über `_BitScanReverse`/`_BitScanForward` statt
  `__builtin_clz`/`__builtin_ctz`; `packed_u64/32/16` über
  `#pragma pack(push,1)` statt `__attribute__((packed))`.
- **`quickjs.c`**:
  - Der Interpreter-Dispatch nutzt normalerweise "computed goto"
    (`goto *label`), eine GCC/Clang-Erweiterung, die MSVC nicht kennt.
    Für `_MSC_VER` wird `DIRECT_DISPATCH` auf `0` gesetzt, wodurch
    automatisch der bereits vorhandene, portable `switch()`-basierte
    Dispatch verwendet wird (derselbe, den auch der Emscripten-Build
    nutzt).
  - `CONFIG_ATOMICS` (Atomics.wait/notify, benötigt POSIX-Threads via
    `<pthread.h>`) ist für MSVC deaktiviert, da MSVC von Haus aus kein
    pthreads mitbringt. `Atomics.*` ist unter MSVC-Builds dadurch nicht
    verfügbar – alle anderen Plattformen sind davon nicht betroffen.
  - `__builtin_frame_address` → `_AddressOfReturnAddress()` für die
    Stack-Overflow-Erkennung.
  - Alle `__attribute__((format(printf,...)))`- und
    `__attribute__((aligned(...)))`-Stellen auf die neuen Makros
    umgestellt.
- **`quickjs-libc.c`**: War bereits recht weitgehend für `_WIN32`
  (MinGW) portiert (dlopen, termios/ioctl, fork/exec/waitpid,
  symlink/readlink, kill, setenv/unsetenv, poll, ... sind alle schon
  sauber per `#if defined(_WIN32)` mit einer Windows-Alternative
  versehen). Ergänzt wurden nur die Stellen, die MinGW zusätzlich zu
  `_WIN32` noch mitbringt, MSVC aber nicht: `<unistd.h>`,
  `<sys/time.h>`, `<dirent.h>`. `USE_WORKER` (os.Worker, benötigt
  `<pthread.h>`/`<stdatomic.h>`) ist für MSVC ebenfalls deaktiviert.
- **`qjs.c`, `qjsc.c`, `dtoa.c`, `libregexp.c`, `run-test262.c`**:
  analoge kleinere Anpassungen (Includes, Attribut-Makro).
- **Neu: `msvc_compat.h`**: Zentrale Shim-Datei, nur für `_MSC_VER`
  wirksam. Stellt bereit: `struct timeval` + `gettimeofday()`,
  `strcasecmp`/`strncasecmp`, `ssize_t`, Aliase für
  `open/close/read/write/lseek/unlink/dup/dup2/isatty/fileno/mkdir/
  popen/pclose/getcwd/chdir`, `S_ISREG`/`S_ISDIR`, `sleep`/`usleep`
  sowie ein minimales `DIR`/`opendir`/`readdir`/`closedir` (über
  `FindFirstFile`/`FindNextFile`).
- **Neu: `CMakeLists.txt`**: Da es bisher nur ein GNU-`Makefile` gab
  (das MSVC nicht direkt ansprechen kann), war ein zusätzliches
  Build-System nötig, um mit `cl.exe` überhaupt bauen zu können.

## Bekannte Einschränkungen unter MSVC

- **`Atomics.*`** (JS-API) ist deaktiviert, da dafür POSIX-Threads
  gebraucht würden, die MSVC nicht mitbringt.
- **`os.Worker`** (Worker-Threads in `quickjs-libc.c`) ist aus dem
  gleichen Grund deaktiviert.
- **`run-test262`** (der Test262-Konformitätsrunner) wird unter MSVC
  bewusst nicht gebaut – er nutzt `<ftw.h>`, `<pthread.h>`,
  `<stdatomic.h>` und `<dirent.h>` sehr tief für die Testinfrastruktur,
  was für ein reines Build-Tool zum Testen von QuickJS selbst nicht
  nötig ist. Für Testzwecke unter Windows empfiehlt sich weiterhin
  gcc/clang (z. B. via MinGW oder WSL).
- Dynamisches Nachladen von Modulen (`import()` von `.dll`) war schon
  vor diesen Änderungen nur für MinGW-artige `_WIN32`-Builds
  implementiert (via `LoadLibrary`/`GetProcAddress`) – das gilt jetzt
  auch für den MSVC-Build.

## Nachträge nach echtem MSVC-Testlauf (danke für den Build-Log!)

Der erste Durchgang war nur mit gcc/pedantic-Heuristiken gegengeprüft, nicht
mit echtem `cl.exe`. Ein Test auf einer echten Windows-Maschine hat vier
weitere Probleme aufgedeckt, die jetzt ebenfalls behoben sind:

1. **`(JSValue)v`-artige Casts** (`quickjs.h`, `quickjs.c`, insgesamt 20
   Stellen): In C ist ein expliziter Cast auf einen Struct-/Union-Typ
   eigentlich nicht erlaubt (nur auf skalare Typen) – GCC/Clang tolerieren
   `(JSValue)v` als GCC-Erweiterung, wenn Quell- und Zieltyp identisch
   sind (was hier immer der Fall ist, da `JSValueConst` per `#define
   JSValueConst JSValue` ein reiner Alias von `JSValue` ist). MSVC lehnt
   das mit `C2440` ab. Da der Cast dadurch ohnehin immer ein No-op war,
   wurden alle 20 Stellen einfach entfernt (`(JSValue)v` → `v`) – das
   ändert das Verhalten auf keiner Plattform.
2. **`optind`** in `qjsc.c`: Der Code nutzt für seine eigene,
   handgeschriebene Optionsverarbeitung die von `<unistd.h>` global
   deklarierte Variable `optind` mit, ohne sie selbst zu deklarieren.
   Da `<unistd.h>` für MSVC nicht eingebunden wird, fehlte die
   Deklaration – jetzt per `static int optind;` für `_MSC_VER` ergänzt.
3. **`<utime.h>`** in `quickjs-libc.c`: MinGW stellt sowohl `<utime.h>`
   als auch `<sys/utime.h>` bereit, MSVCs CRT nur Letzteres. Für
   `_MSC_VER` wird jetzt `<sys/utime.h>` eingebunden.
4. **`__attribute((unused))`** (fehlender zweiter Unterstrich) bei
   `dump_token()` in `quickjs.c`: GCC/Clang akzeptieren auch die
   Kurzform `__attribute` als Alias für `__attribute__` – dieser Fall
   war bei der ersten Codesuche nach `__attribute__` (mit Doppel-
   Unterstrich) durchgerutscht. Jetzt auf `__maybe_unused` umgestellt.

Zusätzlich habe ich den kompletten Quellbaum noch einmal mit
`gcc -std=c11 -pedantic-errors` durchsucht (das erkennt u. a. genau
die "Cast auf Nicht-Skalartyp"-Klasse von Fehlern, die MSVC oben
bemängelt hat) – danach kamen keine weiteren Treffer dieser Art mehr.
Für alle übrigen von `-pedantic-errors` gemeldeten Konstrukte
(computed goto, `__int128`, Adresse eines Labels, Flexible-Array-
Members) ist bereits bekannt und geprüft, dass sie entweder für MSVC
schon deaktiviert sind (computed goto, `__int128` via `CONFIG_ATOMICS`/
`JS_LIMB_BITS`) oder von MSVC ohnehin nur mit einer Warnung (nicht
Fehler) akzeptiert werden (Flexible Array Members, `C4200`).



In dieser Umgebung steht kein echter MSVC-Compiler zur Verfügung (nur
Linux). Ich konnte daher **nicht** mit `cl.exe` gegentesten. Stattdessen
habe ich:

1. Den kompletten Quellbaum systematisch nach GCC/Clang-spezifischen
   Konstrukten durchsucht (computed goto, `__attribute__`, `__builtin_*`,
   fehlende Header wie `unistd.h`/`dirent.h`/`sys/time.h`, `pthread.h`,
   `dlopen`, `fork`/`exec` usw.) und jede Fundstelle einzeln geprüft.
2. Nach jeder Änderung geprüft, dass der **bestehende Linux/gcc-Build
   weiterhin unverändert funktioniert** – sowohl über das originale
   `Makefile` als auch über die neue `CMakeLists.txt` (beide bauen
   fehlerfrei, mit identischen Warnungen wie vorher).
3. Die gebauten `qjs`/`qjsc`-Binaries mit echten JS-Snippets getestet
   (Klassen, Regex, BigInt, async/await, `Math.clz32`, JSON) – alles
   funktioniert wie erwartet.

Ich empfehle, den MSVC-Build auf einer echten Windows-Maschine zu
testen und mir eventuelle Compiler-Fehler mitzuteilen – bei einem
Projekt dieser Größe (~90.000 Zeilen) ist es realistisch, dass noch
die eine oder andere Stelle nachgebessert werden muss, auch wenn ich
alle mir bekannten Problemfälle systematisch abgesucht habe.
