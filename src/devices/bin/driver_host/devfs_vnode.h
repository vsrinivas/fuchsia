// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVFS_VNODE_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVFS_VNODE_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fidl/llcpp/transaction.h>

#include <variant>

#include <ddktl/fidl.h>

#include "src/lib/storage/vfs/cpp/vnode.h"
#include "zx_device.h"

class DevfsVnode : public fs::Vnode, public fidl::WireServer<fuchsia_device::Controller> {
 public:
  explicit DevfsVnode(fbl::RefPtr<zx_device> dev) : dev_(std::move(dev)) {}

  // fs::Vnode methods
  zx_status_t Read(void* data, size_t len, size_t off, size_t* out_actual) override;
  zx_status_t Write(const void* data, size_t len, size_t off, size_t* out_actual) override;

  zx_status_t GetAttributes(fs::VnodeAttributes* a) override;
  fs::VnodeProtocolSet GetProtocols() const override;
  zx_status_t GetNodeInfoForProtocol(fs::VnodeProtocol protocol, fs::Rights rights,
                                     fs::VnodeRepresentation* info) override;
  void HandleFsSpecificMessage(fidl::IncomingMessage& msg, fidl::Transaction* txn) override;

  // fidl::WireServer<fuchsia_device::Controller> methods
  void Bind(BindRequestView request, BindCompleter::Sync& _completer) override;
  void Rebind(RebindRequestView request, RebindCompleter::Sync& _completer) override;
  void UnbindChildren(UnbindChildrenRequestView request,
                      UnbindChildrenCompleter::Sync& completer) override;
  void ScheduleUnbind(ScheduleUnbindRequestView request,
                      ScheduleUnbindCompleter::Sync& _completer) override;
  void GetTopologicalPath(GetTopologicalPathRequestView request,
                          GetTopologicalPathCompleter::Sync& _completer) override;
  void GetMinDriverLogSeverity(GetMinDriverLogSeverityRequestView request,
                               GetMinDriverLogSeverityCompleter::Sync& _completer) override;
  void GetCurrentPerformanceState(GetCurrentPerformanceStateRequestView request,
                                  GetCurrentPerformanceStateCompleter::Sync& completer) override;
  void SetMinDriverLogSeverity(SetMinDriverLogSeverityRequestView request,
                               SetMinDriverLogSeverityCompleter::Sync& _completer) override;
  void RunCompatibilityTests(RunCompatibilityTestsRequestView request,
                             RunCompatibilityTestsCompleter::Sync& _completer) override;
  void SetPerformanceState(SetPerformanceStateRequestView request,
                           SetPerformanceStateCompleter::Sync& _completer) override;

 private:
  // Vnode protected implementation:
  zx_status_t OpenNode(fs::Vnode::ValidatedOptions options,
                       fbl::RefPtr<Vnode>* out_redirect) override;
  zx_status_t CloseNode() override;

  fbl::RefPtr<zx_device> dev_;
};

// Utilties for converting our fidl::Transactions to something usable by the driver C ABI
ddk::internal::Transaction MakeDdkInternalTransaction(fidl::Transaction* txn);
ddk::internal::Transaction MakeDdkInternalTransaction(std::unique_ptr<fidl::Transaction> txn);
// This returns a variant because, as an optimization, we skip performing allocations when
// synchronously processing requests. If a request has ownership taken over using
// device_fidl_transaction_take_ownership, we will perform an allocation to extend the lifetime
// of the transaction. In that case, we'll have a unique_ptr.
//
// This operation will modify its input, invalidating it.
std::variant<fidl::Transaction*, std::unique_ptr<fidl::Transaction>> FromDdkInternalTransaction(
    ddk::internal::Transaction* txn);

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVFS_VNODE_H_
