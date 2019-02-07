cmake_minimum_required(VERSION 2.8.9)
project(tests)

configure_file(cert.pem ${CMAKE_BINARY_DIR}/bin/cert.pem COPYONLY)

include_directories(include)
include_directories(../include)
link_directories(${CMAKE_BINARY_DIR}/third_party/libwebsockets/lib)

add_executable(tests http_client_tests.cpp)
add_dependencies(tests slicksocket)
set_target_properties(tests PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(tests slicksocket)
