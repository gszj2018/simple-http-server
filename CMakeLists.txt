cmake_minimum_required(VERSION 3.22)
project(simple_http_server)

set(CMAKE_CXX_STANDARD 20)


option(BUILD_SHARED_LIBS "" OFF)
option(BUILD_STATIC_LIBS "" ON)
add_subdirectory(third/llhttp)
add_subdirectory(dep/simple-net-lib)

include_directories(third/llhttp/include)
include_directories(dep/simple-net-lib/include)

set(LIB_SRC
        src/http_server.cpp src/http_server.hpp
        )

set(APP_SRC
        src/main.cpp
        )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

add_executable(simple_http_server ${LIB_SRC} ${APP_SRC})

target_link_libraries(simple_http_server llhttp_static simple_net_lib)
