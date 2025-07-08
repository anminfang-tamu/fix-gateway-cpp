cmake_minimum_required(VERSION 3.16)
project(fix-gateway-cpp VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(Threads REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add subdirectories for each module
add_subdirectory(src/network)
add_subdirectory(src/session)
add_subdirectory(src/protocol)
add_subdirectory(src/application)
add_subdirectory(src/utils)

# Main executable
add_executable(fix-gateway
    src/main.cpp
)

# Link libraries in dependency order (bottom to top)
target_link_libraries(fix-gateway
    application
    protocol  
    session
    network
    utils
    Threads::Threads
)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install targets
install(TARGETS fix-gateway DESTINATION bin) 