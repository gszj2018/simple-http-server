cmake_minimum_required(VERSION 3.22)
project(simple_http_server)

set(CMAKE_CXX_STANDARD 20)

set(LIB_SRC
        )

set(APP_SRC
        src/main.cpp
        )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")

add_executable(simple_http_server ${LIB_SRC} ${APP_SRC})
