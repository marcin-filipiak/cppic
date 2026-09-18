# cppic — transpiler C++ → C dla mikrokontrolerów PIC18

Kompilator języka C++ (podzbiór używany w skeczach Arduino, obiektowy C++)
do pliku HEX dla mikrokontrolerów rodziny PIC18.

## Architektura

```
kod.cpp (styl Arduino)                [Transpiler w C++20]
   │  lexer → parser → AST → emit
   ▼
kod.c + runtime/cppic_runtime.c
   │  sdcc -mpic18
   ▼
program.hex  ⭢ wgranie do PIC18
```

- **cppic** — transpilator C++ → C (własny front-end, zero zależności zewnętrznych)
- **runtime/** — minimalny runtime dla PIC18: emulowane SFR-y (host-sim),
  sterta `new`/`delete`, pętla `setup()`/`loop()` dla symulatora
- **backend** — SDCC (Small Device C Compiler) zamienia C na Intel HEX dla PIC18

## Build

```sh
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure   # 4 testy (w tym end-to-end host-sim)
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

## Wymagania

- CMake ≥ 3.16, kompilator C++20 (g++) i C (cc)
- SDCC (`apt install sdcc`) do generowania HEX

## Status

- [x] Lexer (tokenizer podzbioru C++)
- [x] Parser + AST
- [x] Emisja kodu C
- [x] Name mangling, `this`/`self`, metody → funkcje, klasy → struktury
- [x] `new`/`delete` → `cppic_malloc`/`cppic_free` (sterta w runtime)
- [x] `const`/`volatile` w emitowanym C
- [x] Runtime dla PIC18: host-sim (emulowane SFR-y, `setup()`/`loop()`)
- [~] Backend SDCC → HEX: `scripts/build.sh --sdcc` + target CMake `hex-*`
      (wymaga lokalnie zainstalowanego `sdcc`)
- [ ] Integracja z Arduino IDE
- [ ] `String` i przyjazne typy (planowane na bazie sterty)