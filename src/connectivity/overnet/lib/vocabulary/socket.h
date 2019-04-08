// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstring>
#include <sstream>
#include "src/connectivity/overnet/lib/vocabulary/ip_addr.h"
#include "src/connectivity/overnet/lib/vocabulary/slice.h"
#include "src/connectivity/overnet/lib/vocabulary/status.h"

namespace overnet {

class Socket {
 public:
  Socket(int socket) : socket_(socket) {}
  Socket() : socket_(-1) {}
  ~Socket() { Close(); }

  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;

  Socket(Socket&& other) : socket_(other.socket_) { other.socket_ = -1; }

  Socket& operator=(Socket&& other) {
    std::swap(socket_, other.socket_);
    return *this;
  }

  void Close();
  bool IsValid() const { return socket_ != -1; }
  int get() const { return socket_; }

  Status SetOptReusePort(bool reuse);

  template <class T>
  Status SetOpt(int level, int opt, T value) {
    return SetOpt(level, opt, &value, sizeof(value));
  }

  Status SetOpt(int level, int opt, void* value, size_t value_size);
  Status Bind(IpAddr addr);
  Status SendTo(Slice data, int flags, IpAddr dest);

  struct DataAndAddr {
    Slice data;
    IpAddr addr;
  };

  StatusOr<DataAndAddr> RecvFrom(size_t maximum_packet_size, int flags);

 private:
  int socket_;
};

}  // namespace overnet
