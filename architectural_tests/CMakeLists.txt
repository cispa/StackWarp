cmake_minimum_required(VERSION 3.14)
project(chip-muncher C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

include_directories(sequence_generation)
include_directories(msr_flipper)
include_directories(tests)
include_directories(.)

add_subdirectory(msr_flipper)

add_compile_options("-O0;-msse4.2;-mavx2")
add_executable(single_instructions
        "tests/single_instructions.cpp"
        "common.cpp"
        "harness.s"
        "sequence_generation/instructions.cpp"
)
set(NOASLR_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/noaslr.ld)
set_target_properties(single_instructions PROPERTIES
    LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}"
    )
target_link_libraries(single_instructions PUBLIC asmjit zstd)
target_link_options(single_instructions PUBLIC "-static")

add_executable(control_flow
        "tests/control_flow.s"
        "tests/data_flow.s"
        "tests/control_flow.cpp"
        "common.cpp"
        "harness.s"
)
set_target_properties(control_flow PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(control_flow PUBLIC asmjit zstd)
target_link_options(control_flow PUBLIC "-static")

add_executable(all_instructions
        "tests/all_instructions.cpp"
        "sequence_generation/instructions.cpp"
        "common.cpp"
        "harness.s"
)
set_target_properties(all_instructions PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(all_instructions PUBLIC asmjit zstd)
target_link_options(all_instructions PUBLIC "-static")

add_executable(examine-result
        "examine_result.cpp"
        "common.cpp"
        "harness.s"
)
target_link_libraries(examine-result PUBLIC asmjit zstd capstone)
target_link_options(examine-result PUBLIC "-static")

add_executable(self-test
        "tests/self_test.cpp"
        "common.cpp"
        "harness.s"
        "tests/control_flow.s"
        "tests/data_flow.s"
)
target_link_libraries(self-test PUBLIC asmjit zstd capstone)
target_link_options(self-test PUBLIC "-static")
set_target_properties(self-test PROPERTIES
    LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}"
)

add_executable(flip "tests/flip.c")

add_executable(memory_type
        "tests/control_flow.s"
        "tests/data_flow.s"
        "tests/memory_type.cpp"
        "common.cpp"
        "harness.s"
)
#target_compile_options(memory_type PUBLIC "-fsanitize=address")
set_target_properties(memory_type PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(memory_type PUBLIC asmjit zstd)
target_link_options(memory_type PUBLIC "-static")

add_executable(intel_rdt
        "tests/control_flow.s"
        "tests/data_flow.s"
        "tests/intel_rdt.cpp"
        "common.cpp"
        "harness.s"
)
#target_compile_options(intel_rdt PUBLIC "-fsanitize=address")
set_target_properties(intel_rdt PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(intel_rdt PUBLIC asmjit zstd)
target_link_options(intel_rdt PUBLIC "-static")
