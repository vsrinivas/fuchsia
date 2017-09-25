// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_GLUE_SOCKET_SOCKET_PAIR_H_
#define PERIDOT_BIN_LEDGER_GLUE_SOCKET_SOCKET_PAIR_H_

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "zx/socket.h"

namespace glue {

// SocketPair produces a pair of connected sockets.
class SocketPair {
 public:
  SocketPair();
  ~SocketPair();

  zx::socket socket1;
  zx::socket socket2;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(SocketPair);
};

inline SocketPair::SocketPair() {
  FXL_CHECK(zx::socket::create(0u, &socket1, &socket2) == ZX_OK);
}

inline SocketPair::~SocketPair() {}

}  // namespace glue

#endif  // PERIDOT_BIN_LEDGER_GLUE_SOCKET_SOCKET_PAIR_H_
