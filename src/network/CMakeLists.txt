add_library(network
    tcp_connection.cpp
    tcp_server.cpp
    async_sender.cpp
)

# Link dependencies
target_link_libraries(network 
    utils
    common
    Threads::Threads
) 