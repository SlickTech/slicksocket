cmake_minimum_required(VERSION 3.12)
project(slicksocket
        VERSION 1.0.0
        DESCRIPTION "A C++ http(s) and websocket(s) client library based on libwebsockets"
        LANGUAGES CXX)

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

find_package(libwebsockets CONFIG REQUIRED)
find_path(LIBWEBSOCKETS_INCLUDE_DIR libwebsockets.h)
#find_library(WEBSOCKETS libwebsockets.a)
#find_library(SSL libssl.a)
#find_library(CRYPTO libcrypto.a)
#find_library(LIBUV liblibuv.a)
#find_library(LIBZ libz.a)

#-DOPENSSL_INCLUDE_DIR=/usr/local/Cellar/openssl@1.1/1.1.1g/include -DOPENSSL_SSL_LIBRARY=/usr/local/Cellar/openssl@1.1/1.1.1g/lib/libssl.dylib -DOPENSSL_CRYPTO_LIBRARY=/usr/local/Cellar/openssl@1.1/1.1.1g/lib/libcrypto.dylib

option(SLICKSOCKET_WITHOUT_TESTS "Don't build tests" OFF)

include_directories(include ${LIBWEBSOCKETS_INCLUDE_DIR})

set(PUBLIC_HEADERS
        include/slicksocket/callback.h
        include/slicksocket/http_client.h
        include/slicksocket/websocket_client.h
        include/slicksocket/socket_client.h
        include/slicksocket/socket_server.h
)

set(SOURCES
        src/http_client.cpp
        src/websocket_client.cpp
        src/socket_client.cpp
        src/socket_server.cpp
        src/socket_service.cpp
        src/socket_service.h
)

# STATIC LIB
add_library(slicksocket ${PUBLIC_HEADERS} ${SOURCES})

#set(SLICKSOCKET_LIBS ${WEBSOCKETS} ${SSL} ${CRYPTO} ${LIBUV} ${LIBZ})
#get_directory_property(hasParent PARENT_DIRECTORY)
#if(hasParent)
#    set(SLICKSOCKET_LIBS ${SLICKSOCKET_LIBS} PARENT_SCOPE)
#endif()
#bundle_static_libraries(slicksocket_static slicksocket "slicksocket_static;${WEBSOCKETS};${SSL}")
#target_link_directories(slicksocket PRIVATE ${LIBWEBSOCKETS_LIBRARIES})
#add_dependencies(slicksocket websockets)

# SHARED LIB
add_library(slicksocket_shared SHARED ${SOURCES})
#add_dependencies(slicksocket_shared websockets_shared)
set_target_properties(slicksocket_shared PROPERTIES OUTPUT_NAME "slicksocket")
target_link_libraries(slicksocket_shared PRIVATE ${SLICKSOCKET_LIBS})

if (WIN32)
    set(SLICKSOCKET_LIBS ${SLICKSOCKET_LIBS} ws2_32)
    target_compile_definitions(slicksocket PUBLIC -Dslicksocket_STATIC)

    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    include(GenerateExportHeader)
    generate_export_header(slicksocket_shared)

#    add_custom_command(TARGET slicksocket_shared POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy
#            "${libwebsockets_BINARY_DIR}/bin/websockets.dll"
#            "${CMAKE_BINARY_DIR}/bin/")
else()
    set(SLICKSOCKET_LIBS libwebsockets::websockets ${SLICKSOCKET_LIBS} pthread)
#    add_custom_command(TARGET slicksocket POST_BUILD
#            COMMAND ${CMAKE_COMMAND} -E copy
##            "${libwebsockets_BINARY_DIR}/lib/libwebsockets.a"
#            "${CMAKE_BINARY_DIR}/lib/")

    if(APPLE)
        target_compile_options(slicksocket_shared PRIVATE -Wno-implicit-function-declaration -Wno-int-to-pointer-cast
            -flto -fno-strict-aliasing -fomit-frame-pointer -Wall)

#        add_custom_command(TARGET slicksocket_shared POST_BUILD
#                COMMAND ${CMAKE_COMMAND} -E copy
#                "${libwebsockets_BINARY_DIR}/lib/libwebsockets.dylib"
#                "${CMAKE_BINARY_DIR}/bin/")
    else()
        target_compile_options(slicksocket_shared PRIVATE -flto -flto-partition=none -fno-strict-aliasing -fomit-frame-pointer -Wall)

#        add_custom_command(TARGET slicksocket POST_BUILD
#                COMMAND ${CMAKE_COMMAND} -E copy
#                "${libwebsockets_BINARY_DIR}/lib/libwebsockets.so"
#                "${CMAKE_BINARY_DIR}/bin/")
    endif()
endif()

add_subdirectory(tests EXCLUDE_FROM_ALL)