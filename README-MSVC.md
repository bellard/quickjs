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

## Nachträge nach zweitem MSVC-Testlauf

Ein weiterer echter Build-Log hat drei zusätzliche Probleme aufgedeckt:

5. **`C2011: "timeval": "struct" Typneudefinition`**: Der Windows SDK
   (`winsock.h`) deklariert `struct timeval` bereits selbst, geschützt
   durch das Makro `_TIMEVAL_DEFINED` (nicht durch den Include-Guard
   des Headers selbst). Unsere eigene Definition in `msvc_compat.h` war
   nur gegen `_WINSOCK2API_` abgesichert, was hier nicht griff. Jetzt
   wird zuerst `<winsock2.h>` eingebunden (das den korrekten Guard
   `_TIMEVAL_DEFINED` setzt) und unsere eigene Definition greift nur
   noch als Fallback, falls aus irgendeinem Grund kein Windows-Header
   `struct timeval` bereits deklariert hat.
6. **`S_IFIFO`/`S_IFBLK` nichtdeklariert** in `quickjs-libc.c`: MSVCs
   `<sys/stat.h>` kennt (anders als POSIX) keine Bit-Konstanten für
   Named Pipes/Block-Devices. Da diese als `os.S_IFIFO`/`os.S_IFBLK`
   an JS exportiert werden, wurden die Standard-POSIX-Werte dafür in
   `msvc_compat.h` ergänzt (rein als Konstanten, `stat()` wird unter
   Windows ohnehin nie diese Bits setzen).
7. **`C2124: Division oder Modulo durch Null`** in `quickjs.c`: An
   vier Stellen wurde `1.0 / 0.0` als (auf GCC/Clang funktionierender)
   Trick benutzt, um zur Compile-Zeit `+Infinity` zu erzeugen. MSVC
   lehnt eine literale Division durch `0.0` im Quelltext ab. Alle vier
   Stellen wurden durch das portable, standardisierte `INFINITY`-Makro
   aus `<math.h>` ersetzt (funktional identisch, aber ohne den
   Divisions-Trick) – betrifft u. a. `Number.POSITIVE_INFINITY`,
   `Math.max`/`Math.min` und das globale `Infinity`.

## Nachträge nach drittem MSVC-Testlauf: winsock.h vs. winsock2.h

Der dritte Log zeigte den klassischen Windows-Header-Konflikt zwischen
`<winsock.h>` (alt) und `<winsock2.h>` (neu) – dutzende
"Typneudefinition"-Fehler für `sockaddr`, `fd_set`, `timeval`,
`hostent` usw. Ursache: In `quickjs-libc.c` gab es einen bereits
bestehenden `#if defined(_WIN32) #include <windows.h> ...`-Block, der
**vor** unserem `#include "msvc_compat.h"` stand. Ohne dass vorher
`WIN32_LEAN_AND_MEAN` gesetzt war, hat dieses `<windows.h>` automatisch
das alte `<winsock.h>` mitgezogen. Als danach `msvc_compat.h` sein
eigenes `<winsock2.h>` einband, kollidierten beide Header miteinander.

Fix: `msvc_compat.h` wird jetzt in `quickjs-libc.c` als **allererstes**
eingebunden (noch vor `<unistd.h>`, `<windows.h>` & Co.), damit
`WIN32_LEAN_AND_MEAN` gesetzt und `<winsock2.h>` geladen ist, bevor
irgendetwas anderes `<windows.h>` anfassen kann. Zusätzlich habe ich
unsere eigene, redundante `struct timeval`-Definition aus
`msvc_compat.h` komplett entfernt – der `_TIMEVAL_DEFINED`-Guard, auf
den ich mich beim letzten Mal verlassen hatte, existiert in der
aktuellen Windows-11-SDK-Version (10.0.26100.0) offenbar nicht mehr in
der erwarteten Form. Da wir `<winsock2.h>` ohnehin selbst laden, ist
`struct timeval` dadurch bereits vorhanden – wir müssen sie nicht mehr
selbst deklarieren, nur noch `gettimeofday()` (das WinSock nicht
mitbringt) implementieren.

## Nachträge nach viertem MSVC-Testlauf: Laufzeitabsturz (kein Compile-Fehler!)

Diesmal kompilierte alles fehlerfrei, aber `qjs.exe` stürzte zur
**Laufzeit** mit `abort()` in `quickjs.c` (im `default:`-Zweig eines
`switch` über `cv->closure_type`) ab. Ursache war ein subtiler,
nicht-offensichtlicher Unterschied zwischen MSVC und GCC/Clang beim
Verhalten von **Bitfeldern mit `enum`-Typ**:

`JSClosureTypeEnum` hat 8 Werte (0–7), das zugehörige Bitfeld
`JSClosureTypeEnum closure_type : 3;` nutzt also exakt 3 Bits – gerade
genug für die Werte 0–7. Die Vorzeichenbehandlung eines
`enum`-Bitfelds ist in C **implementation-defined**: GCC/Clang
behandeln es hier faktisch als unsigned, MSVC dagegen behandelt es
als **signed** (da der zugrunde liegende Typ von `enum` bei MSVC
standardmäßig `int`, also signed, ist). Ein signed 3-Bit-Feld kann
aber nur Werte von **-4 bis 3** speichern! Die oberen vier
Enum-Werte (`JS_CLOSURE_GLOBAL_DECL`=4, `JS_CLOSURE_GLOBAL`=5,
`JS_CLOSURE_MODULE_DECL`=6, `JS_CLOSURE_MODULE_IMPORT`=7) wurden
dadurch beim Speichern in `-4, -3, -2, -1` verstümmelt. Jeder
darauffolgende `switch(cv->closure_type)` fand dann folgerichtig
keinen passenden `case` mehr und landete im `default: abort();`.

**Fix**: Das Bitfeld wurde von `JSClosureTypeEnum closure_type : 3`
auf `uint8_t closure_type : 3` umgestellt – ein expliziter, garantiert
unsigned Speichertyp umgeht die implementation-defined Mehrdeutigkeit
komplett und funktioniert identisch auf allen Compilern (Zuweisungen/
Vergleiche mit den Enum-Konstanten funktionieren weiterhin ganz normal
über implizite Konvertierung). Vorsichtshalber wurden noch vier
weitere `Enum-Typ : Breite`-Bitfelder im Code auf denselben sicheren
Musters umgestellt (`gc_phase`, `func_kind`, `func_type`, `kind` in
`JSIteratorHelperKindEnum`) – bei denen war die Breite zwar in jedem
Fall 8 Bit bei nur wenigen Enum-Werten, also praktisch unkritisch,
aber besser konsistent sicher als auf implementation-defined Verhalten
zu vertrauen.

Getestet (auf gcc/Linux, da mir kein echtes MSVC zur Verfügung steht):
Closures, verschachtelte Closures, `eval`-globale `var`/`let`-
Deklarationen sowie ES-Modul-Import/Export (der genau die zuvor
verstümmelten Werte 6/7 durchläuft) laufen jetzt alle korrekt durch.

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
