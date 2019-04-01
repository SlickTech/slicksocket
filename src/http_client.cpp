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

#include "slicksocket/http_client.h"
#include "utils.h"
#include <atomic>
#include "socket_service.h"

using namespace slick::net;

http_client::http_client(std::string address,
                         std::string origin,
                         std::string ca_file_path,
                         int32_t cpu_affinity,
                         bool use_global_thread)
  : service_(use_global_thread
      ? socket_service::global(ca_file_path, cpu_affinity) : new socket_service(std::move(ca_file_path), cpu_affinity))
  , address_(std::move(address))
  , origin_(std::move(origin)) {

  auto pos = address_.find(':');
  bool has_port = pos != 4 && pos !=5 && pos != std::string::npos;
  if (has_port) {
    port_ = std::stoi(address_.substr(pos + 1));
  }

  if (address_.find("https://") != std::string::npos) {
    address_ = has_port ? address_.substr(8, pos - 8) : address_.substr(8);
    if (port_ == -1) {
      port_ = 443;
    }
  } else if (address_.find("http://") != std::string::npos) {
    address_ = has_port ? address_.substr(7, pos - 7) : address_.substr(7);
    if (port_ == 0) {
      port_ = 80;
    }
  }
}

http_client::~http_client() {
  if (service_ && !service_->is_global()) {
    delete service_;
    service_ = nullptr;
  }
}

http_response http_client::request(const char* method, std::string path, const std::shared_ptr<http_request>& request) {
  auto req = service_->get_http_request();
  if (!req) {
    return http_response(500, "", "Failed to create lws_context");
  }
  req->path = std::move(path);
  memset(&req->cci, 0, sizeof(req->cci));
  req->cci.port = port_;
  req->cci.address = address_.c_str();
  req->cci.host = req->cci.address;
  if (!origin_.empty()) {
    req->cci.origin = origin_.c_str();
  }
  req->cci.alpn = "http/1.1";
  req->cci.path = req->path.c_str();
  req->cci.pwsi = &req->wsi;
  req->cci.userdata = req;
  req->cci.protocol = "http";
  req->cci.method = method;

  if (port_ == 443) {
    req->cci.ssl_connection = LCCSCF_USE_SSL;
  }

  req->request = request;
  req->callback = nullptr;
  service_->request(req);
  while (!req->completed.load(std::memory_order_relaxed));
  http_response response(req->status, req->content_type, req->response.str());
  service_->release_request(req);
  return response;
}

void http_client::request(const char* method, std::string path, AsyncCallback&& callback) {
  auto req = service_->get_http_request();
  if (!req) {
    callback(http_response(500, "", "Failed to create lws_context"));
    return;
  }
  req->type = request_type::http;
  req->path = std::move(path);
  memset(&req->cci, 0, sizeof(req->cci));
  req->cci.port = port_;
  req->cci.address = address_.c_str();
  req->cci.host = req->cci.address;
  req->cci.origin = req->cci.address;
  req->cci.alpn = "http/1.1";
  req->cci.path = req->path.c_str();
  req->cci.pwsi = &req->wsi;
  req->cci.userdata = req;
  req->cci.protocol = "http";
  req->cci.method = method;

  if (port_ == 443) {
    req->cci.ssl_connection = LCCSCF_USE_SSL;
  }

  req->request = nullptr;
  req->callback = callback;
  service_->request(req);
}

void http_client::request(const char *method,
                          std::string path,
                          const std::shared_ptr<http_request>& request,
                          AsyncCallback &&callback) {
  auto req = service_->get_http_request();
  if (!req) {
    callback(http_response(500, "", "Failed to create lws_context"));
    return;
  }
  req->type = request_type::http;
  req->path = std::move(path);
  memset(&req->cci, 0, sizeof(req->cci));
  req->cci.port = port_;
  req->cci.address = address_.c_str();
  req->cci.host = req->cci.address;
  req->cci.origin = req->cci.address;
  req->cci.alpn = "http/1.1";
  req->cci.path = req->path.c_str();
  req->cci.pwsi = &req->wsi;
  req->cci.userdata = req;
  req->cci.protocol = "http";
  req->cci.method = method;

  if (port_ == 443) {
    req->cci.ssl_connection = LCCSCF_USE_SSL;
  }

  req->request = request;
  req->callback = callback;
  service_->request(req);
}

