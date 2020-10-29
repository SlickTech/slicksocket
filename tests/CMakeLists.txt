cmake_minimum_required(VERSION 3.12)
project(tests
        VERSION 1.0.0
        LANGUAGES CXX)

configure_file(cert.pem ${CMAKE_BINARY_DIR}/bin/cert.pem COPYONLY)

include_directories(include)
include_directories(../include)
link_directories(${CMAKE_BINARY_DIR}/lib)

add_executable(tests http_client_tests.cpp)
set_target_properties(tests PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(tests PRIVATE ${SLICKSOCKET_LINK_LIBRARIES})