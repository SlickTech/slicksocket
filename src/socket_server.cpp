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

#include "slicksocket/socket_server.h"
#include "slicksocket/callback.h"
#include <libwebsockets.h>
#include "ring_buffer.h"

#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif

#define QUEUE_SIZE 1048576

using namespace slick::net;

namespace {

int raw_socket_server_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

const struct lws_protocols s_protocols[] = {
    {"raw_socket", raw_socket_server_callback, 0, 0 },
    {nullptr, nullptr, 0, 0}
};

std::unordered_map<struct lws *, ring_string_buffer> clients;
socket_server_callback_t* callback = nullptr;

}

void socket_server::serve(int32_t port, int32_t cpu_affinity) {
  callback = callback_;
  lws_context_creation_info context_info;
  memset(&context_info, 0, sizeof(context_info));

  context_info.port = port;
  context_info.protocols = s_protocols;
  context_info.options = LWS_SERVER_OPTION_ONLY_RAW;
  context_info.user = callback_;

  struct lws_context *context = lws_create_context(&context_info);
  if (!context) {
    lwsl_err("lws init failed\n");
    return;
  }

  lwsl_user("socket_server serve on %d\n", port);

  int n = 0;
  while (n >= 0 && run_.load(std::memory_order_relaxed)) {
    n = lws_service(context, 0);
  }

  lwsl_user("socket_server exit. destroying context\n");
  lws_context_destroy(context);
}

bool socket_server::send(void *client_handle, const char *message, size_t length) {
  auto wsi = reinterpret_cast<lws*>(client_handle);
  auto iter = clients.find(wsi);
  if (iter == clients.end()) {
    return false;
  }

  auto& buffer = iter->second;
  buffer.write(message, length);
  lws_callback_on_writable(wsi);
  return true;
}

namespace {
int raw_socket_server_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {

  switch (reason) {
    case LWS_CALLBACK_PROTOCOL_INIT:
      break;

    case LWS_CALLBACK_PROTOCOL_DESTROY:break;

      /* callbacks related to raw socket descriptor */

    case LWS_CALLBACK_RAW_ADOPT: {
      auto iter = clients.emplace(wsi, 8192).first;
      if (iter == clients.end()) {
        printf("*\n");
      }
      callback->on_client_connected(wsi);
      break;
    }

    case LWS_CALLBACK_RAW_CLOSE:
      callback->on_client_disconnected(wsi);
      break;

    case LWS_CALLBACK_RAW_RX:
      callback->on_data(wsi, (const char *) in, len);
      break;

    case LWS_CALLBACK_RAW_WRITEABLE: {
      auto it = clients.find(wsi);
      if (it != clients.end()) {
        auto &buffer = it->second;
        auto msg = buffer.read();
        if (msg.second) {
          size_t n = lws_write(wsi, (unsigned char *) msg.first, msg.second, LWS_WRITE_RAW);
          if (n < len) {
            return -1;
          }
          lws_callback_on_writable(wsi);
          return 1;
        }
      }
      break;
    }

    default:break;
  }

  return lws_callback_http_dummy(wsi, reason, user, in, len);
}
}
