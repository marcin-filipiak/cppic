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

# ---------------------------------------------------------------------------
# End-to-end host-sim test.
#
# Transpile samples/blink.cpp to C with the real `cppic` binary, compile the
# result together with the runtime in CPPIC_HOST_SIM mode and run it.  The
# runtime's main() returns non-zero when the sketch never wrote a non-zero
# PORTB, so a passing test proves the emitted C produces the expected
# hardware behaviour on the desktop.
# ---------------------------------------------------------------------------
set(CPPIC_HOSTSIM_DIR ${CMAKE_CURRENT_BINARY_DIR}/hostsim)
file(MAKE_DIRECTORY ${CPPIC_HOSTSIM_DIR})

set(CPPIC_BLINK_C ${CPPIC_HOSTSIM_DIR}/blink.c)
add_custom_command(
  OUTPUT ${CPPIC_BLINK_C}
  COMMAND $<TARGET_FILE:cppic>
          --emit ${CMAKE_CURRENT_SOURCE_DIR}/samples/blink.cpp
          > ${CPPIC_BLINK_C}
  DEPENDS cppic ${CMAKE_CURRENT_SOURCE_DIR}/samples/blink.cpp
  COMMENT "cppic --emit samples/blink.cpp (host-sim test)"
  VERBATIM
)

add_executable(blink_hostsim
  ${CPPIC_BLINK_C}
  ${CMAKE_SOURCE_DIR}/runtime/cppic_runtime.c
)
target_compile_definitions(blink_hostsim PRIVATE CPPIC_HOST_SIM)
target_include_directories(blink_hostsim PRIVATE ${CMAKE_SOURCE_DIR}/runtime)
add_dependencies(blink_hostsim cppic)
add_test(NAME hostsim_blink COMMAND blink_hostsim)

# ---------------------------------------------------------------------------
# SDCC backend: blink.hex for a PIC18 (only when sdcc is installed).
#   cmake --build build --target hex-blink
# ---------------------------------------------------------------------------
if(CPPIC_SDCC)
  add_custom_target(hex-blink
    COMMAND ${CPPIC_SDCC} -mpic18 -p${CPPIC_SDCC_DEVICE}
            -I${CMAKE_SOURCE_DIR}/runtime
            ${CPPIC_BLINK_C} ${CMAKE_SOURCE_DIR}/runtime/cppic_runtime.c
    DEPENDS ${CPPIC_BLINK_C}
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/blink.hex
    COMMENT "Building blink.hex with sdcc for ${CPPIC_SDCC_DEVICE}"
    VERBATIM
  )
  add_dependencies(hex-blink cppic)
endif()