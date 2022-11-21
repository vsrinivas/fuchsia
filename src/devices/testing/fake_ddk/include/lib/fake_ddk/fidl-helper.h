// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_
#define SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/fidl/cpp/wire/transaction.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <optional>
#include <variant>

#include <ddktl/fidl.h>
#include <fbl/algorithm.h>

#include "lib/async-loop/loop.h"

namespace fake_ddk {
class FidlProtocol {
 public:
  using Transport = fidl::internal::ChannelTransport;
};
}  // namespace fake_ddk

template <>
class fidl::internal::WireWeakEventSender<fake_ddk::FidlProtocol> {
 public:
  explicit WireWeakEventSender(std::weak_ptr<fidl::internal::AsyncServerBinding> binding)
      : inner_(std::move(binding)) {}
  fidl::internal::WeakEventSenderInner inner_;
};

template <>
struct fidl::IsProtocol<fake_ddk::FidlProtocol> : public std::true_type {};

namespace fake_ddk {

typedef zx_status_t(MessageOp)(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn);

ddk::internal::Transaction MakeDdkInternalTransaction(fidl::Transaction* txn);
ddk::internal::Transaction MakeDdkInternalTransaction(std::unique_ptr<fidl::Transaction> txn);

std::variant<::fidl::Transaction*, std::unique_ptr<::fidl::Transaction>> FromDdkInternalTransaction(
    ddk::internal::Transaction* txn);

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
//                            [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) ->
//                               zx_status_t {
//                                 return static_cast<Device*>(ctx)->DdkMessage(msg, txn)});
//          <fidl_client_function> ( <fake_ddk>.local().get(), <args>);
//
class FidlMessenger : public fidl::internal::IncomingMessageDispatcher {
 public:
  // This is necessary for fidl::BindServer to work. It is usually auto-generated by LLCPP server
  // bindings. Future evolution of LLCPP may cause this code to break.
  using _EnclosingProtocol = FidlProtocol;
  using _Transport = fidl::internal::ChannelTransport;
  using Interface = FidlMessenger;
  using WeakEventSender = fidl::internal::WireWeakEventSender<fake_ddk::FidlMessenger>;

  explicit FidlMessenger() : loop_(&kAsyncLoopConfigNeverAttachToThread) {}
  explicit FidlMessenger(const async_loop_config_t* config) : loop_(config) {}
  virtual ~FidlMessenger() {
    if (binding_)
      binding_->Unbind();
  }
  void Shutdown() { loop_.Shutdown(); }

  // Local channel to send FIDL client messages
  zx::channel& local() { return local_; }

  void Dispatch(fidl::IncomingHeaderAndMessage&& msg, ::fidl::Transaction* txn);

  // This is necessary for fidl::BindServer to work. It is usually auto-generated by LLCPP server
  // bindings. Future evolution of LLCPP may cause this code to break.
  void dispatch_message(fidl::IncomingHeaderAndMessage&& msg, ::fidl::Transaction* txn,
                        fidl::internal::MessageStorageViewBase* storage_view) final {
    Dispatch(std::move(msg), txn);
  }

  // Set handlers to be called when FIDL message is received
  // - Message operation context |op_ctx| and |op| must outlive FidlMessenger
  // - If `remote` is empty, FidlHandler will create a new channel, storing the local endpoint in
  //   `local` for the client to retrieve later and binding the remote endpoint to the server.
  //   Otherwise, the endpoint in `remote` will be bound to the server, and FidlHandler will
  //   assume the client has retained the local endpoint.
  zx_status_t SetMessageOp(void* op_ctx, MessageOp* op,
                           std::optional<zx::channel> remote = std::nullopt);

 private:
  MessageOp* message_op_ = nullptr;
  void* op_ctx_ = nullptr;
  // Channel to mimic RPC
  zx::channel local_;
  // Server binding
  std::unique_ptr<fidl::ServerBindingRef<FidlProtocol>> binding_;
  // Dispatcher for fidl messages
  async::Loop loop_;
};

}  // namespace fake_ddk

#endif  // SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FIDL_HELPER_H_
