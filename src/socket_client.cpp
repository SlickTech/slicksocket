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

#include "slicksocket/socket_client.h"
#include "slicksocket/callback.h"
#include "socket_service.h"

using namespace slick::net;

socket_client::socket_client(client_callback_t *callback,
                             std::string address,
                             uint32_t port,
                             int32_t cpu_affinity,
                             bool use_global_thread)
  : service_(use_global_thread
             ? socket_service::global("", cpu_affinity)
             : new socket_service("", cpu_affinity))
  , address_(std::move(address))
  , port_(port)
  , callback_(callback)
{
}

socket_client::~socket_client() {
  if (service_ && !service_->is_global()) {
    delete service_;
    service_ = nullptr;
  }
}

bool socket_client::connect() {
  if (request_) {
    return true;
  }

  request_ = service_->get_request_info(request_type::socket);
  if (!request_) {
    return false;
  }

  memset(&request_->cci, 0, sizeof(request_->cci));
  request_->cci.port = port_;
  request_->cci.address = address_.c_str();
  request_->cci.host = request_->cci.address;
  request_->cci.origin = request_->cci.address;
  request_->cci.protocol = "raw_socket";
  request_->cci.pwsi = &request_->wsi;
  request_->cci.method = "RAW";
  request_->cci.userdata = request_;
  request_->socket_info.callback = callback_;
  request_->socket_info.sending_buffer.reset();
  request_->socket_info.shutdown.store(false, std::memory_order_relaxed);
  service_->request(request_);
  return true;
}

void socket_client::stop() noexcept {
  if (request_) {
    request_->socket_info.shutdown.store(true, std::memory_order_relaxed);
    request_ = nullptr;
  }
}

bool socket_client::send(char *msg, size_t len) {
  if (!request_ || !request_->wsi) {
    return false;
  }

  auto& socket_info = request_->socket_info;

  if (socket_info.sending_buffer.write(msg, len)) {
    lws_callback_on_writable(request_->wsi);
    return true;
  }
  return false;
}

int raw_socket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  auto req = reinterpret_cast<request_info*>(lws_wsi_user(wsi));
  if (!req || req->type != request_type::socket) {
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  auto& client = req->socket_info;

  switch (reason) {
    case LWS_CALLBACK_RAW_CONNECTED:
      lwsl_user("%s:%d connected.\n", req->cci.address, req->cci.port);
      client.sending_buffer.reset();
      client.callback->on_connected();
      client.disconnecte_callback_invoked = false;
      break;

    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      lwsl_user("%s:%d Connection error occurred. ", req->cci.address, req->cci.port);
      if (in && len) {
        lwsl_user("%s", (const char*)in);
      }
      lwsl_user("\n");
      client.callback->on_error((const char*)in, len);
      break;

    case LWS_CALLBACK_RAW_WRITEABLE: {
      auto msg = client.sending_buffer.read();
      if (msg.second) {
        size_t n = lws_write(wsi, (unsigned char*)msg.first, msg.second, LWS_WRITE_RAW);
        if (n < len) {
          req->wsi = nullptr;
          return -1;
        }
        lws_callback_on_writable(wsi);
      }
      break;
    }

    case LWS_CALLBACK_RAW_RX: {
      client.callback->on_data((const char*)in, len, 0);
      break;
    }

    case LWS_CALLBACK_RAW_CLOSE:
      req->wsi = nullptr;
      lwsl_user("%s:%d disconnected.\n", req->cci.address, req->cci.port);
      client.callback->on_disconnected();
      client.disconnecte_callback_invoked = true;
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      req->wsi = nullptr;
      if (!client.disconnecte_callback_invoked) {
        lwsl_user("%s:%d disconnected.\n", req->cci.address, req->cci.port);
        client.callback->on_disconnected();
        client.disconnecte_callback_invoked = true;
      }
      break;

    default:
      break;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}