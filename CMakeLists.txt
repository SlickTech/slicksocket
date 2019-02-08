cmake_minimum_required(VERSION 3.12)
project(slicksocket)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if (NOT_CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (CMAKE_BUILD_TYPE MATCHES Release)
    add_definitions(-DNDEBUG)
endif()

option(SLICKSOCKET_WITHOUT_TESTS "Don't build tests" OFF)

# Exclude libwebsocket shared lib and test app build
set(LWS_WITHOUT_SERVER ON CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TEST_CLIENT ON CACHE BOOL "" FORCE)

add_subdirectory(third_party/libwebsockets EXCLUDE_FROM_ALL)

find_package(OpenSSL REQUIRED)
message("OpenSSL: ${OPENSSL_INCLUDE_DIR}")

include_directories(include)
include_directories("${OPENSSL_INCLUDE_DIR}")
include_directories(${CMAKE_BINARY_DIR}/third_party/libwebsockets/include)

link_directories("${OPENSSL_INCLUDE_DIR}/../lib")

if (NOT WIN32)
    add_compile_options(-flto -flto-partition=none -fno-strict-aliasing -fomit-frame-pointer -Wall)
endif()

set(SOURCES
        include/http_client.h
        src/http_client.cpp)

add_library(slicksocket_static ${SOURCES})

set_target_properties(slicksocket_static PROPERTIES LINKER_LANGUAGE CXX)

add_dependencies(slicksocket_static websockets)

if (NOT SLICKSOCKET_WITHOUT_TESTS)
    add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

if (WIN32)
    add_custom_command(TARGET slicksocket_static POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/third_party/libwebsockets/lib/websockets_static.lib"
            "${CMAKE_BINARY_DIR}/lib/")

#    add_custom_command(TARGET slicksocket POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy
#            "${CMAKE_BINARY_DIR}/third_party/libwebsockets/bin/websockets.dll"
#            "${CMAKE_BINARY_DIR}/bin/")
else()
    add_library(slicksocket SHARED ${SOURCES})
    set_target_properties(slicksocket PROPERTIES LINKER_LANGUAGE CXX)
    add_dependencies(slicksocket websockets_shared)
    target_link_libraries(slicksocket websockets_shared)

    add_custom_command(TARGET slicksocket_static POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/third_party/libwebsockets/lib/libwebsockets.a"
            "${CMAKE_BINARY_DIR}/lib/")

    add_custom_command(TARGET slicksocket POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/third_party/libwebsockets/lib/libwebsockets.so"
            "${CMAKE_BINARY_DIR}/bin/")
endif()