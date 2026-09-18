add_executable(lexer_test lexer_test.cpp)
target_link_libraries(lexer_test PRIVATE cppic_core)
add_test(NAME lexer_test COMMAND lexer_test)

add_executable(parser_test parser_test.cpp)
target_link_libraries(parser_test PRIVATE cppic_core)
add_test(NAME parser_test COMMAND parser_test)

add_executable(transpiler_test transpiler_test.cpp)
target_compile_definitions(transpiler_test PRIVATE
  CPPIC_SAMPLE_BLINK=CMAKE_CURRENT_SOURCE_DIR/samples/blink.cpp)
target_link_libraries(transpiler_test PRIVATE cppic_core)
add_test(NAME transpiler_test COMMAND transpiler_test)
