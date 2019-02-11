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

#include "http_client.h"
#include "ring_buffer.h"
#include <libwebsockets.h>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include <thread>

using namespace slick::core;
using namespace slick::net;

namespace {
struct request_info {
  uint32_t status = 0;
  const char* method;
  std::shared_ptr<http_request> request;
  std::function<void(http_response)> callback = nullptr;
  std::stringstream response;
  lws *wsi;
  char buffer[2048 + LWS_PRE];
  char *px = buffer + LWS_PRE;
  int buffer_len = sizeof(buffer) - LWS_PRE;
  std::atomic_bool completed {false};
};

int http_callback(struct lws *wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  auto req = (request_info*)lws_wsi_user(wsi);

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      req->status = 502;
      req->wsi = nullptr;
      if (in && len) {
        req->response << "Error occurred. " << std::string((char*)in, len);
        printf("OnError: %.*s\n", (int)len, (char*)in);
      } else {
        req->response << "Error occurred. ";
        printf("Error occurred. %s", req->request->path().c_str());
      }
      req->completed.store(true, std::memory_order_release);
      break;

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      unsigned char **p = (unsigned char **) in, *end = (*p) + len;
      if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_USER_AGENT, (unsigned char*)"libwebsocket", 12, p, end)) {
        req->status = 502;
        req->wsi = nullptr;
        req->response << "Failed to add User-Agent header";
        req->completed.store(true, std::memory_order_release);
        return -1;
      }

      if (!req->request->headers().empty()) {
        for (auto &kvp : req->request->headers()) {
          if (lws_add_http_header_by_name(wsi,
                                          (unsigned char *) kvp.first.c_str(),
                                          (unsigned char *) kvp.second.c_str(),
                                          (int) kvp.second.size(),
                                          p,
                                          end)) {
            req->status = 502;
            req->wsi = nullptr;
            req->response << "Failed to add header " << kvp.first << kvp.second;
            req->completed.store(true, std::memory_order_release);
            return -1;
          }
        }
      }

      const auto& content_type = req->request->content_type();
	  if (!content_type.empty()) {
        if (lws_add_http_header_by_token(wsi,
                                         WSI_TOKEN_HTTP_CONTENT_TYPE,
                                         (unsigned char *) content_type.c_str(),
                                         (int) content_type.size(),
                                         p,
                                         end)) {
          req->status = 502;
          req->wsi = nullptr;
          req->response << "Failed to add header Content-Type:" << content_type;
          req->completed.store(true, std::memory_order_release);
          return -1;
        }
	  }

	  const auto& body = req->request->body();
	  if (!body.empty()) {
	    std::string sz = std::to_string(body.size());
        if (lws_add_http_header_by_token(wsi,
                                         WSI_TOKEN_HTTP_CONTENT_LENGTH,
                                         (unsigned char *) sz.c_str(),
                                         (int) sz.size(),
                                         p,
                                         end)) {
          req->status = 502;
          req->wsi = nullptr;
          req->response << "Failed to add header Content-Length:" << sz;
          req->completed.store(true, std::memory_order_release);
          return -1;
        }
	    lws_client_http_body_pending(wsi, 1);
	    lws_callback_on_writable(wsi);
	  }
      break;
    }

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      auto p = (unsigned char*)&req->buffer[LWS_PRE];
      const auto& body = req->request->body();
      auto sz = body.size() + 1;

      if (sz > sizeof(req->buffer) - LWS_PRE) {
        req->status = 502;
        req->wsi = nullptr;
        req->response << "body exceeds buffer size";
        req->completed.store(true, std::memory_order_release);
        return -1;
      }

      auto n = lws_snprintf((char*)p, sz, "%s", body.c_str());
      lws_client_http_body_pending(wsi, 0);

      if (lws_write(wsi, p, n, LWS_WRITE_HTTP_FINAL) != n) {
        req->status = 502;
        req->wsi = nullptr;
        req->response << "Failed to write body";
        req->completed.store(true, std::memory_order_release);
      }
      break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
      req->status = lws_http_client_http_response(wsi);
      break;

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      if (lws_http_client_read(wsi, &req->px, &req->buffer_len) < 0) {
        return -1;
      }
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
      req->response << std::string((char*)in, len);
      return 0;

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
      req->wsi = nullptr;
      req->completed.store(true, std::memory_order_release);
      lws_cancel_service(lws_get_context(wsi));
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      req->wsi = nullptr;
      break;

    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}

const struct lws_protocols s_protocols[] = {
    { "http", http_callback, 0, 0 },
    { nullptr, nullptr, 0, 0 }
};

std::once_flag s_init;

} // namespace

namespace slick {
namespace net {

class http_client::http_client_impl final {
  uint64_t cursor_ = 0;
  lws_context *context_ = nullptr;
  int32_t cpu_affinity_ = -1;
  int16_t port_ = 0;
  std::atomic_bool run_ {true};
  lws_client_connect_info cci_;
  std::string address_;
  std::string ssl_ca_file_path_;
  std::thread thread_;
  ring_buffer<request_info> queue_;

