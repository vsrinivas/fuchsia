// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
#define SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_

#include <fuchsia/device/manager/llcpp/fidl.h>
#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/io2/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include <fbl/ref_ptr.h>

#include "async_loop_owned_rpc_handler.h"

class DriverHostContext;
struct zx_device;

class DeviceControllerConnection
    : public AsyncLoopOwnedRpcHandler<DeviceControllerConnection>,
      public fidl::WireServer<fuchsia_device_manager::DeviceController> {
 public:
  // |ctx| must outlive this connection
  DeviceControllerConnection(DriverHostContext* ctx, fbl::RefPtr<zx_device> dev,
                             fidl::ServerEnd<fuchsia_device_manager::DeviceController> rpc,
                             fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client);

  // |ctx| must outlive this connection
  static zx_status_t Create(DriverHostContext* ctx, fbl::RefPtr<zx_device> dev, zx::channel rpc,
                            fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client,
                            std::unique_ptr<DeviceControllerConnection>* conn);

  ~DeviceControllerConnection();

  static void HandleRpc(std::unique_ptr<DeviceControllerConnection> conn,
                        async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);
  zx_status_t HandleRead();

  const fbl::RefPtr<zx_device>& dev() const { return dev_; }

 protected:
  DriverHostContext* const driver_host_context_;
  const fbl::RefPtr<zx_device> dev_;

 private:
  fidl::Client<fuchsia_device_manager::Coordinator> coordinator_client_;
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
  void CompleteCompatibilityTests(CompleteCompatibilityTestsRequestView request,
                                  CompleteCompatibilityTestsCompleter::Sync& _completer) override;
  void Open(OpenRequestView request, OpenCompleter::Sync& _completer) override;
};

struct DevhostRpcReadContext {
  const char* path;
  DeviceControllerConnection* conn;
};

#endif  // SRC_DEVICES_BIN_DRIVER_HOST_DEVICE_CONTROLLER_CONNECTION_H_
