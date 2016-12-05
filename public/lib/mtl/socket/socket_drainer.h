// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_SOCKET_SOCKET_DRAINER_H_
#define LIB_MTL_SOCKET_SOCKET_DRAINER_H_

#include "lib/fidl/c/waiter/async_waiter.h"
#include "lib/fidl/cpp/waiter/default.h"
#include "lib/ftl/macros.h"
#include "mx/socket.h"

namespace mtl {

class SocketDrainer {
 public:
  class Client {
   public:
    virtual void OnDataAvailable(const void* data, size_t num_bytes) = 0;
    virtual void OnDataComplete() = 0;

   protected:
    virtual ~Client() {}
  };

  SocketDrainer(Client* client,
                const FidlAsyncWaiter* waiter = fidl::GetDefaultAsyncWaiter());
  ~SocketDrainer();

  void Start(mx::socket source);

 private:
  void ReadData();
  void WaitForData();
  static void WaitComplete(mx_status_t result,
                           mx_signals_t pending,
                           void* context);

  Client* client_;
  mx::socket source_;
  const FidlAsyncWaiter* waiter_;
  FidlAsyncWaitID wait_id_;
  bool* destruction_sentinel_;

  FTL_DISALLOW_COPY_AND_ASSIGN(SocketDrainer);
};

}  // namespace mtl

#endif  // LIB_MTL_SOCKET_SOCKET_DRAINER_H_
