/***
 * Copyright (C) 2019 SlickTech. All rights reserved.
 * Licensed under the MIT license. See LICENSE file in the project root for full license information.
 */

#pragma once

#include <cstdint>

namespace slick {
namespace net {

class websocket_client {
 public:
  websocket_client(std::string address, int16_t port = -1, std::string ca_file_path = "");
  virtual ~websocket_client();

  bool send(const char* msg, size_t len);

 private:
  class websocket_impl;
  std::unique_ptr<websocket_impl> impl_;
};

} // namespace net
} // namespace slick