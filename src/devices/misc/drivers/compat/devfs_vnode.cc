// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "devfs_vnode.h"

#include <lib/ddk/device.h>

#include <string_view>

#include <fbl/string_buffer.h>

#include "device.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

namespace {

// Bitmask for checking if a pointer stashed in a ddk::internal::Transaction is from the heap or
// not. This is safe to use on our pointers, because fidl::Transactions are always aligned to more
// than one byte.
static_assert(alignof(fidl::Transaction) > 1);
constexpr uintptr_t kTransactionIsBoxed = 0x1;

// Reply originating from driver.
zx_status_t DdkReply(fidl_txn_t* txn, const fidl_outgoing_msg_t* msg) {
  auto message = fidl::OutgoingMessage::FromEncodedCMessage(msg);
  // If FromDdkInternalTransaction returns a unique_ptr variant, it will be destroyed when exiting
  // this scope.
  auto fidl_txn = FromDdkInternalTransaction(ddk::internal::Transaction::FromTxn(txn));
  std::visit([&](auto&& arg) { arg->Reply(&message); }, fidl_txn);
  return ZX_OK;
}

}  // namespace

std::variant<fidl::Transaction*, std::unique_ptr<fidl::Transaction>> FromDdkInternalTransaction(
    ddk::internal::Transaction* txn) {
  uintptr_t raw = txn->DriverHostCtx();
  ZX_ASSERT_MSG(raw != 0, "Reused a fidl_txn_t!\n");

  // Invalidate the source transaction
  txn->DeviceFidlTxn()->driver_host_context = 0;

  auto ptr = reinterpret_cast<fidl::Transaction*>(raw & ~kTransactionIsBoxed);
  if (raw & kTransactionIsBoxed) {
    return std::unique_ptr<fidl::Transaction>(ptr);
  }
  return ptr;
}

ddk::internal::Transaction MakeDdkInternalTransaction(fidl::Transaction* txn) {
  device_fidl_txn_t fidl_txn = {};
  fidl_txn.txn = {
      .reply = DdkReply,
  };
  fidl_txn.driver_host_context = reinterpret_cast<uintptr_t>(txn);
  return ddk::internal::Transaction(fidl_txn);
}

ddk::internal::Transaction MakeDdkInternalTransaction(std::unique_ptr<fidl::Transaction> txn) {
  device_fidl_txn_t fidl_txn = {};
  fidl_txn.txn = {
      .reply = DdkReply,
  };
  fidl_txn.driver_host_context = reinterpret_cast<uintptr_t>(txn.release()) | kTransactionIsBoxed;
  return ddk::internal::Transaction(fidl_txn);
}

zx_status_t DevfsVnode::GetAttributes(fs::VnodeAttributes* a) {
  a->mode = V_TYPE_CDEV | V_IRUSR | V_IWUSR;
  a->content_size = 0;
  a->link_count = 1;
  return ZX_OK;
}

fs::VnodeProtocolSet DevfsVnode::GetProtocols() const { return fs::VnodeProtocol::kDevice; }

zx_status_t DevfsVnode::GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                               fs::VnodeRepresentation* info) {
  if (protocol == fs::VnodeProtocol::kDevice) {
    *info = fs::VnodeRepresentation::Device{};
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

void DevfsVnode::HandleFsSpecificMessage(fidl::IncomingMessage& msg, fidl::Transaction* txn) {
  ::fidl::DispatchResult dispatch_result =
      fidl::WireTryDispatch<fuchsia_device::Controller>(this, msg, txn);
  if (dispatch_result == ::fidl::DispatchResult::kFound) {
    return;
  }

  fidl_incoming_msg_t c_msg = std::move(msg).ReleaseToEncodedCMessage();
  auto ddk_txn = MakeDdkInternalTransaction(txn);
  zx_status_t status = dev_->MessageOp(&c_msg, ddk_txn.Txn());
  if (status != ZX_OK && status != ZX_ERR_ASYNC) {
    // Close the connection on any error
    txn->Close(status);
  }
}

zx_status_t DevfsVnode::Read(void* data, size_t len, size_t off, size_t* out_actual) {
  return dev_->ReadOp(data, len, off, out_actual);
}
zx_status_t DevfsVnode::Write(const void* data, size_t len, size_t off, size_t* out_actual) {
  return dev_->WriteOp(data, len, off, out_actual);
}

void DevfsVnode::Bind(BindRequestView request, BindCompleter::Sync& completer) {
  if (dev_->HasChildren()) {
    // A DFv1 driver will add a child device once it's bound. If the device has any children, refuse
    // the Bind() call.
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }
  auto async = completer.ToAsync();
  auto promise =
      dev_->RebindToLibname(std::string_view{request->driver.data(), request->driver.size()})
          .then(
              [completer = std::move(async)](fpromise::result<void, zx_status_t>& result) mutable {
                if (result.is_ok()) {
                  completer.ReplySuccess();
                } else {
                  completer.ReplyError(result.take_error());
                }
              });

  dev_->executor().schedule_task(std::move(promise));
}

void DevfsVnode::GetCurrentPerformanceState(GetCurrentPerformanceStateRequestView request,
                                            GetCurrentPerformanceStateCompleter::Sync& completer) {
  completer.Reply(0);
}

void DevfsVnode::Rebind(RebindRequestView request, RebindCompleter::Sync& completer) {
  auto async = completer.ToAsync();
  auto promise =
      dev_->RebindToLibname(std::string_view{request->driver.data(), request->driver.size()})
          .then(
              [completer = std::move(async)](fpromise::result<void, zx_status_t>& result) mutable {
                if (result.is_ok()) {
                  completer.ReplySuccess();
                } else {
                  completer.ReplyError(result.take_error());
                }
              });

  dev_->executor().schedule_task(std::move(promise));
}

void DevfsVnode::UnbindChildren(UnbindChildrenRequestView request,
                                UnbindChildrenCompleter::Sync& completer) {
  completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
}

void DevfsVnode::ScheduleUnbind(ScheduleUnbindRequestView request,
                                ScheduleUnbindCompleter::Sync& completer) {
  dev_->Remove();
  completer.ReplySuccess();
}

void DevfsVnode::GetTopologicalPath(GetTopologicalPathRequestView request,
                                    GetTopologicalPathCompleter::Sync& completer) {
  std::string path("/dev/");
  path.append(dev_->topological_path());
  completer.ReplySuccess(fidl::StringView::FromExternal(path));
}

void DevfsVnode::GetMinDriverLogSeverity(GetMinDriverLogSeverityRequestView request,
                                         GetMinDriverLogSeverityCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}

void DevfsVnode::SetMinDriverLogSeverity(SetMinDriverLogSeverityRequestView request,
                                         SetMinDriverLogSeverityCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED);
}

void DevfsVnode::SetPerformanceState(SetPerformanceStateRequestView request,
                                     SetPerformanceStateCompleter::Sync& completer) {
  completer.Reply(ZX_ERR_NOT_SUPPORTED, 0);
}
