add_library(application
    fix_gateway.cpp
    message_handler.cpp
    order_book_interface.cpp
)

target_link_libraries(application protocol) 