// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_DRIVER_HOST_DEVFS_CONNECTION_H_
#define SRC_DEVICES_DRIVER_HOST_DEVFS_CONNECTION_H_

#include <fuchsia/device/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/limits.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <stdint.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

#include <type_traits>

#include <ddktl/fidl.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

#include "async-loop-ref-counted-rpc-handler.h"
#include "zircon/assert.h"

typedef struct zx_device zx_device_t;

namespace devmgr {

class Connection;

// callback to process a FIDL message.
// - |msg| is a decoded FIDL message.
// - return value of ERR_DISPATCHER_{INDIRECT,ASYNC} indicates that the reply is
//   being handled by the callback (forwarded to another server, sent later,
//   etc, and no reply message should be sent).
// - WARNING: Once this callback returns, usage of |msg| is no longer
//   valid. If a client transmits ERR_DISPATCHER_{INDIRECT,ASYNC}, and intends
//   to respond asynchronously, they must copy the fields of |msg| they
//   wish to use at a later point in time.
// - otherwise, the return value is treated as the status to send
//   in the rpc response, and msg.len indicates how much valid data
//   to send.  On error return msg.len will be set to 0.
using FidlDispatchFunction = fit::callback<zx_status_t(fidl_msg_t* msg, Connection* txn)>;

class DevfsConnection : public fbl::RefCounted<DevfsConnection>,
                        public AsyncLoopRefCountedRpcHandler<DevfsConnection>,
                        public llcpp::fuchsia::device::Controller::Interface {
 public:
  DevfsConnection() = default;

  static void HandleRpc(fbl::RefPtr<DevfsConnection>&& conn, async_dispatcher_t* dispatcher,
                        async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

  fbl::RefPtr<zx_device_t> dev;
  size_t io_off = 0;
  uint32_t flags = 0;
  zx_txid_t last_txid = 0;
  bool reply_called = false;

 private:
  // Attempts to read and dispatch a FIDL message.
  //
  // If a message cannot be read, returns an error instead of blocking.
  zx_status_t ReadMessage(FidlDispatchFunction dispatch);

  // Synthesizes a FIDL close message.
  //
  // This may be invoked when a channel is closed, to simulate dispatching
  // to the same close function.
  zx_status_t CloseMessage(FidlDispatchFunction dispatch);

  void Bind(::fidl::StringView driver, BindCompleter::Sync _completer) override;
  void Rebind(::fidl::StringView driver, RebindCompleter::Sync _completer) override;
  void UnbindChildren(UnbindChildrenCompleter::Sync completer) override;
  void ScheduleUnbind(ScheduleUnbindCompleter::Sync _completer) override;
  void GetDriverName(GetDriverNameCompleter::Sync _completer) override;
  void GetDeviceName(GetDeviceNameCompleter::Sync _completer) override;
  void GetTopologicalPath(GetTopologicalPathCompleter::Sync _completer) override;
  void GetEventHandle(GetEventHandleCompleter::Sync _completer) override;
  void GetDriverLogFlags(GetDriverLogFlagsCompleter::Sync _completer) override;
  void GetDevicePerformanceStates(GetDevicePerformanceStatesCompleter::Sync completer) override;
  void GetCurrentPerformanceState(GetCurrentPerformanceStateCompleter::Sync completer) override;
  void SetDriverLogFlags(uint32_t clear_flags, uint32_t set_flags,
                         SetDriverLogFlagsCompleter::Sync _completer) override;
  void RunCompatibilityTests(int64_t hook_wait_time,
                             RunCompatibilityTestsCompleter::Sync _completer) override;
  void GetDevicePowerCaps(GetDevicePowerCapsCompleter::Sync _completer) override;
  void SetPerformanceState(uint32_t requested_state,
                           SetPerformanceStateCompleter::Sync _completer) override;
  void ConfigureAutoSuspend(bool enable, ::llcpp::fuchsia::device::DevicePowerState requested_state,
                            ConfigureAutoSuspendCompleter::Sync _completer) override;

  void UpdatePowerStateMapping(
      ::fidl::Array<::llcpp::fuchsia::device::SystemPowerStateInfo, 7> mapping,
      UpdatePowerStateMappingCompleter::Sync _completer) override;
  void GetPowerStateMapping(GetPowerStateMappingCompleter::Sync _completer) override;
  void Suspend(::llcpp::fuchsia::device::DevicePowerState requested_state,
               SuspendCompleter::Sync _completer) override;
  void Resume(ResumeCompleter::Sync _complete) override;
};

class Connection {
 public:
  Connection(fidl_txn_t txn, zx_txid_t txid, fbl::RefPtr<DevfsConnection> conn);

  explicit Connection(const ddk::Connection* conn);

  Connection(const Connection& other);
  Connection& operator=(const Connection& other);

  ~Connection();

  fidl_txn_t* Txn() { return &txn_; }

  zx_txid_t Txid() const { return txid_; }

  zx::unowned_channel channel() const {
    ZX_ASSERT_MSG(conn_.get() != nullptr, "Can't get channel =(");
    return conn_->channel();
  }

  DevfsConnection* devfs_connection() { return conn_.get(); }

  // Consumes self.
  ddk::Connection ToDdkConnection();

  // Utilizes a |fidl_txn_t| object as a wrapped Connection.
  //
  // Only safe to call if |txn| was previously returned by |Connection.Txn()|.
  static Connection* FromTxn(fidl_txn_t* txn);

  // Copies txn into a new Connection.
  //
  // This may be useful for copying a Connection out of stack-allocated scope,
  // so a response may be generated asynchronously.
  //
  // Only safe to call if |txn| was previously returned by |Connection.Txn()|.
  static Connection CopyTxn(fidl_txn_t* txn);

 private:
  fidl_txn_t txn_;
  zx_txid_t txid_;

  fbl::RefPtr<DevfsConnection> conn_ = nullptr;
};

inline Connection* Connection::FromTxn(fidl_txn_t* txn) {
  static_assert(std::is_standard_layout<Connection>::value,
                "Cannot cast from non-standard layout class");
  static_assert(offsetof(Connection, txn_) == 0, "Connection must be convertable to txn");
  return reinterpret_cast<Connection*>(txn);
}

inline Connection Connection::CopyTxn(fidl_txn_t* txn) { return *FromTxn(txn); }

class Transaction : public fidl::Transaction {
 public:
  explicit Transaction(fidl_txn_t* txn) : conn_(Connection::CopyTxn(txn)) {}

  ~Transaction() {
    ZX_ASSERT_MSG(status_called_,
                  "Transaction must have it's Status() method used. \
            This provides ::DevhostMessage with the correct status value.\n");
  }

  /// Status() return the internal state of the transaction. This MUST be called
  /// to bridge the Transaction and dispatcher.
  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 protected:
  void Reply(fidl::Message msg) final {
    const fidl_msg_t fidl_msg{
        .bytes = msg.bytes().data(),
        .handles = msg.handles().data(),
        .num_bytes = static_cast<uint32_t>(msg.bytes().size()),
        .num_handles = static_cast<uint32_t>(msg.handles().size()),
    };

    status_ = conn_.Txn()->reply(conn_.Txn(), &fidl_msg);
    msg.ClearHandlesUnsafe();
  }

  void Close(zx_status_t close_status) final { status_ = close_status; }

  std::unique_ptr<fidl::Transaction> TakeOwnership() final {
    // |conn_| will keep the channel alive.
    status_called_ = true;
    return std::make_unique<Transaction>(*this);
  }

 private:
  devmgr::Connection conn_;
  zx_status_t status_ = ZX_OK;
  bool status_called_ = false;
};

}  // namespace devmgr

#endif  // SRC_DEVICES_DRIVER_HOST_DEVFS_CONNECTION_H_