 public:
  http_client_impl(std::string&& address, int16_t port, std::string&& ca_path, int32_t cpu_affinity = -1)
    : cpu_affinity_(cpu_affinity)
    , port_(port)
    , address_(std::move(address))
    , ssl_ca_file_path_(std::move(ca_path))
    , queue_(65536) {

#ifndef NDEBUG
    std::call_once(s_init, []() {
      lws_set_log_level(LLL_NOTICE | LLL_WARN | LLL_INFO | LLL_ERR | LLL_USER, nullptr);
    });
#endif

    lws_context_creation_info info;
    memset(&info, 0, sizeof(info));

    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = s_protocols;
    info.ka_time = 5;
    info.ka_probes = 5;
    info.ka_interval = 1;
    if (!ssl_ca_file_path_.empty()) {
      info.client_ssl_ca_filepath = ssl_ca_file_path_.c_str();
    }

    context_ = lws_create_context(&info);
    if (!context_) {
      throw std::runtime_error("Failed to create lws_context");
    }

    thread_ = std::thread(&http_client_impl::run, this);

    memset(&cci_, 0, sizeof(cci_));
	cci_.port = port_;
    cci_.address = address_.c_str();
    cci_.host = cci_.address;
    cci_.origin = cci_.address;
    cci_.protocol = s_protocols[0].name;
    cci_.context = context_;
    cci_.alpn = "http/1.1";

	if (port_ == 443) {
		cci_.ssl_connection = LCCSCF_USE_SSL;
	}
  }

  ~http_client_impl() {
    run_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }

    if (context_) {
      lws_cancel_service(context_);
      lws_context_destroy(context_);
      context_ = nullptr;
    }
  }

 public:
  inline http_response request(const char *method, std::shared_ptr<http_request> &&request);
  inline void request(const char *method,
                      std::shared_ptr<http_request> &&request,
                      http_client::AsyncCallback &&callback);

 private:
  inline void run();
};

} //namespace net
} //namespace slick

http_client::http_client(std::string address, int16_t port, std::string ca_file_path, int32_t cpu_affinity) {
  if (address.find("https://") == 0) {
    port = port != -1 ? port : 443;
    impl_ = std::make_unique<http_client_impl>(address.substr(8), port, std::move(ca_file_path), cpu_affinity);
  } else if (address.find("http://") == 0) {
    port = port != -1 ? port : 80;
    impl_ = std::make_unique<http_client_impl>(address.substr(7), port, std::move(ca_file_path), cpu_affinity);
  } else {
    impl_ = std::make_unique<http_client_impl>(address.substr(7), port, std::move(ca_file_path), cpu_affinity);
  }
}

http_client::http_client(std::string address)
  : http_client(std::move(address), -1, "", -1) {
}

http_client::http_client(std::string address, int32_t cpu_affinity)
  : http_client(std::move(address), -1, "", cpu_affinity) {

}
http_client::http_client(std::string address, std::string ca_file_path, int32_t cpu_affinity)
  : http_client(std::move(address), -1, std::move(ca_file_path), cpu_affinity) {
}

http_client::~http_client() {}

http_response http_client::get(std::string path) {
  return impl_->request("GET", std::make_shared<http_request>(std::move(path)));
}

http_response http_client::get(std::shared_ptr<http_request> request) {
  return impl_->request("GET", std::move(request));
}

http_response http_client::post(std::shared_ptr<http_request> request) {
  return impl_->request("POST", std::move(request));
}

http_response http_client::put(std::shared_ptr<http_request> request) {
  return impl_->request("PUT", std::move(request));
}

http_response http_client::del(std::string path) {
  return impl_->request("DELETE", std::make_shared<http_request>(std::move(path)));
}

http_response http_client::del(std::shared_ptr<http_request> request) {
  return impl_->request("DELETE", std::move(request));
}

void http_client::get(std::string path, AsyncCallback&& callback) {
  impl_->request("GET", std::make_shared<http_request>(std::move(path)), std::move(callback));
}
void http_client::get(std::shared_ptr<http_request> request, AsyncCallback&& callback) {
	impl_->request("GET", std::move(request), std::move(callback));
}

//******************** http_client_impl implementation ********************

inline void http_client::http_client_impl::run() {
  std::unordered_set<request_info*> requests;

  if (cpu_affinity_ != -1) {
#ifdef WIN32
    auto thrd = GetCurrentThread();
    SetThreadAffinityMask(thrd, 1LL << (cpu_affinity_ % std::thread::hardware_concurrency()));
#else
    cpu_set_t cpus;
    CPU_ZERO(&cpus);
    CPU_SET((cpu_affinity_ % std::thread::hardware_concurrency()), &cpus);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpus);
#endif
  }

  while (run_.load(std::memory_order_relaxed)) {
    auto sn = queue_.available();
    if (cursor_ != sn) {
      auto& req = queue_[cursor_++];
      lws_client_connect_info cci;
      memcpy(&cci, &cci_, sizeof(cci));
      cci.path = req.request->path().c_str();
      cci.pwsi = &req.wsi;
      cci.method = req.method;
      cci.userdata = &req;

      requests.emplace(&req);

      lws_client_connect_via_info(&cci);
    }

    if (!requests.empty()) {
      lws_service(context_, 0);
    }

    for (auto it = requests.begin(); it != requests.end();) {
      auto req = *it;
      if (req->completed.load(std::memory_order_relaxed)) {
        it = requests.erase(it);
      } else {
        ++it;
      }
    }
  }
}

http_response http_client::http_client_impl::request(const char* method, std::shared_ptr<http_request> &&request) {
  auto slot = queue_.reserve();
  auto& req = slot[0];
  req.request = std::move(request);
  req.callback = nullptr;
  req.method = method;
  slot.publish();
  while (!req.completed.load(std::memory_order_relaxed));
  return http_response(req.status, req.response.str());
}

inline void http_client::http_client_impl::request(const char *method,
                                                   std::shared_ptr<http_request> &&request,
                                                   http_client::AsyncCallback &&callback) {
	auto slot = queue_.reserve();
	auto& req = slot[0];
	req.request = std::move(request);
	req.callback = std::move(callback);
	req.method = method;
	slot.publish();
}
