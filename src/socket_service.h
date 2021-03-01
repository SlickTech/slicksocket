/***
 *  MIT License
 *
 *  Copyright (c) 2018-2019 SlickTech <support@slicktech.org>
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

#include <libwebsockets.h>
#include <atomic>
#include <sstream>
#include <functional>
#include "ring_buffer.h"

namespace slick {
namespace net {

class socket_service;

class client_callback_t;
struct http_request;
struct http_response;

enum class request_type {
  http,
  ws,
  socket,
};

struct http_info {
  uint32_t status = 0;
  std::shared_ptr<http_request> request;
  std::function<void(http_response)> callback = nullptr;
  std::stringstream response;
  char content_type[512];
  char buffer[2048 + LWS_PRE];
  char *px = buffer + LWS_PRE;
  int buffer_len = sizeof(buffer) - LWS_PRE;
  std::atomic_bool completed {false};
};

struct socket_info {
  client_callback_t *callback = nullptr;
  ring_string_buffer sending_buffer {8192};
  std::atomic_bool shutdown {false};
  bool disconnecte_callback_invoked {false};

  socket_info() = default;
  socket_info(client_callback_t* cb) : callback(cb) {}
};

struct request_info {
  lws *wsi = nullptr;
  request_type type;
  std::string path;
  lws_client_connect_info cci;
  struct http_info http_info;
  struct socket_info socket_info;
};

//struct http_request_info : public request_info {
//  uint32_t status = 0;
//  std::shared_ptr<http_request> request;
//  std::function<void(http_response)> callback = nullptr;
//  std::stringstream response;
//  char content_type[512];
//  char buffer[2048 + LWS_PRE];
//  char *px = buffer + LWS_PRE;
//  int buffer_len = sizeof(buffer) - LWS_PRE;
//  std::atomic_bool completed {false};
//};

//struct ws_request_info : public request_info {
//  client_callback_t *callback = nullptr;
//  ring_string_buffer sending_buffer {8192};
//  std::atomic_bool shutdown {false};
//  bool disconnecte_callback_invoked {false};
//
//  ws_request_info() {
//    type = request_type::ws;
//  }
//};

//struct socket_request_info : public request_info {
//  client_callback_t *callback = nullptr;
//  ring_string_buffer sending_buffer {8192};
//  std::atomic_bool shutdown {false};
//  bool disconnecte_callback_invoked {false};
//
//  socket_request_info() {
//    type = request_type::socket;
//  }
//};

class socket_service {

  std::thread thread_;
  lws_context *context_ = nullptr;
  uint64_t cursor_ = 0;
  std::atomic_bool run_{true};
  object_pool<request_info> request_pool_;
//  object_pool<http_request_info> http_request_pool_;
//  object_pool<ws_request_info> ws_request_pool_;
//  object_pool<socket_request_info> socket_request_pool_;
  ring_buffer<request_info*> request_queue_;
  std::string ca_file_path_;
  bool is_global_ = false;

 public:
  static socket_service* global(const std::string& ca_file_path, int32_t cpu_affinity) noexcept;

  socket_service(std::string ca_file_path, int32_t cpu_affinity = -1, bool is_global = false);

  ~socket_service() {
    run_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }

    if (context_) {
      lws_context_destroy(context_);
      context_ = nullptr;
    }
  }

  bool is_global() const noexcept { return is_global_; };

  request_info* get_request_info(request_type type) {
    if (!context_) {
      return nullptr;
    }
    auto obj = request_pool_.get_obj();
    if (obj) {
      obj->type = type;
    }
    return obj;
  }

//  http_request_info* get_http_request() {
//    if (!context_) {
//      return nullptr;
//    }
//    return http_request_pool_.get_obj();
//  }
//
//  ws_request_info* get_ws_request() {
//    if (!context_) {
//      return nullptr;
//    }
//    return ws_request_pool_.get_obj();
//  }
//
//  socket_request_info* get_socket_request() {
//    if (!context_) {
//      return nullptr;
//    }
//    return socket_request_pool_.get_obj();
//  }

  void release_request(request_info* req) {
    request_pool_.release_obj(req);
  }

//  void release_request(http_request_info* req) {
//    http_request_pool_.release_obj(req);
//  }
//
//  void release_request(ws_request_info* req) {
//    ws_request_pool_.release_obj(req);
//  }
//
//  void release_request(socket_request_info* req) {
//    socket_request_pool_.release_obj(req);
//  }

  void request(request_info* req) {
    auto slot = request_queue_.reserve();
    slot[0] = req;
    slot.publish();
  }

 private:
  void serve(int32_t cpu_affinity);
};

}
}