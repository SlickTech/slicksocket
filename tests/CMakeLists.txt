cmake_minimum_required(VERSION 3.12)
project(slicksocket_tests
        VERSION 1.0.0
        LANGUAGES CXX)

configure_file(cert.pem ${CMAKE_BINARY_DIR}/bin/cert.pem COPYONLY)

include_directories(include)
include_directories(../include)
link_directories(${CMAKE_BINARY_DIR}/lib)

#add_subdirectory(..)

add_executable(slicksocket_tests http_client_tests.cpp)
set_target_properties(slicksocket_tests PROPERTIES LINKER_LANGUAGE CXX)

target_link_libraries(slicksocket_tests PRIVATE slicksocket websockets)