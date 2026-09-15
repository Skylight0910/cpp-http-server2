cmake_minimum_required(VERSION 3.16)

project(cpp_http_server2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(http_server_core STATIC
    Buffer.cpp
    Channel.cpp
    EventLoop.cpp
    EventLoopThread.cpp
    HttpRequest.cpp
    HttpResponse.cpp
)

target_include_directories(http_server_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(http_server_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)
target_link_libraries(http_server_core PUBLIC Threads::Threads)

add_executable(http_server server.cpp)
target_link_libraries(http_server PRIVATE http_server_core)

add_executable(core_tests tests/test_core.cpp)
target_link_libraries(core_tests PRIVATE http_server_core)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
