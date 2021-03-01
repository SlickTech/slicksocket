/***
 *  MIT License
 *
 *  Copyright (c) 2021 SlickTech <support@slicktech.org>
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#pragma once

#include <thread>
#include <unordered_map>

//struct lws;
//enum lws_callback_reasons;

namespace slick {
namespace net {

class socket_server_callback_t;

class socket_server {
  socket_server_callback_t* callback_;
  std::atomic_bool run_{true};

 public:
  socket_server(socket_server_callback_t* callback) noexcept : callback_(callback) {}
  virtual ~socket_server() noexcept = default;

  void serve(int32_t port, int32_t cpu_affinity = -1);

  void stop() noexcept {
    run_.store(false, std::memory_order_release);
  }

  bool send(void* client_handle, const char* message, size_t length);

};

}
}