add_library(session
    fix_session_manager.cpp
)

target_link_libraries(session network) 