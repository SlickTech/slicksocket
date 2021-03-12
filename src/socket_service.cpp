#include "socket_service.h"
#include <slicksocket/http_client.h>
#include "utils.h"
#include <mutex>

#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif

#define QUEUE_SIZE 65536

using namespace slick::net;

extern int http_callback(struct lws *wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len);
extern int ws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
extern int raw_socket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

namespace {

const struct lws_protocols s_protocols[] = {
    {"ws", ws_callback, 0, 0},
    {"http", http_callback, 0, 0},
    {"raw_socket", raw_socket_callback, 0, 0},
    {nullptr, nullptr, 0, 0}
};

class spin_lock final {
  std::atomic_flag flag_{false};
 public:
  spin_lock() = default;
  ~spin_lock() = default;

  void lock() { while (flag_.test_and_set(std::memory_order_acquire)); }
  void unlock() { flag_.clear(std::memory_order_release); }
};

spin_lock s_lock;
std::unordered_map<std::string, socket_service*> s_global_service;

struct destroyer {
  ~destroyer() {
    for (auto& kvp : s_global_service) {
      delete kvp.second;
      kvp.second = nullptr;
    }
    s_global_service.clear();
  }
};

destroyer s_destroyer;
}

socket_service::socket_service(std::string ca_file_path, int32_t cpu_affinity, bool is_global)
    : request_pool_(QUEUE_SIZE)
    , request_queue_(QUEUE_SIZE)
    , ca_file_path_(std::move(ca_file_path))
    , is_global_(is_global) {
  lws_context_creation_info context_info;
  memset(&context_info, 0, sizeof(context_info));

  context_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_H2_JUST_FIX_WINDOW_UPDATE_OVERFLOW;
  context_info.port = CONTEXT_PORT_NO_LISTEN;
  context_info.protocols = s_protocols;
  context_info.ka_time = 5;
  context_info.ka_probes = 5;
  context_info.ka_interval = 1;

  if (!ca_file_path_.empty()) {
    context_info.client_ssl_ca_filepath = ca_file_path_.c_str();
  }

  context_ = lws_create_context(&context_info);
  if (context_) {
    thread_ = std::thread([this, cpu_affinity]() { serve(cpu_affinity); });
  }
}

socket_service* socket_service::global(const std::string& ca_file_path, int32_t cpu_affinity) noexcept {
  std::lock_guard<spin_lock> g(s_lock);
  auto it = s_global_service.find(ca_file_path);
  if (it == s_global_service.end()) {
    it = s_global_service.emplace(ca_file_path, new socket_service(ca_file_path, cpu_affinity, true)).first;
  }
  return it->second;
}

void socket_service::serve(int32_t cpu_affinity) {
  uint64_t sn = 0;
  set_cpu_affinity(cpu_affinity);
  while (run_.load(std::memory_order_relaxed)) {
    sn = request_queue_.available();
    if (cursor_ != sn) {
      auto req = request_queue_[cursor_++];
      auto &cci = req->cci;
      cci.context = context_;
      req->service = this;
      requests_.emplace(req);
      lwsl_user("Connecting to %s:%d%s\n", cci.address, cci.port, cci.path);
      lws_client_connect_via_info(&cci);
    }
    
    if (requests_.empty()) {
      std::this_thread::yield();
      continue;
    }

    lws_service(context_, 0);

    for (auto it = requests_.begin(); it != requests_.end();) {
      auto req = *it;
      if (!req->wsi) {
        if (req->type == request_type::http) {
          auto& http_info = req->http_info;
          http_info.completed.store(true, std::memory_order_release);
          if (http_info.callback) {
            http_info.callback(http_response(http_info.status, http_info.content_type, http_info.response.str()));
            request_pool_.release_obj(req);
          }
          it = requests_.erase(it);
        } else if (req->type == request_type::ws || req->type == request_type::socket) {
          auto& socket_info = req->socket_info;
          if (socket_info.shutdown.load(std::memory_order_relaxed)) {
            // client shutdown
            request_pool_.release_obj(req);
            it = requests_.erase(it);
          } else {
            ++it;
          }
        }
      } else {
        ++it;
      }
    }
  }
}

void socket_service::notify_all() const {
  for (auto req : requests_) {
    if (req->wsi) {
      lws_callback_on_writable(req->wsi);
    }
  }
}
