cmake_minimum_required(VERSION 3.16)
project(cppic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra)

add_library(cppic_core
  src/lexer/token.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/ast/dump.cpp
  src/transpile/transpiler.cpp
  src/preprocess/preprocessor.cpp
)
target_include_directories(cppic_core PUBLIC src)

add_executable(cppic src/driver/main.cpp)
target_link_libraries(cppic PRIVATE cppic_core)

# ---------------------------------------------------------------------------
# SDCC backend: build a PIC18 Intel HEX from a sketch.  Only wired when an
# sdcc executable is found; individual sketches are targeted via
# `cmake --build build --target hex-<sketch>`.
# ---------------------------------------------------------------------------
find_program(CPPIC_SDCC sdcc)
set(CPPIC_SDCC_DEVICE "p18f87k22" CACHE STRING "PIC18 part passed to sdcc (-mpic18 --pIC18... )")

enable_testing()
add_subdirectory(tests)