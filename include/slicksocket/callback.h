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

namespace slick {
namespace net {

/**
 * callback interface
 */
class client_callback_t {
 public:
  virtual ~client_callback_t() = default;

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

class socket_server_callback_t {
  virtual ~socket_server_callback_t() = default;

  virtual void on_client_connected() = 0;

  /**
   * on_disconnected invoked when connection closed
   */
  virtual void on_client_disconnected() = 0;

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

}
}