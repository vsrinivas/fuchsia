// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVFS_VNODE_H_
#define SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVFS_VNODE_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <lib/fidl/llcpp/transaction.h>

#include <variant>

#include <ddktl/fidl.h>

#include "src/lib/storage/vfs/cpp/vnode.h"

std::variant<fidl::Transaction*, std::unique_ptr<fidl::Transaction>> FromDdkInternalTransaction(
    ddk::internal::Transaction* txn);
ddk::internal::Transaction MakeDdkInternalTransaction(fidl::Transaction* txn);
ddk::internal::Transaction MakeDdkInternalTransaction(std::unique_ptr<fidl::Transaction> txn);

class DevfsVnode : public fs::Vnode, public fidl::WireServer<fuchsia_device::Controller> {
 public:
  // Create a DevfsVnode. `dev` is unowned, so the Device must outlive the Vnode.
  explicit DevfsVnode(zx_device* dev) : dev_(dev) {}

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
  void SetPerformanceState(SetPerformanceStateRequestView request,
                           SetPerformanceStateCompleter::Sync& _completer) override;

 private:
  // A pointer to the device that this vnode represents. This will be
  // set to nullptr if the device is freed
  zx_device* dev_;
};

#endif  // SRC_DEVICES_MISC_DRIVERS_COMPAT_DEVFS_VNODE_H_
