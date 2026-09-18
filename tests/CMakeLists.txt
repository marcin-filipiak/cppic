add_executable(lexer_test lexer_test.cpp)
target_link_libraries(lexer_test PRIVATE cppic_core)
add_test(NAME lexer_test COMMAND lexer_test)

add_executable(parser_test parser_test.cpp)
target_link_libraries(parser_test PRIVATE cppic_core)
add_test(NAME parser_test COMMAND parser_test)