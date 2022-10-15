cmake_minimum_required(VERSION 3.22)
project(simple_http_server)

set(CMAKE_CXX_STANDARD 20)


OPTION(OPT_ENABLE_NATIVE "optimize for local architecture" ON)
OPTION(OPT_ENABLE_LTO "enable link-time optimization" ON)
option(BUILD_SHARED_LIBS "use shared libs" OFF)
option(BUILD_STATIC_LIBS "use static libs" ON)
add_subdirectory(third/llhttp)
add_subdirectory(third/base64)
add_subdirectory(dep/simple-net-lib)

include_directories(third/llhttp/include)
include_directories(third/base64/include)
include_directories(third/jsoncpp/json)
include_directories(dep/simple-net-lib/include)

set(LIB_SRC
        src/http_server.cpp src/http_server.hpp
        )

set(APP_SRC
        third/jsoncpp/jsoncpp.cpp
        src/main.cpp
        )

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
if (OPT_ENABLE_NATIVE)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
endif ()
if (OPT_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

add_executable(simple_http_server ${LIB_SRC} ${APP_SRC})

target_link_libraries(simple_http_server llhttp_static simple_net_lib base64)

message("===simple-http-server===")
message("DEFAULT FLAGS: ${CMAKE_CXX_FLAGS}")
message("DEBUG   FLAGS: ${CMAKE_CXX_FLAGS_DEBUG}")
message("RELEASE FLAGS: ${CMAKE_CXX_FLAGS_RELEASE}")
message("LTO: ${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
