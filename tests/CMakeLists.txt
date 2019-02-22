cmake_minimum_required(VERSION 2.8.9)
project(tests)

configure_file(cert.pem ${CMAKE_BINARY_DIR}/bin/cert.pem COPYONLY)

include_directories(include)
include_directories(../include)
link_directories(${CMAKE_BINARY_DIR}/lib)

add_executable(tests http_client_tests.cpp)
set_target_properties(tests PROPERTIES LINKER_LANGUAGE CXX)

if (WIN32)
    target_link_libraries(tests slicksocket openssl libssl libcrypto ws2_32)
else()
    target_link_libraries(tests slicksocket ssl crypto pthread)
endif()