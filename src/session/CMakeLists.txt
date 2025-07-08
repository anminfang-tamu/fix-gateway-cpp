add_library(session
    fix_session.cpp
    session_manager.cpp
)

target_link_libraries(session network) 