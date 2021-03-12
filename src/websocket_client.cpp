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

#include "slicksocket/websocket_client.h"
#include "slicksocket/callback.h"
#include <atomic>
#include <array>
#include "socket_service.h"

using namespace slick::net;

websocket_client::websocket_client(client_callback_t *callback,
                                   std::string url,
                                   std::string origin,
                                   std::string ca_file_path,
                                   int32_t cpu_affinity,
                                   bool use_global_service)
  : callback_(callback)
  , service_(use_global_service
      ? socket_service::global(ca_file_path, cpu_affinity) : new socket_service(std::move(ca_file_path), cpu_affinity))
  , url_(std::move(url))
  , origin_(std::move(origin)) {

  std::string protoco("wss");
  auto pos = url_.find("://");
  if (pos == std::string::npos) {
      pos = url_.find("/");
      if (pos == std::string::npos) {
          address_ = url_;
          path_ = "/";
      }
      else {
          address_ = url_.substr(0, pos);
          path_ = url_.substr(pos);
      }
  }
  else {
      protoco = url_.substr(0, pos);
      auto address_begin = pos + 3;
      auto pos1 = url_.find("/", address_begin);
      if (pos1 == std::string::npos) {
          address_ = url_.substr(address_begin);
          path_ = "/";
      }
      else {
          address_ = url_.substr(address_begin, pos1 - address_begin);
          path_ = url_.substr(pos1);
      }
  }
  
  pos = address_.find(':');
  if (pos != 3 && pos != 4 && pos != std::string::npos) {
    port_ = std::stoi(address_.substr(pos + 1));
    address_ = address_.substr(0, pos);
  }

  if (port_ == -1) {
      port_ = (protoco == "ws") ? 80 : 443;
  }
  
}

websocket_client::~websocket_client() noexcept {
  if (service_ && !service_->is_global()) {
    delete service_;
    service_ = nullptr;
  }
  stop();
}

bool websocket_client::connect() noexcept {
  if (request_) {
    request_->socket_info.shutdown.store(true, std::memory_order_relaxed);
  }

  request_ = service_->get_request_info(request_type::ws);
  if (!request_) {
    return false;
  }
  request_->type = request_type::ws;
  memset(&request_->cci, 0, sizeof(request_->cci));
  request_->cci.port = port_;
  request_->cci.address = address_.c_str();
  request_->cci.host = request_->cci.address;
  if (!origin_.empty()) {
    request_->cci.origin = origin_.c_str();
  }
  request_->cci.path = path_.c_str();
  request_->cci.protocol = "ws";
  request_->cci.pwsi = &request_->wsi;
  request_->cci.userdata = request_;

  if (port_ == 443) {
    request_->cci.ssl_connection = LCCSCF_USE_SSL;
  }

  auto& socket_info = request_->socket_info;
  socket_info.callback = callback_;
  socket_info.sending_buffer.reset();
  socket_info.shutdown.store(false, std::memory_order_relaxed);
  service_->request(request_);
  return true;
}

void websocket_client::stop() noexcept {
  if (request_) {
    request_->socket_info.shutdown.store(true, std::memory_order_relaxed);
    if (request_->wsi) {
      lws_callback_on_writable(request_->wsi);
    }
    service_->wakeup();
    request_ = nullptr;
  }
}

bool websocket_client::send(const char *msg, size_t len) noexcept {
  if (!request_ || !request_->wsi) {
    return false;
  }

  static std::array<char, LWS_PRE> header{'0'};

  auto& socket_info = request_->socket_info;

  // reserve space for LWS header
  if (!socket_info.sending_buffer.write(&header[0], LWS_PRE, len)) {
    return false;
  }

  if (socket_info.sending_buffer.write(msg, len)) {
    lws_callback_on_writable(request_->wsi);
    service_->wakeup();
    return true;
  }
  return false;
}

int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  auto req = (request_info*)lws_wsi_user(wsi);
  if (!req) {
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  // for some callback lws always invokes on the first protocol
  if (req->type != request_type::ws) {
    if (reason == LWS_CALLBACK_WSI_DESTROY) {
      req->wsi = nullptr;
    }
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  auto& client = req->socket_info;
  if (client.shutdown.load(std::memory_order_relaxed)) {
      if (req->wsi) {
          req->wsi = nullptr;
      }
      return -1;
  }

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      req->wsi = nullptr;
      client.callback->on_error((const char*)in, len);
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      client.sending_buffer.reset();
      client.callback->on_connected();
      client.disconnecte_callback_invoked = false;
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      auto msg = client.sending_buffer.read();
      if (msg.first && msg.second) {
        size_t n = lws_write(wsi, (unsigned char*)msg.first + LWS_PRE, msg.second - LWS_PRE, LWS_WRITE_TEXT);
        if (n < len) {
          req->wsi = nullptr;
          return -1;
        }
        lws_callback_on_writable(wsi);
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE: {
      auto remaining = lws_remaining_packet_payload(wsi);
      client.callback->on_data((const char*)in, len, remaining);
      break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED:
      req->wsi = nullptr;
      client.callback->on_disconnected();
      client.disconnecte_callback_invoked = true;
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      req->wsi = nullptr;
      if (!client.disconnecte_callback_invoked) {
        client.callback->on_disconnected();
        client.disconnecte_callback_invoked = true;
      }
      break;

    case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
      lwsl_user("******* LWS_CALLBACK_EVENT_WAIT_CANCELLED\n");
      break;

    default:
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }
  
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}


