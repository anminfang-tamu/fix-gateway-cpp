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
    demos/platform_demo.cpp
)

target_link_libraries(platform-demo
    utils
    common
    Threads::Threads
)

# Lock-free integration demo executable (Phase 3)
add_executable(lockfree-demo
    demos/lockfree_demo.cpp
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
    demos/simple_lockfree_test.cpp
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
    demos/message_pool_test.cpp
)

target_link_libraries(message-pool-test
    utils
    common
    manager
    network
    Threads::Threads
)

# FIX Engine demonstration executable 
add_executable(fix-demo
    demos/fix_demo.cpp
)

target_link_libraries(fix-demo
    protocol
    utils
    common
    Threads::Threads
)

# Memory performance benchmark executable
add_executable(memory-perf-test
    demos/memory_performance_test.cpp
)

target_link_libraries(memory-perf-test
    protocol
    utils
    common
    Threads::Threads
)

# Quick performance comparison demo
add_executable(quick-perf-demo
    demos/quick_perf_demo.cpp
)

target_link_libraries(quick-perf-demo
    protocol
    utils
    common
    Threads::Threads
)

# Message pool integration demo
add_executable(message-integration-demo
    demos/message_integration_demo.cpp
)

target_link_libraries(message-integration-demo
    protocol
    utils
    common
    Threads::Threads
)

# Templated message pool demo
add_executable(templated-pool-demo
    demos/templated_pool_demo.cpp
)

target_link_libraries(templated-pool-demo
    protocol
    utils
    common
    Threads::Threads
)

# ====================
# DEMO EXECUTABLES  
# ====================

# Existing pool templated demo
add_executable(existing-pool-templated-demo
    demos/existing_pool_templated_demo.cpp
)

target_link_libraries(existing-pool-templated-demo
    protocol
    utils
    common
    Threads::Threads
)

# Message pool validation test
add_executable(message-pool-validation-test
    demos/message_pool_validation_test.cpp
)

target_link_libraries(message-pool-validation-test
    utils
    common
    Threads::Threads
)

# Raw pointer performance demo
add_executable(raw-pointer-perf-demo
    demos/raw_pointer_perf_demo.cpp
)

target_link_libraries(raw-pointer-perf-demo
    protocol
    utils
    common
    Threads::Threads
)

# Raw pointer API demo
add_executable(raw-pointer-api-demo
    demos/raw_pointer_api_demo.cpp
)

target_link_libraries(raw-pointer-api-demo
    protocol
    utils
    common
    Threads::Threads
)

# Install targets
install(TARGETS fix-gateway DESTINATION bin) 