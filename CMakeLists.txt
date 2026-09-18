cmake_minimum_required(VERSION 3.16)
project(cppic VERSION 0.1.0 LANGUAGES CXX)

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
)
target_include_directories(cppic_core PUBLIC src)

add_executable(cppic src/driver/main.cpp)
target_link_libraries(cppic PRIVATE cppic_core)

enable_testing()
add_subdirectory(tests)