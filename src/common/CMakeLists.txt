# Common library - shared data structures and utilities
add_library(common STATIC
    message.cpp
)

# Include directories
target_include_directories(common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/common
        ${CMAKE_SOURCE_DIR}/config
)

# Required for std::chrono and threading
target_link_libraries(common
    PUBLIC
        Threads::Threads
)

# Set properties
set_target_properties(common PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
) 