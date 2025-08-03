# Manager module CMakeLists.txt

# Create manager library
add_library(manager
    inbound_message_manager.cpp
    business_logic_manager.cpp
    fix_session_manager.cpp
    async_sender_manager.cpp
    message_router.cpp
)

# Link dependencies
target_link_libraries(manager
    network    # For AsyncSender and TcpConnection
    utils      # For PriorityQueue
    common     # For Message types
    Threads::Threads  # For std::thread and threading
)

# Include directories
target_include_directories(manager PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/manager
    ${CMAKE_SOURCE_DIR}/include/network
    ${CMAKE_SOURCE_DIR}/include/utils
    ${CMAKE_SOURCE_DIR}/include/common
    ${CMAKE_SOURCE_DIR}/config
)

# Compiler options for optimization
target_compile_options(manager PRIVATE
    $<$<CONFIG:Debug>:-g -O0 -Wall -Wextra>
    $<$<CONFIG:Release>:-O3 -DNDEBUG>
) 