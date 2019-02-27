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
#include "ring_buffer.h"
#include <libwebsockets.h>
#include <thread>
#include <atomic>
#include <unordered_set>
#include <array>

using namespace slick::net;
using namespace slick::core;

namespace slick {
namespace net {

inline int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);

const struct lws_protocols s_protocols[] = {
    { "ws", ws_callback, 0, 0 },
    { nullptr, nullptr, 0, 0 }
};

struct client_info {
  lws *wsi = nullptr;
  websocket_callback *callback = nullptr;
  std::string address;
  std::string path;
  lws_client_connect_info cci;
  core::ring_string_buffer sending_buffer;
  std::atomic_bool shutdown {false};

  client_info() : sending_buffer(4096) {}
  client_info(std::string addr, std::string pth, websocket_callback* cb)
    : callback(cb)
    , address(std::move(addr))
    , path(std::move(pth))
    , sending_buffer(4096)
  {
    memset(&cci, 0, sizeof(cci));
  }
};

static std::string ssl_ca_file_path_;

class websocket_service {

  std::thread thread_;
  lws_context *context_ = nullptr;
  uint64_t cursor_ = 0;
  std::atomic_bool run_{true};
  ring_buffer<std::shared_ptr<client_info>> queue_;

 public:
  static websocket_service& get() noexcept {
    static websocket_service instance;
    return instance;
  }

  bool add_client(const std::shared_ptr<client_info>& info) {
    if (!context_) {
      info->callback->on_error("Failed to create lws_context.", 29);
      return false;
    }

    auto slot = queue_.reserve();
    auto& ci = slot[0];
    ci = info;
	slot.publish();
    return true;
  }

 private:
  websocket_service()
    : queue_(256)
  {
    lws_context_creation_info context_info;
    memset(&context_info, 0, sizeof(context_info));

    context_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    context_info.port = CONTEXT_PORT_NO_LISTEN;
    context_info.protocols = s_protocols;
    context_info.ka_time = 5;
    context_info.ka_probes = 5;
    context_info.ka_interval = 1;
    if (!ssl_ca_file_path_.empty()) {
      context_info.client_ssl_ca_filepath = ssl_ca_file_path_.c_str();
    }

    context_ = lws_create_context(&context_info);
    thread_ = std::thread([this]() { serve(); });
  }

  ~websocket_service() {
    run_.store(false, std::memory_order_relaxed);

    if (thread_.joinable()) {
      thread_.join();
    }

    if (context_) {
      lws_context_destroy(context_);
      context_ = nullptr;
    }
  }

 private:
  void serve() {
    std::unordered_set<std::shared_ptr<client_info>> clients;
    uint64_t sn = 0;
    while(run_.load(std::memory_order_relaxed)) {
      sn = queue_.available();
      if (cursor_ != sn) {
        auto client = queue_[cursor_++];
        clients.emplace(client);

        auto& cci = client->cci;
        cci.address = client->address.c_str();
        cci.host = cci.address;
        cci.origin = cci.address;
        cci.protocol = s_protocols[0].name;
        cci.context = context_;
        cci.alpn = "http/1.1";

        if (cci.port == 443) {
          cci.ssl_connection = LCCSCF_USE_SSL;
        }

        cci.path = client->path.c_str();
        cci.pwsi = &client->wsi;
        cci.userdata = client.get();

        lws_client_connect_via_info(&cci);
      }

      if (!clients.empty()) {
        lws_service(context_, 0);
      }

      for (auto it = clients.begin(); it != clients.end();) {
        auto client = it->get();
        if (!client->wsi) {
          if (client->shutdown.load(std::memory_order_relaxed)) {
            // client shutdown
            it = clients.erase(it);
          } else {
            // reconnect
            lws_client_connect_via_info(&client->cci);
            ++it;
          }
        } else {
          ++it;
        }
      }
    }
  }
};

int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  auto client = (client_info*)lws_wsi_user(wsi);
  if (!client) {
    return lws_callback_http_dummy(wsi, reason, user, in, len);
  }

  if (client->shutdown.load(std::memory_order_relaxed)) {
    client->wsi = nullptr;
    return -1;
  }

  switch (reason) {
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
      client->wsi = nullptr;
      client->callback->on_error((const char*)in, len);
      break;

    case LWS_CALLBACK_CLIENT_ESTABLISHED:
      client->sending_buffer.reset();
      client->callback->on_connected();
      break;

    case LWS_CALLBACK_CLIENT_WRITEABLE: {
      auto msg = client->sending_buffer.read();
      if (msg.second) {
        auto n = lws_write(wsi, (unsigned char*)msg.first + LWS_PRE, msg.second - LWS_PRE, LWS_WRITE_TEXT);
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
      break;

    case LWS_CALLBACK_WSI_DESTROY:
      client->wsi = nullptr;
      break;
  }

  if (client->shutdown.load(std::memory_order_relaxed)) {
    client->wsi = nullptr;
    return -1;
  }
  return lws_callback_http_dummy(wsi, reason, user, in, len);
}


} // namespace net
} // namespace slick

websocket_client::websocket_client(websocket_callback* callback, std::string address, std::string path)
  : callback_(callback)
  , info_(std::make_shared<client_info>(std::move(address), std::move(path), callback)) {
  auto pos = info_->address.find(':');
  if (pos != 3 && pos != 4 && pos != std::string::npos) {
    info_->cci.port = std::stoi(address.substr(pos + 1));
  }

  if (info_->address.find("wss://") != std::string::npos) {
    info_->address = (pos != std::string::npos) ? info_->address.substr(6, pos - 6) : info_->address.substr(8);
    if (info_->cci.port == 0) {
      info_->cci.port = 443;
    }
  } else if (info_->address.find("ws://") != std::string::npos) {
    info_->address = (pos != std::string::npos) ? info_->address.substr(5, pos - 5) : info_->address.substr(7);
    if (info_->cci.port == 0) {
      info_->cci.port = 80;
    }
  }
}

void websocket_client::set_ssl_certificate_file_path(std::string ca_path) {
  ssl_ca_file_path_ = std::move(ca_path);
}

bool websocket_client::connect() {
  return websocket_service::get().add_client(info_);
}

void websocket_client::stop() {
  info_->shutdown.store(true, std::memory_order_relaxed);
}

bool websocket_client::send(const char *msg, size_t len) {
  if (!info_->wsi) {
    return false;
  }

  static std::array<char, LWS_PRE> header{'0'};

  // reserve space for LWS header
  if (!info_->sending_buffer.write(&header[0], LWS_PRE, len)) {
    return false;
  }

  if (info_->sending_buffer.write(msg, len)) {
    lws_callback_on_writable(info_->wsi);
    return true;
  }
  return false;
}


