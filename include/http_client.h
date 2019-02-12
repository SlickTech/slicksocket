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

#ifndef SLICK_HTTP_CLIENT_H
#define SLICK_HTTP_CLIENT_H

#include <string>
#include <unordered_map>
#include <functional>
#include <sstream>
#include <memory>

namespace slick {
namespace net {

struct http_request {
 private:
  std::string path_;
  std::string body_;
  std::string content_type_;

  /**
    * NOTE: Header name must end with ":"
    */
  std::unordered_map<std::string, std::string> headers_;

 public:
  http_request() = default;
  explicit http_request(std::string &&p) : path_(std::move(p)) {}

  void add_header(std::string key, std::string value) noexcept {
    if (key.back() != ':') {
      key.append(":");
    }
    headers_.emplace(std::move(key), std::move(value));
  }

  void add_body(std::string body, std::string content_type) noexcept {
    body_ = std::move(body);
    content_type_ = std::move(content_type);
  }

  const std::string& path() const noexcept { return path_; }
  const std::unordered_map<std::string, std::string>& headers() const noexcept { return headers_; }
  const std::string& body() const noexcept { return body_; }
  const std::string& content_type() const noexcept { return content_type_; }
};

struct http_response {
  int32_t status = 0;
  std::string content_type;
  std::string response_text;

  http_response(int32_t stat, std::string &&type, std::string &&response)
      : status(stat), content_type(std::move(type)), response_text(std::move(response)) {}
};

class http_client {
 public:
  using AsyncCallback = std::function<void(http_response)>;

  explicit http_client(std::string address);
  http_client(std::string address, int32_t cpu_affinity);
  http_client(std::string address, std::string ca_file_path, int32_t cpu_affinity = -1);
  http_client(std::string address, int16_t port, std::string ca_file_path, int32_t cpu_affinity = -1);
  virtual ~http_client();

  // Sync requests
  http_response get(std::string path);
  http_response get(std::shared_ptr<http_request> request);

  http_response post(std::shared_ptr<http_request> request);
  http_response put(std::shared_ptr<http_request> request);

  http_response del(std::string path);
  http_response del(std::shared_ptr<http_request> request);

  // Async requests
  void get(std::string path, AsyncCallback&& callback);
  void get(std::shared_ptr<http_request> request, AsyncCallback&& callback);

  void post(std::shared_ptr<http_request> request, AsyncCallback&& callback);
  void put(std::shared_ptr<http_request> request, AsyncCallback&& callback);

  void del(std::string path, AsyncCallback&& callback);
  void del(std::shared_ptr<http_request> request, AsyncCallback&& callback);

 private:
  class http_client_impl;
  std::unique_ptr<http_client_impl> impl_;
};


}
}

#endif //SLICK_HTTP_CLIENT_H
