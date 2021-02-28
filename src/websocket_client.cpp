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
                                   std::string address,
                                   std::string origin,
                                   std::string path,
                                   std::string ca_file_path,
                                   int32_t cpu_affinity,
                                   bool use_global_service)
  : callback_(callback)
  , service_(use_global_service
      ? socket_service::global(ca_file_path, cpu_affinity) : new socket_service(std::move(ca_file_path), cpu_affinity))
  , address_(std::move(address))
  , origin_(std::move(origin))
  , path_(std::move(path)) {
  auto pos = address.find(':');
  if (pos != 3 && pos != 4 && pos != std::string::npos) {
    port_ = std::stoi(address.substr(pos + 1));
  }

  if (address_.find("wss://") != std::string::npos) {
    address_ = (pos != std::string::npos) ? address_.substr(6, pos - 6) : address_.substr(6);
    if (port_ == -1) {
      port_ = 443;
    }
  } else if (address_.find("ws://") != std::string::npos) {
    address_ = (pos != std::string::npos) ? address_.substr(5, pos - 5) : address_.substr(5);
    if (port_ == -1) {
      port_ = 80;
    }
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
    request_->shutdown.store(true, std::memory_order_relaxed);
  }

  request_ = service_->get_ws_request();
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

  request_->callback = callback_;
  request_->sending_buffer.reset();
  request_->shutdown.store(false, std::memory_order_relaxed);
  service_->request(request_);
  return true;
}

void websocket_client::stop() noexcept {
  if (request_) {
    request_->shutdown.store(true, std::memory_order_relaxed);
    request_ = nullptr;
  }
}

bool websocket_client::send(const char *msg, size_t len) noexcept {
  if (!request_ || !request_->wsi) {
    return false;
  }

  static std::array<char, LWS_PRE> header{'0'};

  // reserve space for LWS header
  if (!request_->sending_buffer.write(&header[0], LWS_PRE, len)) {
    return false;
  }

  if (request_->sending_buffer.write(msg, len)) {
    lws_callback_on_writable(request_->wsi);
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

  auto client = reinterpret_cast<ws_request_info*>(req);
  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      client->wsi = nullptr;
      client->callback->on_error((const char*)in, len);
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      client->sending_buffer.reset();
      client->callback->on_connected();
      client->disconnecte_callback_invoked = false;
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      auto msg = client->sending_buffer.read();
      if (msg.second) {
        size_t n = lws_write(wsi, (unsigned char*)msg.first + LWS_PRE, msg.second - LWS_PRE, LWS_WRITE_TEXT);
        if (n < len) {
          client->wsi = nullptr;
          return -1;
        }
        lws_callback_on_writable(wsi);
      }
      break;
    }

    case LWS_CALLBACK_CLIENT_RECEIVE: {
      auto remaining = lws_remaining_packet_payload(wsi);
      client->callback->on_data((const char*)in, len, remaining);
      break;
    }

    case LWS_CALLBACK_CLIENT_CLOSED:
      client->wsi = nullptr;
      client->callback->on_disconnected();
      client->disconnecte_callback_invoked = true;
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      client->wsi = nullptr;
      if (!client->disconnecte_callback_invoked) {
        client->callback->on_disconnected();
        client->disconnecte_callback_invoked = true;
      }
      break;

    default:
      return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  if (client->wsi && client->shutdown.load(std::memory_order_relaxed)) {
    lwsl_user("Shutting down %s:%d%s\n", client->cci.address, client->cci.port, client->cci.path);
    client->wsi = nullptr;
    return -1;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}


