# cppic — transpiler C++ → C dla mikrokontrolerów PIC18

Kompilator języka C++ (podzbiór używany w skeczach Arduino, obiektowy C++)
do pliku HEX dla mikrokontrolerów rodziny PIC18.

## Architektura

```
kod.cpp (styl Arduino)                [Transpiler w C++20]
   │  lexer → parser → AST → sema → emit
   ▼
kod.c + runtime.c
   │  sdcc -mpic18
   ▼
program.hex  ⭢ wgranie do PIC18
```

- **cppic** — transpilator C++ → C (własny front-end, brak zależności zewnętrznych)
- **runtime/** — minimalny runtime dla PIC18 (`new`/`delete`, `String`, heap)
- **backend** — SDCC (Small Device C Compiler) zamienia C na Intel HEX dla PIC18

## Build

```sh
cmake -B build -G Ninja
cmake --build build
```

## Wymagania

- CMake ≥ 3.16, kompilator C++20 (g++)
- SDCC (`apt install sdcc`) do generowania HEX

## Status

- [x] Lexer (tokenizer podzbioru C++)
- [ ] Parser + AST
- [ ] Analiza semantyczna + name mangling
- [ ] Emisja kodu C
- [ ] Runtime dla PIC18
- [ ] Backend SDCC → HEX (end-to-end)
- [ ] Integracja z Arduino IDE