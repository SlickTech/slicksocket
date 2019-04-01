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
#include <memory>

namespace slick {
namespace net {

/**
 * websocket callback
 */
class websocket_callback {
 public:
  virtual ~websocket_callback() = default;

  /**
   * on_connected invoked when connection established
   */
  virtual void on_connected() = 0;

  /**
   * on_disconnected invoked when connection closed
   */
  virtual void on_disconnected() = 0;

  /**
   * on_error invoked when error occurred
   *
   * @param msg     Error message. Might be empty.
   * @param len     Error message length
   */
  virtual void on_error(const char* msg, size_t len) = 0;

  /**
   * on_data invoked when data received
   *
   * @param data        Data string
   * @param len         Current data length
   * @param remaining   How many data remains
   */
  virtual void on_data(const char* data, size_t len, size_t remaining) = 0;
};

struct ws_request_info;
class socket_service;

class websocket_client {
  websocket_callback *callback_;
  ws_request_info* request_ = nullptr;
  socket_service* service_ = nullptr;
  std::string address_;
  std::string origin_;
  std::string path_;
  int16_t port_ = -1;

 public:
  websocket_client(websocket_callback *callback,
                   std::string address,
                   std::string origin = "",
                   std::string path = "/",
                   std::string ca_file_path = "",
                   int32_t cpu_affinity = -1,
                   bool use_global_service = false);

  virtual ~websocket_client();

  /**
   * Connect to WebSocket server
   * @return False if error occurred. Otherwise True.
   */
  bool connect();

  /**
   * Stop WebSocket Client
   */
  void stop();

  /**
   * Send message to WebSocket server
   * @param msg     The message to send
   * @param len     The length of the message
   * @return        True on success. Otherwise False.
   */
  bool send(const char* msg, size_t len);

 private:
//  std::shared_ptr<ws_request_info> info_;
};

} // namespace net
} // namespace slick