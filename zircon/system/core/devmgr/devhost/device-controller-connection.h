// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <fuchsia/device/manager/c/fidl.h>
#include <fuchsia/io/c/fidl.h>
#include <lib/zx/channel.h>
#include "../shared/async-loop-owned-rpc-handler.h"

struct zx_device;

namespace devmgr {

class DeviceControllerConnection : public AsyncLoopOwnedRpcHandler<DeviceControllerConnection> {
public:
    DeviceControllerConnection(fbl::RefPtr<zx_device> dev, zx::channel rpc,
                               const fuchsia_device_manager_DeviceController_ops_t* device_fidl_ops,
                               const fuchsia_io_Directory_ops_t* directory_fidl_ops);

    static zx_status_t Create(fbl::RefPtr<zx_device> dev, zx::channel rpc,
                              std::unique_ptr<DeviceControllerConnection>* conn);
    static zx_status_t Create(fbl::RefPtr<zx_device> dev, zx::channel rpc,
                              const fuchsia_device_manager_DeviceController_ops_t* device_fidl_ops,
                              const fuchsia_io_Directory_ops_t* directory_fidl_ops,
                              std::unique_ptr<DeviceControllerConnection>* conn);

    ~DeviceControllerConnection();

    static void HandleRpc(std::unique_ptr<DeviceControllerConnection> conn,
                          async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                          const zx_packet_signal_t* signal);
    zx_status_t HandleRead();

    const fbl::RefPtr<zx_device>& dev() const { return dev_; }
private:
    const fbl::RefPtr<zx_device> dev_;

    const fuchsia_device_manager_DeviceController_ops_t* device_fidl_ops_;
    const fuchsia_io_Directory_ops_t* directory_fidl_ops_;
};

} // namespace devmgr

