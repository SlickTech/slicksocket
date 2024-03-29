# SlickSocket
A lightweight cross-platform modern C++ HTTP/HTTPS and WebSocket client library.

## Why another network library?
There are few C++ HTTP libraries out there, but most of them are lack of ssl support. Some of 
 the libraries I have used before are good, such as cpprestsdk, Beasts, libwebsockets etc., but 
 either they have a big footprint, or not easy to setup and use, especially on Windows. 
 
Slicksocket is build on top of [libwebsockets](https://libwebsockets.org). Libwebsockets is a 
great C library. It not only support both client and server, but also support most of protocols
including latest HTTP2 and WSS2. Although Libwebsockets provided 50 minimal examples, but it
is missing a good tutorial and user guide. For a new user, it will take some time to learn and 
figure out how to make things work. The goal of Slicksocket is to remove the hussle away by providing
a lightweight, easy to use modern C++ library.

## What make SlickSocket stands out?
* Use latest C++ 11/14 standard.
* Cross-platform. Easy to compile and use on Windows, Linux and Mac OS X.
* Support HTTP/HTTPS and WebSocket.
* Only depends on libwebsockets (and Of cause OpenSSL for HTTPS).
* Lock-free design, have very good performance.
* Support both Synchronous and Asynchronous HTTP/HTTPS request.

## Getting Started
[Build Slicksockt](https://github.com/SlickTech/slicksocket#build-slicksocket)<br />
[Tutorial](https://github.com/SlickTech/slicksocket#tutorial)<br />
[Caveats](https://github.com/SlickTech/slicksocket#caveats)

## Build SlickSocket
### 1. Install vcpkg
```
git clone https://github.com/microsoft/vcpkg

On Windows run:
.\vcpkg\bootstrap-vcpkg.bat

On Mac or Linux run:
./vcpkg/boostrap-vcpkg.sh
```
For more information about vcpkg, please visit https://github.com/microsoft/vcpkg

### 2. Install Libwebsockets
```
./vcpkg/vcpkg install libwebsockets
```

### 3. Get the project and dependencies

```
git clone https://github.com/SlickTech/slicksocket.git
cd slicksocket
```

### 4. Build the project
```
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[path to vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build . --target slicksocket
```
**NOTE:** By default, it makes a release build. To make debug build, use: <br />
``cmake .. -DCMAKE_BUILD_TYPE=Debug``<br />

**NOTE:** If cmake failed because it can't find OpenSSL, it might happen if you are build against
a non-distro OpenSSL or installed OpenSSL using brew on Mac, please use follow command:
```
cmake .. -DOPENSSL_ROOT_DIR=<OpenSSL_install_path> \
    -DCMAKE_INCLUDE_DIRECTORIES_PROJECT_BEFORE=<OpenSSL_install_path>
```

**NOTE:** By default, above command makes a shared library. To make a static library, run:<br />
``cmake --build . --target slicksocket_static`` <br />
``cmake --build .`` will create both shared and static library.


## Tutorial
Set up project from CMake:
```c++
cmake_minimum_required(VERSION 3.7)
project(sample)

include_directory(<slicksocket_include_path>/include)
add_executable(sample main.cpp)

if (WIN32)
    target_link_libraries(sample PRIVATE slicksocket websockets openssl libssl libcrypto ws2_32)
else()
    target_link_libraries(sample PRIVATE slicksocket websockets_shared ssl crypto pthread)
endif()
```
**NOTE:** Substitute '<slicksocket_include_path>' to slicksocket include path on your machine.

### Http Client
Using http_client in your code:<br />
```c++
// sampel.cpp
#include <slicksocket/http_client.h>
#include <iostream>

using namespace slick::net;

int main(int argc, char* argv[]) {
    http_client client("https://api.pro.coinbase.com");
    
    // GET request
    
    // Synchronous request
    auto rsp = client.request("GET", "/products");
    if (rsp.status != 200) {
        std::cerr << "Failed to get products";
        return 1;
    }
    
    std::cout << rsp.respnse_text << std:endl;
    
    // Asynchronous request
    std::atomic_bool completed {false};
    client->request("GET", "/products", [](http_respose rsp) {
        if (rsp.status != 200) {
            std::cerr << "Failed to get products";
            return;
        }
        std::cout << rsp.respnse_text << std:endl;
        completed.store(true, std::memory_order_release);
    });
    
    // POST request
    http_client client2("https://postman-echo.com");
    auto request = std::make_shared<http_request>("/post");
    request->add_header("Authorization", "testxxxxxxxxxxxxxxxxxxxx");
    request->add_body("{\"name\":\"Tom\"}", "application/json");
    response = client2.request("POST", std::move(request));
    std::cout << "POST response: "
              << response.status << " " << response.response_text << std::endl;
              
    // DELETE request - delete depends on serever implementation
    
    // delete through request url
    request = std::make_shared<http_request>("/12345");
    response = client2.request("DELETE", std::move(request));
    std::cout << response.response_text << std::endl;
    
    // delete through request body
    request = std::make_shared<http_request>("/");
    request->add_body("{\"id\": 12345}", "application/json");
    response = client2.request("DELETE", std::move(request));
    std::cout << response.response_text << std::endl;
    
    while (!completed.load(std::memory_order_relaxed));
    return 0;
}
```

## Caveats
* Need multipart form-data support.
