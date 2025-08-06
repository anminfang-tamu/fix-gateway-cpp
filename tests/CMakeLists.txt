# Tests CMakeLists.txt

# Find required packages
find_package(Threads REQUIRED)

# Test for AsyncSender
add_executable(test_async_sender 
    test_async_sender.cpp
)

target_link_libraries(test_async_sender 
    common
    utils  
    network
    Threads::Threads
)

target_include_directories(test_async_sender PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Test for StreamFixParser 2-Stage Architecture
add_executable(test_stream_fix_parser_2stage
    test_stream_fix_parser_2stage.cpp
)

target_link_libraries(test_stream_fix_parser_2stage
    protocol
    common
    utils
    Threads::Threads
)

target_include_directories(test_stream_fix_parser_2stage PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)


add_executable(test_stream_fix_parser_improved
    test_stream_fix_parser_improved.cpp
)

target_link_libraries(test_stream_fix_parser_improved
    protocol
    common
    utils
    Threads::Threads
)

target_include_directories(test_stream_fix_parser_improved PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Claude's Custom Performance Test
add_executable(claude_performance_test
    claude_performance_test.cpp
)

target_link_libraries(claude_performance_test
    protocol
    common
    utils
    Threads::Threads
)

target_include_directories(claude_performance_test PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Add test to CTest if available
# Removed BUILD_TESTING condition to ensure tests are always built
add_test(NAME async_sender_test COMMAND test_async_sender)
add_test(NAME stream_fix_parser_2stage_test COMMAND test_stream_fix_parser_2stage)
add_test(NAME stream_fix_parser_improved_test COMMAND test_stream_fix_parser_improved)