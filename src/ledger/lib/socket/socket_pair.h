// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_SOCKET_PAIR_H_
#define SRC_LEDGER_LIB_SOCKET_SOCKET_PAIR_H_

#include <lib/zx/socket.h>

#include "src/ledger/lib/logging/logging.h"

namespace socket {

// SocketPair produces a pair of connected sockets.
class SocketPair {
 public:
  SocketPair();
  SocketPair(const SocketPair&) = delete;
  SocketPair& operator=(const SocketPair&) = delete;
  ~SocketPair();

  zx::socket socket1;
  zx::socket socket2;
};

inline SocketPair::SocketPair() {
  LEDGER_CHECK(zx::socket::create(0u, &socket1, &socket2) == ZX_OK);
}

inline SocketPair::~SocketPair() {}

}  // namespace socket

#endif  // SRC_LEDGER_LIB_SOCKET_SOCKET_PAIR_H_
