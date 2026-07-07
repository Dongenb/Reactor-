cmake_minimum_required(VERSION 3.16)
project(reactor_chat_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(chat_common
    src/common/json.cpp
    src/common/protocol.cpp
    src/common/socket_utils.cpp
)
target_include_directories(chat_common PUBLIC src)
target_compile_options(chat_common PRIVATE -Wall -Wextra -Wpedantic)

add_executable(chat_server
    src/server/main.cpp
    src/server/chat_server.cpp
)
target_link_libraries(chat_server PRIVATE chat_common)
target_compile_options(chat_server PRIVATE -Wall -Wextra -Wpedantic)

add_executable(chat_client
    src/client/main.cpp
)
target_link_libraries(chat_client PRIVATE chat_common pthread)
target_compile_options(chat_client PRIVATE -Wall -Wextra -Wpedantic)
