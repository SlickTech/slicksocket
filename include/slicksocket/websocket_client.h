/***
 *  MIT License
 *
 *  Copyright (c) 2019-2021 SlickTech <support@slicktech.org>
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
#include <memory>

namespace slick {
namespace net {

struct request_info;
class socket_service;
class client_callback_t;

class websocket_client {
  client_callback_t *callback_;
  request_info* request_ = nullptr;
  socket_service* service_ = nullptr;
  std::string url_;
  std::string address_;
  std::string origin_;
  std::string path_;
  int16_t port_ = -1;

 public:
  websocket_client(client_callback_t *callback,
                   std::string url_,
                   std::string origin = "",
                   std::string ca_file_path = "",
                   int32_t cpu_affinity = -1,
                   bool use_global_service = false);

  virtual ~websocket_client() noexcept;
  
  const std::string& url() const noexcept { return url_; }

  /**
   * Connect to WebSocket server
   * @return False if error occurred. Otherwise True.
   */
  bool connect() noexcept;

  /**
   * Stop WebSocket Client
   */
  void stop() noexcept;

  /**
   * Send message to WebSocket server
   * @param msg     The message to send
   * @param len     The length of the message
   * @return        True on success. Otherwise False.
   */
  bool send(const char* msg, size_t len) noexcept;
};

} // namespace net
} // namespace slick