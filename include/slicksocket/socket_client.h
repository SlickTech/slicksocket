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

#include <cstdint>
#include <string>
#include <functional>

namespace slick {
namespace net {

class client_callback_t;
class socket_service;
struct request_info;

class socket_client {
  client_callback_t* callback_;
  socket_service* service_;
  request_info* request_ = nullptr;
  uint32_t port_;
  std::string address_;

 public:
  socket_client(client_callback_t *callback,
                std::string address,
                uint32_t port,
                int32_t cpu_affinity = -1,
                bool use_global_thread = false);
  virtual ~socket_client();

  /**
   * Connect to WebSocket server
   * @return False if error occurred. Otherwise True.
   */
  bool connect();

  /**
  * Stop socket_client
  */
  void stop() noexcept;

  /**
   * Send message to WebSocket server
   * @param msg     The message to send
   * @param len     The length of the message
   * @return        True on success. Otherwise False.
   */
  bool send(char* msg, size_t len);
};

}
}