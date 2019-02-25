/***
 *  MIT License
 *
 *  Copyright (c) 2019 SlickTech <support@slicktech.org>
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

namespace slick {
namespace net {

class websocket_callback {
 public:
  virtual ~websocket_callback() = default;
  virtual void on_connected() = 0;
  virtual void on_disconnected() = 0;
  virtual void on_error(const char* msg, size_t len) = 0;
  virtual void on_data(const char* data, size_t len) = 0;
};

struct client_info;

class websocket_client {
  websocket_callback *callback_;
  int16_t port_ = -1;
  std::string address_;
  std::string path_;

 public:
  websocket_client(websocket_callback *callback, std::string address, std::string path = "/");
  virtual ~websocket_client() = default;

  static void set_ssl_certificate_file_path(std::string ca_path);
  static void set_cpu_affinity(uint32_t cpu_affinity);

  bool connect();
  void stop();

  bool send(const char* msg, size_t len);

 private:
  std::shared_ptr<client_info> info_;
};

} // namespace net
} // namespace slick