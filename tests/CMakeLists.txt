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

# Test for MessageManager
add_executable(test_message_manager 
    test_message_manager.cpp
)

target_link_libraries(test_message_manager 
    manager
    network
    utils
    common
    Threads::Threads
)

target_include_directories(test_message_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Test for StreamFixParser
add_executable(test_stream_fix_parser 
    test_stream_fix_parser.cpp
)

target_link_libraries(test_stream_fix_parser 
    protocol
    common
    utils
    Threads::Threads
)

target_include_directories(test_stream_fix_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Add test to CTest if available
if(BUILD_TESTING)
    add_test(NAME async_sender_test COMMAND test_async_sender)
    add_test(NAME message_manager_test COMMAND test_message_manager)
    add_test(NAME stream_fix_parser_test COMMAND test_stream_fix_parser)
endif() 