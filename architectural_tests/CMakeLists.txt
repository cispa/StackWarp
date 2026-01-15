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

add_executable(sequences
        "tests/control_flow.s"
        "tests/data_flow.s"
        "tests/sequences.cpp"
        "common.cpp"
        "harness.s"
)
set_target_properties(sequences PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(sequences PUBLIC asmjit zstd)
target_link_options(sequences PUBLIC "-static")

add_executable(all_instructions
        "tests/all_instructions.cpp"
        "sequence_generation/instructions.cpp"
        "common.cpp"
        "harness.s"
)
set_target_properties(all_instructions PROPERTIES LINK_FLAGS "-T ${NOASLR_LINKER_SCRIPT}")
target_link_libraries(all_instructions PUBLIC asmjit zstd)
target_link_options(all_instructions PUBLIC "-static")

