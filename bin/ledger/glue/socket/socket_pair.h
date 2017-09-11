// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_PAIR_H_
#define APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_PAIR_H_

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "mx/socket.h"

namespace glue {

// SocketPair produces a pair of connected sockets.
class SocketPair {
 public:
  SocketPair();
  ~SocketPair();

  mx::socket socket1;
  mx::socket socket2;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SocketPair);
};

inline SocketPair::SocketPair() {
  FXL_CHECK(mx::socket::create(0u, &socket1, &socket2) == MX_OK);
}

inline SocketPair::~SocketPair() {}

}  // namespace glue

#endif  // APPS_LEDGER_SRC_GLUE_SOCKET_SOCKET_PAIR_H_
