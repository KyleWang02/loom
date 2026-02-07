cmake_minimum_required(VERSION 3.16)
project(loom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include paths
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Loom core library (grows with each phase)
add_library(loom_core
    src/util/error.cpp
    src/util/log.cpp
    src/util/sha256.cpp
    src/util/uuid.cpp
    src/util/glob.cpp
    src/util/swap.cpp
    src/util/target_expr.cpp
    src/util/version.cpp
    src/util/name.cpp
    src/util/source.cpp
    src/util/manifest.cpp
    src/util/config.cpp
    src/lang/lexer.cpp
)
target_include_directories(loom_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/third_party
)

# Main executable (uncomment when CLI is ready)
# add_executable(loom src/main.cpp src/cli.cpp)
# target_link_libraries(loom loom_core)

# Demo programs (for manual real-world testing)
add_executable(demo_errors demos/demo_errors.cpp)
target_link_libraries(demo_errors loom_core)

# Testing
enable_testing()

# Catch2 test runner (shared main)
add_library(catch2_main OBJECT tests/test_main.cpp)
target_include_directories(catch2_main PUBLIC ${CMAKE_SOURCE_DIR}/third_party)

# Helper function to add a test
function(loom_add_test name)
    add_executable(${name} ${ARGN} $<TARGET_OBJECTS:catch2_main>)
    target_link_libraries(${name} loom_core)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "LOOM_SOURCE_DIR=${CMAKE_SOURCE_DIR}")
endfunction()

# Phase 1 tests
loom_add_test(test_result tests/test_result.cpp)
loom_add_test(test_log tests/test_log.cpp)
loom_add_test(test_sha256 tests/test_sha256.cpp)
loom_add_test(test_uuid tests/test_uuid.cpp)
loom_add_test(test_glob tests/test_glob.cpp)
loom_add_test(test_swap tests/test_swap.cpp)

# Phase 2 tests
loom_add_test(test_target_expr tests/test_target_expr.cpp)

# Phase 3 tests
loom_add_test(test_version tests/test_version.cpp)
loom_add_test(test_name tests/test_name.cpp)
loom_add_test(test_source tests/test_source.cpp)
loom_add_test(test_manifest tests/test_manifest.cpp)
loom_add_test(test_config tests/test_config.cpp)

# Phase 4 tests
loom_add_test(test_lexer tests/test_lexer.cpp)
