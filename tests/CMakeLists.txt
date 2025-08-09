# Tests CMakeLists.txt - Simplified gTest setup

# MessageRouter gTest
add_executable(test_message_router
    test_message_router.cpp
)

target_link_libraries(test_message_router
    manager
    application  
    protocol
    common
    utils
    Threads::Threads
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(test_message_router PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# StreamFixParser Comprehensive gTest
add_executable(test_stream_fix_parser_comprehensive
    test_stream_fix_parser_comprehensive.cpp
)

target_link_libraries(test_stream_fix_parser_comprehensive
    protocol
    common
    utils
    Threads::Threads
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

target_include_directories(test_stream_fix_parser_comprehensive PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
)

# Simple CTest registration
add_test(NAME MessageRouterTest COMMAND test_message_router)
add_test(NAME StreamFixParserComprehensiveTest COMMAND test_stream_fix_parser_comprehensive)