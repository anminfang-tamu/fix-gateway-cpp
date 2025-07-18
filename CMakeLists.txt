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
include_directories(${CMAKE_SOURCE_DIR}/include/common)
include_directories(${CMAKE_SOURCE_DIR}/include/manager)
include_directories(${CMAKE_SOURCE_DIR}/config)

# Add subdirectories for each module
add_subdirectory(src/common)
add_subdirectory(src/network)
add_subdirectory(src/session)
add_subdirectory(src/protocol)
add_subdirectory(src/application)
add_subdirectory(src/utils)
add_subdirectory(src/manager)

# Main executable
add_executable(fix-gateway
    src/main.cpp
)

# Link libraries in dependency order (bottom to top)
target_link_libraries(fix-gateway
    manager
    network
    utils
    common
    Threads::Threads
)

# Platform detection demo executable
add_executable(platform-demo
    src/platform_demo.cpp
)

target_link_libraries(platform-demo
    utils
    common
    Threads::Threads
)

# Lock-free integration demo executable (Phase 3)
add_executable(lockfree-demo
    src/lockfree_demo.cpp
)

target_link_libraries(lockfree-demo
    utils
    common
    manager
    network
    Threads::Threads
)

# Simple lock-free test executable (Phase 3)
add_executable(simple-lockfree-test
    src/simple_lockfree_test.cpp
)

target_link_libraries(simple-lockfree-test
    utils
    common
    manager
    network
    Threads::Threads
)

# Tests (optional)
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Message pool test executable (Phase 3)
add_executable(message-pool-test
    src/message_pool_test.cpp
)

target_link_libraries(message-pool-test
    utils
    common
    manager
    network
    Threads::Threads
)

# Install targets
install(TARGETS fix-gateway DESTINATION bin) 