cmake_minimum_required(VERSION 3.16)
project(loom VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include paths
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Source files (populated as phases are implemented)
# set(LOOM_SOURCES
#     src/main.cpp
# )

# add_executable(loom ${LOOM_SOURCES})

# Testing
enable_testing()
# add_subdirectory(tests)
