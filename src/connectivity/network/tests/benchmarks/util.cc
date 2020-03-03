// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

void TraceSend(int sendfd, uint8_t* buf, ssize_t size, const char* name, struct sockaddr_in* addr,
               socklen_t addrlen) {
  TRACE_DURATION("benchmark", name);
  FXL_CHECK(sendto(sendfd, buf, size, 0, reinterpret_cast<struct sockaddr*>(addr), addrlen) == size)
      << strerror(errno);
}

void TraceRecv(int recvfd, uint8_t* buf, ssize_t size, const char* name, struct sockaddr_in* addr,
               socklen_t addrlen) {
  TRACE_DURATION("benchmark", name);
  FXL_CHECK(recvfrom(recvfd, buf, size, 0, nullptr, nullptr) == size) << strerror(errno);
}
