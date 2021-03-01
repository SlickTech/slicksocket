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

#define CATCH_CONFIG_MAIN

#include "catch.hpp"
#include "slicksocket/http_client.h"
#include <condition_variable>
#include "slicksocket/callback.h"
#include "slicksocket/socket_server.h"
#include "slicksocket/socket_client.h"
#include <libwebsockets.h>

using namespace slick::net;

namespace {

TEST_CASE("HTTP GET") {
  http_client client("https://api.pro.coinbase.com", "cert.pem");
  auto response = client.request("GET", "/products");
  std::cout << response.response_text << std::endl;
  REQUIRE((response.status == 200 && !response.response_text.empty()
      && response.response_text.find("BTC-USD") != std::string::npos
      && response.response_text.find("quote_currency") != std::string::npos));

  std::condition_variable cond;
  std::mutex mutex;
  std::unique_lock<std::mutex> lock(mutex);
  client.request("GET", "/products", [&cond](http_response rsp) {
    std::cout << rsp.status << " " << rsp.response_text << std::endl;
    REQUIRE((rsp.status == 200 && !rsp.response_text.empty()
        && rsp.response_text.find("BTC-USD") != std::string::npos
        && rsp.response_text.find("quote_currency") != std::string::npos));
    cond.notify_one();
  });

  cond.wait(lock);
}

TEST_CASE("HTTP POST") {
  http_client client("https://postman-echo.com", "cert.pem");
  auto request = std::make_shared<http_request>();
  request->add_header("Authorization", "test");
  request->add_body("{\"name\":\"Tom\"}", "application/json");
  auto response = client.request("POST", "/post", std::move(request));
  printf("%d %s\n", response.status, response.response_text.c_str());
  REQUIRE((response.status == 200 && response.response_text.find("\"authorization\":\"test\"") != std::string::npos
      && response.response_text.find("\"json\":{\"name\":\"Tom\"}") != std::string::npos));
}

TEST_CASE("HTTP PUT") {
  http_client client("https://postman-echo.com", "cert.pem");
  auto request = std::make_shared<http_request>();
  request->add_header("Authorization", "test");
  request->add_body("{\"id\":12345}", "application/json");
  auto response = client.request("PUT", "/put", std::move(request));
  REQUIRE((response.status == 200 && response.response_text.find("\"authorization\":\"test\"") != std::string::npos
      && response.response_text.find("\"json\":{\"id\":12345}") != std::string::npos));
}

class socket_server_impl : public socket_server, public socket_server_callback_t {
 public:
  socket_server_impl() : socket_server(this) {
  }

  void on_client_connected(void* client_handle) override {
    lwsl_user("client %p connected\n", client_handle);
  }

  void on_client_disconnected(void* client_handle) override {
    lwsl_user("client %p disconnected\n", client_handle);
  }

  void on_error(void* client_handle, const char* msg, size_t len) override {

  }

  void on_data(void* client_handle, const char* data, size_t len) override {
    std::string msg(data, len);
    lwsl_user("data from client: %s\n", msg.c_str());
    REQUIRE(msg == "hello");
    send(client_handle, data, len);
  }
};

class socket_client_impl : public socket_client, public client_callback_t {
  bool run_ = true;
 public:
  socket_client_impl() : socket_client(this, "127.0.0.1", 5000) {}

  bool working() const noexcept { return run_; }

  void on_connected() override {
    lwsl_user("client connected\n");
    send("hello", 5);
  }
  void on_disconnected() override {
    lwsl_user("client disconnected\n");
  }
  void on_error(const char* msg, size_t len) override {
    lwsl_user("client error\n");
  }
  void on_data(const char* data, size_t len, size_t remaining) override {
    std::string msg(data, len);
    lwsl_user("data from server: %s\n", msg.c_str());
    REQUIRE(msg == "hello");
    run_ = false;
  }
};

TEST_CASE("RAW socket") {

  lws_set_log_level(LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE, NULL);

  socket_server_impl server;
  std::thread thrd([&server]() {
    server.serve(5000);
  });

  sleep(10);
  socket_client_impl client;
  client.connect();
  while (client.working()) {
  }
  client.stop();
  sleep(10);
  server.stop();
  if (thrd.joinable()) {
    thrd.join();
  }
}



}