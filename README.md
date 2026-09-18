# cppic — transpiler C++ → C dla mikrokontrolerów PIC18

Kompilator języka C++ (podzbiór używany w skeczach Arduino, obiektowy C++)
do pliku HEX dla mikrokontrolerów rodziny PIC18.

## Architektura

```
kod.cpp (styl Arduino)                [Transpiler w C++20]
   │  preprocessor → lexer → parser → AST → emit
   ▼
kod.c + runtime/cppic_runtime.c
   │  sdcc -mpic18
   ▼
program.hex  ⭢ wgranie do PIC18
```

- **cppic** — transpilator C++ → C (własny front-end, zero zależności zewnętrznych)
- **preprocessor + linker** — `#include "..."` inline'owane rekurencyjnie,
  `#define`/`#ifdef`/`#if`/`#elif`/`#else`/`#endif`, a wiele plików sklejanych
  w jedną jednostkę translacji (wbudowany „linker")
- **runtime/** — minimalny runtime dla PIC18: emulowane SFR-y (host-sim),
  sterta `new`/`delete`, pętla `setup()`/`loop()` dla symulatora
- **backend** — SDCC (Small Device C Compiler) zamienia C na Intel HEX dla PIC18

## Build

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # 6 testów (w tym end-to-end host-sim)
```

## Szybki start

Skecz w stylu Arduino (klasy, metody, `this`, `new`/`delete`, `PORTB`/`TRISB`):

```cpp
class Led {
public:
    void init(unsigned char pin) { this->pin = pin; }
    void on()  { PORTB |=  (1 << pin); }
    void off() { PORTB &= ~(1 << pin); }
private:
    unsigned char pin;
};

Led led;

void setup() { TRISB = 0; led.init(1); }
void loop()  { led.on(); led.off(); }
```

Przetestuj bez sprzętu (host-sim, kompilowane zwykłym `cc`):

```sh
scripts/build.sh --hostsim tests/samples/blink.cpp
```

Zbuduj plik HEX dla PIC18 (wymaga `sdcc` na PATH):

```sh
scripts/build.sh --sdcc tests/samples/blink.cpp     # → blink.hex
# albo przez CMake:
cmake --build build --target hex-blink
```

Pojedyncze kroki:

```sh
build/cppic --lex   tests/samples/blink.cpp   # tokeny
build/cppic --dump  tests/samples/blink.cpp   # AST
build/cppic --emit  tests/samples/blink.cpp   # kod C (domyślny tryb)
```

## Preprocesor i wbudowany linker

Cppic ma wbudowany uproszczony preprocesor C i „linker" — wiele plików
`setup()/loop()` i osobnych modułów jest scalanych w jeden plik C.

### Wbudowane dyrektywy preprocesora

| Dyrektywa                               | Obsługa                                                     |
|-----------------------------------------|-------------------------------------------------------------|
| `#include "plik.h"` / `<header>`        | rekurencyjne inlinowanie; nieznane `<...>` przekazywane do wygenerowanego C |
| `#define NAZWA [wartosc]`               | object-like macros (podstawiane w kodzie i w `#if`)         |
| `#undef NAZWA`                          | usuwa definicję                                             |
| `#ifdef / #ifndef / #if / #elif / #else / #endif` | `defined()`, `&&`, `\|\|`, `!`, `==`, `!=`, liczby |
| `#error wiadomosc`                      | błąd (zwraca kod 1 z numerem linii i pliku)                 |
| `#pragma` / `#line` / inne              | pomijane bezbłędnie                                         |

`#define` z argumentami (`MACRO(a,b)`) nie jest obsługiwane.

### Linkowanie wielu plików

Podaj kilka plików na wywołaniu — scalają się w jedną jednostkę:

```sh
# buduj i uruchom na host-sim (skrypt)
scripts/build.sh --hostsim main.cpp led.cpp

# ręcznie
build/cppic --emit main.cpp led.cpp > program.c
cc -DCPPIC_HOST_SIM -I runtime -o program \
    program.c runtime/cppic_runtime.c
./program
```

Nagłówki `#include "..."` w każdym z plików są wyszukiwane względem
katalogu danego pliku (oraz katalogów dodanych `--I dir`).
Pliki headerowe nie muszą być wymieniane na liście argumentów.

## Typ `String` (Arduino-style)

`String` jest typem wbudowanym opartym o runtime (`CppicString` oraz
funkcje `cppic_string_*` z `cppic_runtime.c`). Obsługiwane:

- deklaracja i inicjalizacja literałem: `String s = "abc";`
- przypisanie i głęboka kopia: `s = "abc"; s = other;`
- konkatenacja w przypisaniu/inicjalizacji: `s = a + "x"; s = a + b;`
- dopisywanie: `s += "x"; s += other;`
- porównania: `==`, `!=`, `<`, `<=`, `>`, `>=` (z `String` i literałem)
- metody: `length()`, `charAt(i)`, `c_str()`, `isEmpty()`,
  `startsWith(s)`, `endsWith(s)`, `indexOf(c)` / `indexOf("s")`
- indeksowanie tylko do odczytu: `s[i]` → `cppic_string_char_at(...)`

```cpp
String msg = "Hello";

void setup() {
    msg = msg + ", world";
    msg += "!";
    if (msg.length() == 13 && msg.startsWith("Hello"))
        PORTB = 0x01;
}
```

Ograniczenia:

- `String` nie może być zwracany przez wartość — użyj parametru `String& out`
- `s[i] = ...` jest zabronione (indeksowanie tylko do odczytu)
- `+` nie można zagnieżdżać (`s = a + b + c`) — rozbij na osobne
  przypisania; `s += 'x'` / `s += 5` nie są obsługiwane (literały znakowe
  są składane do liczb przez parser)
- `String s(...)` (konstruktor) nie jest obsługiwane — użyj `String s = "..."`
- przekazanie `String` przez wartość robi płytką kopię struktury; w
  parametrach używaj referencji `String&` / `const String&`

## Wymagania

- CMake ≥ 3.16, kompilator C++20 (g++) i C (cc)
- SDCC (`apt install sdcc`) do generowania HEX

## Pakiet .deb / dystrybucja

Skrypt `packaging/build-deb.sh` buduje skompilowany program (konfiguracja
Release), pakuje go do debiana razem z runtime i wypakowuje wszystko do
katalogu `dist/`:

```sh
CMAKE=/path/to/cmake ./packaging/build-deb.sh     # CMAKE tylko gdy brak cmake na PATH
# lub: ./packaging/build-deb.sh --clean            # odśwież od zera
```

Wynik w `dist/`:

```
dist/
├── bin/cppic                        # skompilowany program (Release, stripped)
├── runtime/
│   ├── cppic_runtime.h              # nagłówki runtime dla generowanego C
│   └── cppic_runtime.c              # sterta new/delete + symulator
├── README.md
└── cppic_0.1.0-1_amd64.deb          # pakiet instalacyjny
```

Zależności pakietu (pole `Depends`/`Recommends` w `packaging/control`,
zweryfikowane przez `readelf`/`ldd`):

- `Depends`: `libc6 (>= 2.34)`, `libgcc-s1 (>= 4.2)`, `libstdc++6 (>= 12)`
  — wymagane przez binarkę (GLIBC 2.34 / GLIBCXX 3.4.29, g++ 12)
- `Recommends`: `sdcc` — niezbędny do emisji HEX (backend PIC18)

Instalacja:

```sh
sudo dpkg -i dist/cppic_0.1.0-1_amd64.deb     # sprawdzi zależności i zainstaluje
# lub ręcznie z rozwiązywaniem zależności:
sudo apt install ./dist/cppic_0.1.0-1_amd64.deb
```

Po instalacji `cppic` trafia do `/usr/bin`, runtime do
`/usr/share/cppic/runtime/`, man na `man cppic`:

```sh
cppic --emit sketch.cpp > sketch.c
sdcc -mpic18 -p18f87k22 -I/usr/share/cppic/runtime \
     sketch.c /usr/share/cppic/runtime/cppic_runtime.c
```

## Status

- [x] Lexer (tokenizer podzbioru C++)
- [x] Parser + AST
- [x] Preprocesor: `#include`, `#define`/`#undef`, `#ifdef`/`#if`/`#elif`/`#else`/`#endif`, `#error`
- [x] Wbudowany linker — wiele plików scalanych w jedną jednostkę translacji
- [x] Emisja kodu C
- [x] Name mangling, `this`/`self`, metody → funkcje, klasy → struktury
- [x] `new`/`delete` → `cppic_malloc`/`cppic_free` (sterta w runtime)
- [x] `String` (Arduino-style) na bazie runtime `CppicString`
- [x] `const`/`volatile` w emitowanym C
- [x] Runtime dla PIC18: host-sim (emulowane SFR-y, `setup()`/`loop()`)
- [~] Backend SDCC → HEX: `scripts/build.sh --sdcc` + target CMake `hex-*`
      (wymaga lokalnie zainstalowanego `sdcc`)
- [ ] Integracja z Arduino IDE