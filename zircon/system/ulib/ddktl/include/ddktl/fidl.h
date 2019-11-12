// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_FIDL_H_
#define DDKTL_FIDL_H_

#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

#include <memory>
#include <type_traits>

namespace ddk {

class Connection {
 public:
  Connection(fidl_txn_t txn, zx_txid_t txid, uintptr_t devhost_ctx)
      : txn_(txn), txid_(txid), devhost_ctx_(devhost_ctx) {}

  fidl_txn_t* Txn() { return &txn_; }
  const fidl_txn_t* Txn() const { return &txn_; }

  zx_txid_t Txid() const { return txid_; }

  uintptr_t DevhostContext() const { return devhost_ctx_; }

  // Utilizes a |fidl_txn_t| object as a wrapped Connection.
  //
  // Only safe to call if |txn| was previously returned by |Connection.Txn()|.
  static const Connection* FromTxn(const fidl_txn_t* txn);

  // Copies txn into a new Connection.
  //
  // This may be useful for copying a Connection out of stack-allocated scope,
  // so a response may be generated asynchronously.
  //
  // Only safe to call if |txn| was previously returned by |Connection.Txn()|.
  static Connection CopyTxn(const fidl_txn_t* txn);

 private:
  fidl_txn_t txn_;
  zx_txid_t txid_;

  // Private information only for use by devhost.
  uintptr_t devhost_ctx_;
};

inline const Connection* Connection::FromTxn(const fidl_txn_t* txn) {
  static_assert(std::is_standard_layout<Connection>::value,
                "Cannot cast from non-standard layout class");
  static_assert(offsetof(Connection, txn_) == 0, "Connection must be convertable to txn");
  return reinterpret_cast<const Connection*>(txn);
}

inline Connection Connection::CopyTxn(const fidl_txn_t* txn) { return *FromTxn(txn); }

}  // namespace ddk

// TODO(surajmalhotra): Extend namespace to cover DdkTransaction.

// An implementation of |fidl::Transaction| for using LLCPP bindings in drivers,
// designed to work with ::DdkMessage.  If can be used to reply synchronously as in:
// zx_status_t DdkFidlDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
//     DdkTransaction transaction(txn);
//     fuchsia::hardware::serial::Device::Dispatch(this, msg, &transaction);
//     return transaction.Status();
// }
// void DdkFidlDevice::GetClass(GetClassCompleter::Sync completer) {
//     completer.Reply(fuchsia::hardware::serial::Class::CONSOLE);
// }
//
// And also can be used to reply asynchronously via ToAsync() call as in:
//
// zx_status_t DdkFidlDevice::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
//   DdkTransaction transaction(txn);
//   fuchsia::hardware::serial::Device::Dispatch(this, msg, &transaction);
//   return ZX_ERR_AYSNC; // Ownership of transaction was taken, can't use transaction.Status()
//   here.
// }
// void DdkFidlDevice::GetClass(GetClassCompleter::Sync completer) {
//   auto async_completer = new Wrapper(completer.ToAsync());
//   DdkScheduleWork([](void* ctx) {
//     auto* wrapper = reinterpret_cast<Wrapper*>(ctx);
//     wrapper->completer.Reply(fuchsia::hardware::serial::Class::CONSOLE);
//     delete wrapper;
//   });
// }
//
// Note that this class is not thread safe.
class DdkTransaction : public fidl::Transaction {
 public:
  explicit DdkTransaction(fidl_txn_t* txn) : connection_(ddk::Connection::CopyTxn(txn)) {}

  ~DdkTransaction() {
    ZX_ASSERT_MSG(ownership_taken_ || status_called_,
                  "Sync DdkTransaction must have it's Status() method used.\n"
                  "This provides ::DdkMessage with the correct status value.\n"
                  "If ToAsync() was called, the DdkTransaction ownership was taken and\n"
                  "Status() must not be called in ::DdkMessage\n");
  }

  /// Status() return the internal state of the DDK transaction. This MUST be called
  /// to bridge the Transaction and DDK dispatcher.
  zx_status_t Status() __WARN_UNUSED_RESULT {
    status_called_ = true;
    return status_;
  }

 protected:
  void Reply(fidl::Message msg) final {
    if (!closed_) {
      const fidl_msg_t fidl_msg{
          .bytes = msg.bytes().data(),
          .handles = msg.handles().data(),
          .num_bytes = static_cast<uint32_t>(msg.bytes().size()),
          .num_handles = static_cast<uint32_t>(msg.handles().size()),
      };

      status_ = connection_.Txn()->reply(connection_.Txn(), &fidl_msg);
    }
    msg.ClearHandlesUnsafe();
  }

  void Close(zx_status_t epitaph) final {
    closed_ = true;
    status_ = epitaph;
  }

  std::unique_ptr<Transaction> TakeOwnership() final {
    ownership_taken_ = true;
    return std::make_unique<DdkTransaction>(std::move(*this));
  }

 private:
  ddk::Connection connection_;  // includes a fidl_txn_t.
  zx_status_t status_ = ZX_OK;
  bool closed_ = false;
  bool status_called_ = false;
  bool ownership_taken_ = false;
};

#endif  // DDKTL_FIDL_H_
