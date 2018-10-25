// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include "device-resources.h"
#include "proxy-protocol.h"

// This class, along with PlatformProxyDevice, represent a platform device.
// Platform devices run in separate devhosts than the platform bus driver.
// PlatformDevice exists in the platform bus devhost and PlatformProxyDevice
// exists in the platform device's devhost. PlatformProxyDevice proxys
// requests from the platform device via a channel, which are then
// handled by PlatformDevice::DdkRxrpc and then handled by relevant Rpc* methods.
//
// Resource handles passed to the proxy to allow it to access MMIOs and interrupts.
// This ensures if the proxy driver dies we will release their address space resources
// back to the kernel if necessary.

namespace platform_bus {

class PlatformBus;

class PlatformDevice;
using PlatformDeviceType = ddk::Device<PlatformDevice, ddk::Rxrpcable>;

// This class represents a platform device attached to the platform bus.
// Instances of this class are created by PlatformBus at boot time when the board driver
// calls the platform bus protocol method pbus_device_add().

class PlatformDevice : public PlatformDeviceType {
public:
    // Creates a new PlatformDevice instance.
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    static zx_status_t Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                              fbl::unique_ptr<platform_bus::PlatformDevice>* out);

    inline uint32_t vid() const { return vid_; }
    inline uint32_t pid() const { return pid_; }
    inline uint32_t did() const { return did_; }

    // Device protocol implementation.
    void DdkRelease();
    zx_status_t DdkRxrpc(zx_handle_t channel);

    // Starts the underlying devmgr device.
    zx_status_t Start();

private:
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    explicit PlatformDevice(zx_device_t* parent, PlatformBus* bus, const pbus_dev_t* pdev);
    zx_status_t Init(const pbus_dev_t* pdev);

    // Handlers for RPCs from PlatformProxy.
    zx_status_t RpcGetMmio(const DeviceResources* dr, uint32_t index, zx_paddr_t* out_paddr,
                           size_t* out_length, zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGetInterrupt(const DeviceResources* dr, uint32_t index, uint32_t* out_irq,
                                uint32_t* out_mode, zx_handle_t* out_handle,
                                uint32_t* out_handle_count);
    zx_status_t RpcGetBti(const DeviceResources* dr, uint32_t index, zx_handle_t* out_handle,
                          uint32_t* out_handle_count);
    zx_status_t RpcGetSmc(const DeviceResources* dr, uint32_t index,
                          zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGetDeviceInfo(const DeviceResources* dr, pdev_device_info_t* out_info);
    zx_status_t RpcDeviceAdd(const DeviceResources* dr, uint32_t index, uint32_t* out_device_id);
    zx_status_t RpcGetMetadata(const DeviceResources* dr, uint32_t index, uint32_t* out_type,
                               uint8_t* buf, uint32_t buf_size, uint32_t* actual);
    zx_status_t RpcGetProtocols(const DeviceResources* dr, uint32_t* out_protocols,
                                uint32_t* out_protocol_count);
    zx_status_t RpcGpioConfigIn(const DeviceResources* dr, uint32_t index, uint32_t flags);
    zx_status_t RpcGpioConfigOut(const DeviceResources* dr, uint32_t index, uint8_t initial_value);
    zx_status_t RpcGpioSetAltFunction(const DeviceResources* dr, uint32_t index, uint64_t function);
    zx_status_t RpcGpioRead(const DeviceResources* dr, uint32_t index, uint8_t* out_value);
    zx_status_t RpcGpioWrite(const DeviceResources* dr, uint32_t index, uint8_t value);
    zx_status_t RpcGpioGetInterrupt(const DeviceResources* dr, uint32_t index, uint32_t flags,
                                    zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGpioReleaseInterrupt(const DeviceResources* dr, uint32_t index);
    zx_status_t RpcGpioSetPolarity(const DeviceResources* dr, uint32_t index, uint32_t flags);
    zx_status_t RpcI2cTransact(const DeviceResources* dr, uint32_t txid, rpc_i2c_req_t* req,
                               zx_handle_t channel);
    zx_status_t RpcI2cGetMaxTransferSize(const DeviceResources* dr, uint32_t index,
                                         size_t* out_size);
    zx_status_t RpcClkEnable(const DeviceResources* dr, uint32_t index);
    zx_status_t RpcClkDisable(const DeviceResources* dr, uint32_t index);

    PlatformBus* bus_;
    char name_[ZX_DEVICE_NAME_MAX + 1];
    const uint32_t vid_;
    const uint32_t pid_;
    const uint32_t did_;

    // Tree of platform bus resources for this device and its children.
    DeviceResources resource_tree_;

    // Flattened list of DeviceResources, indexed by device ID.
    // device_index_[0] returns the DeviceResources for this top level device.
    fbl::Vector<const DeviceResources*> device_index_;
};

} // namespace platform_bus
