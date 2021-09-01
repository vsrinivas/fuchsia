// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_

#include <fidl/fuchsia.device.manager/cpp/wire.h>

#include <fbl/ref_ptr.h>

class DriverHostContext;
struct zx_device;

class DeviceControllerConnection
    : public fidl::WireServer<fuchsia_device_manager::DeviceController> {
 public:
  // |ctx| must outlive this connection
  DeviceControllerConnection(
      DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
      fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client);

  // |ctx| must outlive this connection
  static std::unique_ptr<DeviceControllerConnection> Create(
      DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
      fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client);

  static void Bind(std::unique_ptr<DeviceControllerConnection> conn,
                   fidl::ServerEnd<fuchsia_device_manager::DeviceController> request,
                   async_dispatcher_t* dispatcher);

  const fbl::RefPtr<zx_device>& dev() const { return dev_; }

 protected:
  DriverHostContext* const driver_host_context_;
  const fbl::RefPtr<zx_device> dev_;

 private:
  fidl::WireSharedClient<fuchsia_device_manager::Coordinator> coordinator_client_;

  // Fidl methods
  void BindDriver(BindDriverRequestView request, BindDriverCompleter::Sync& _completer) override;
  void ConnectProxy(ConnectProxyRequestView request,
                    ConnectProxyCompleter::Sync& _completer) override;
  void Init(InitRequestView request, InitCompleter::Sync& _completer) override;
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& _completer) override;
  void Resume(ResumeRequestView request, ResumeCompleter::Sync& _completer) override;
  void Unbind(UnbindRequestView request, UnbindCompleter::Sync& _completer) override;
  void CompleteRemoval(CompleteRemovalRequestView request,
                       CompleteRemovalCompleter::Sync& _completer) override;
  void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
