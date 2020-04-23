// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_
#define SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/bind.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <fbl/algorithm.h>

#include "lib/async-loop/loop.h"

namespace fake_ddk {

typedef zx_status_t(MessageOp)(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn);

// Helper class to call fidl handlers in unit tests
// Use in conjunction with fake ddk
//
// Example usage:
//          // After device_add call
//          <fidl_client_function> ( <fake_ddk>.FidlClient().get(), <args>);
//
// Note: It is assumed that only one device add is done per fake ddk instance
//
// This can also be used stand alone
// Example standalone usage:
//          DeviceX *dev;
//          FidlMessenger fidl;
//          fidl.SetMessageOp((void *)dev,
//                            [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) ->
//                               zx_status_t {
//                                 return static_cast<Device*>(ctx)->DdkMessage(msg, txn)});
//          <fidl_client_function> ( <fake_ddk>.local().get(), <args>);
//
class FidlMessenger {
 public:
  explicit FidlMessenger() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  explicit FidlMessenger(const async_loop_config_t* config) : loop_(config) {}

  // Local channel to send FIDL client messages
  zx::channel& local() { return local_; }

  // Set handlers to be called when FIDL message is received
  // Note: Message operation context |op_ctx| and |op| must outlive FidlMessenger
  zx_status_t SetMessageOp(void* op_ctx, MessageOp* op) {
    zx_status_t status;
    zx::channel remote;

    if (message_op_) {
      // Message op was already set
      return ZX_ERR_INVALID_ARGS;
    }
    message_op_ = op;
    if ((status = zx::channel::create(0, &local_, &remote)) < 0) {
      return status;
    }

    if ((status = loop_.StartThread("fake_ddk_fidl")) < 0) {
      return status;
    }

    auto dispatch_fn = [](void* ctx, fidl_txn_t* txn, fidl_msg_t* msg,
                          const void* ops) -> zx_status_t {
      return reinterpret_cast<MessageOp*>(const_cast<void*>(ops))(ctx, msg, txn);
    };

    status = fidl_bind(loop_.dispatcher(), remote.release(), dispatch_fn, op_ctx,
                       reinterpret_cast<const void*>(message_op_));
    if (status != ZX_OK) {
      return status;
    }

    return status;
  }

 private:
  MessageOp* message_op_ = nullptr;
  // Channel to mimic RPC
  zx::channel local_;
  // Dispatcher for fidl messages
  async::Loop loop_;
};

}  // namespace fake_ddk

#endif  // SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_