int http_callback(struct lws *wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len) {
  auto req = (http_request_info*)lws_wsi_user(wsi);

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      lwsl_user("%s:%d Connection error occurred. ", req->cci.address, req->cci.port);
      req->response << req->path << " error occurred. ";
      if (in && len) {
        req->response << std::string((char*)in, len);
        lwsl_user("%s", in);
      }
      lwsl_user("\n");
      req->wsi = nullptr;
      req->completed.store(true, std::memory_order_release);
      break;

    case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER: {
      unsigned char **p = (unsigned char **) in, *end = (*p) + len - 1;
      if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_USER_AGENT, (unsigned char*)"libwebsocket", 12, p, end)) {
        req->wsi = nullptr;
        req->response << req->path << " failed to add User-Agent header";
        req->completed.store(true, std::memory_order_release);
        return -1;
      }

      if (!req->request) {
        break;
      }

      if (!req->request->headers().empty()) {
        for (auto &kvp : req->request->headers()) {
          if (lws_add_http_header_by_name(wsi,
                                          (unsigned char *) kvp.first.c_str(),
                                          (unsigned char *) kvp.second.c_str(),
                                          (int) kvp.second.size(),
                                          p,
                                          end)) {
            req->wsi = nullptr;
            req->response << req->path << " failed to add header " << kvp.first << ": " << kvp.second;
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
          req->wsi = nullptr;
          req->response << req->path << " failed to add header Content-Type:" << content_type;
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
          req->wsi = nullptr;
          req->response << req->path << " failed to add header Content-Length:" << sz;
          req->completed.store(true, std::memory_order_release);
          return -1;
        }
        lws_client_http_body_pending(wsi, 1);
        lws_callback_on_writable(wsi);
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
      auto p = (unsigned char*)&req->buffer[LWS_PRE];
	  if (!req->request) {
		  break;
	  }

      const auto& body = req->request->body();
      auto sz = body.size() + 1;

      if (sz > sizeof(req->buffer) - LWS_PRE) {
        req->wsi = nullptr;
        req->response << req->path << " body exceeds buffer size";
        req->completed.store(true, std::memory_order_release);
        return -1;
      }

      auto n = lws_snprintf((char*)p, sz, "%s", body.c_str());
      lws_client_http_body_pending(wsi, 0);

      if (lws_write(wsi, p, n, LWS_WRITE_HTTP_FINAL) != n) {
        req->wsi = nullptr;
        req->response << req->path << " failed to write body";
        req->completed.store(true, std::memory_order_release);
        return -1;
      }
      break;
    }

    case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP: {
      req->status = lws_http_client_http_response(wsi);
      assert(sizeof(req->content_type) > lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE));
      lws_hdr_copy(wsi, req->content_type, sizeof(req->content_type), WSI_TOKEN_HTTP_CONTENT_TYPE);
      req->response.clear();
      break;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
      char *px = req->buffer + LWS_PRE;
      auto buffer_len = req->buffer_len;
      if (lws_http_client_read(wsi, &px, &buffer_len) < 0) {
        req->completed.store(true, std::memory_order_relaxed);
        return -1;
      }
      return 0;
    }

    case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
      req->response << std::string((char*)in, len);
      return 0;

    case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
    case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
      req->wsi = nullptr;
      req->completed.store(true, std::memory_order_release);
      lws_cancel_service(lws_get_context(wsi));
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      req->wsi = nullptr;
      req->completed.store(true, std::memory_order_release);
      break;

    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}