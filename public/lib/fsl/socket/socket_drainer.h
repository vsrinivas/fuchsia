// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FSL_SOCKET_SOCKET_DRAINER_H_
#define LIB_FSL_SOCKET_SOCKET_DRAINER_H_

#include <zx/socket.h>

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"

namespace fsl {

class FXL_EXPORT SocketDrainer {
 public:
  class Client {
   public:
    virtual void OnDataAvailable(const void* data, size_t num_bytes) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client();
  };

  SocketDrainer(Client* client,
                const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());
  ~SocketDrainer();

  void Start(zx::socket source);

 private:
  void ReadData();
  void WaitForData();
  static void WaitComplete(zx_status_t result,
                           zx_signals_t pending,
                           uint64_t count,
                           void* context);

  Client* client_;
  zx::socket source_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_;
  bool* destruction_sentinel_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SocketDrainer);
};

}  // namespace fsl

#endif  // LIB_FSL_SOCKET_SOCKET_DRAINER_H_
