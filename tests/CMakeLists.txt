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

# Add test to CTest if available
if(BUILD_TESTING)
    add_test(NAME async_sender_test COMMAND test_async_sender)
endif() 