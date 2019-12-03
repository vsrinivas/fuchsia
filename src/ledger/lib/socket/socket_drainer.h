// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_H_
#define SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/zx/socket.h>

#include "src/lib/fxl/macros.h"

namespace ledger {

class SocketDrainer {
 public:
  class Client {
   public:
    virtual void OnDataAvailable(const void* data, size_t num_bytes) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client();
  };

  explicit SocketDrainer(Client* client,
                         async_dispatcher_t* dispatcher = async_get_default_dispatcher());
  ~SocketDrainer();

  void Start(zx::socket source);

 private:
  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);

  Client* client_;
  async_dispatcher_t* dispatcher_;
  zx::socket source_;
  async::WaitMethod<SocketDrainer, &SocketDrainer::OnHandleReady> wait_{this};
  bool* destruction_sentinel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketDrainer);
};

}  // namespace ledger

#endif  // SRC_LEDGER_LIB_SOCKET_SOCKET_DRAINER_H_
