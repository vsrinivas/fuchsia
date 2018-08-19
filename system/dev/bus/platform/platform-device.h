// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddk/protocol/canvas.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <ddktl/protocol/platform-bus.h>
#include <ddktl/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

#include "device-resources.h"
#include "proxy-protocol.h"

// An overview of PlatformDevice and PlatformProxy.
//
// Both this class and PlatformProxy implement the platform device protocol.
// The implementation in this file implements the platform device protocol for drivers that
// exist within the platform bus process. The implementation of the protocol
// in PlatformProxy is for drivers that live in their own devhost and perform
// RPC calls to the platform bus over a channel. In that case, RPC calls are
// handled by PlatformDevice::DdkRxrpc and then handled by relevant Rpc* methods.
// Any resource handles passed back to the proxy are then used to create/map mmio
// and irq objects within the proxy process. This ensures if the proxy driver dies
// we will release their address space resources back to the kernel if necessary.

namespace platform_bus {

class PlatformBus;

class PlatformDevice;
using PlatformDeviceType = ddk::Device<PlatformDevice, ddk::GetProtocolable, ddk::Rxrpcable>;

// This class represents a platform device attached to the platform bus.
// Instances of this class are created by PlatformBus at boot time when the board driver
// calls the platform bus protocol method pbus_device_add().

class PlatformDevice : public PlatformDeviceType, public ddk::PlatformDevProtocol<PlatformDevice> {
public:
    // Creates a new PlatformDevice instance.
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    static zx_status_t Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                              uint32_t flags, fbl::unique_ptr<platform_bus::PlatformDevice>* out);

    inline uint32_t vid() const { return vid_; }
    inline uint32_t pid() const { return pid_; }
    inline uint32_t did() const { return did_; }

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();
    zx_status_t DdkRxrpc(zx_handle_t channel);

    // Platform device protocol implementation for devices running in the platform bus devhost.
    zx_status_t MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                        zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t GetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t DeviceAdd(uint32_t index, device_add_args_t* args, zx_device_t** out);

    // Common GetDeviceInfo implementation, used for both devices in the proxying case
    // and those running in the platform bus devhost.
    zx_status_t GetDeviceInfo(const DeviceResources* dr, pdev_device_info_t* out_info);

    // Starts the underlying devmgr device.
    zx_status_t Start();

private:
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    explicit PlatformDevice(zx_device_t* parent, PlatformBus* bus, uint32_t flags,
                            const pbus_dev_t* pdev);
    zx_status_t Init(const pbus_dev_t* pdev);

    // Handlers for RPCs from PlatformProxy.
    zx_status_t RpcGetMmio(const DeviceResources* dr, uint32_t index, zx_paddr_t* out_paddr,
                           size_t *out_length, zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGetInterrupt(const DeviceResources* dr, uint32_t index, uint32_t* out_irq,
                                uint32_t* out_mode, zx_handle_t* out_handle,
                                uint32_t* out_handle_count);
    zx_status_t RpcGetBti(const DeviceResources* dr, uint32_t index, zx_handle_t* out_handle,
                          uint32_t* out_handle_count);
    zx_status_t RpcDeviceAdd(const DeviceResources* dr, uint32_t index, uint32_t* out_device_id);
    zx_status_t RpcGetMetadata(const DeviceResources* dr, uint32_t index, uint32_t* out_type,
                               uint8_t* buf, uint32_t buf_size, uint32_t* actual);
    zx_status_t RpcUmsSetMode(const DeviceResources* dr, usb_mode_t mode);
    zx_status_t RpcGpioConfig(const DeviceResources* dr, uint32_t index, uint32_t flags);
    zx_status_t RpcGpioSetAltFunction(const DeviceResources* dr, uint32_t index, uint64_t function);
    zx_status_t RpcGpioRead(const DeviceResources* dr, uint32_t index, uint8_t* out_value);
    zx_status_t RpcGpioWrite(const DeviceResources* dr, uint32_t index, uint8_t value);
    zx_status_t RpcGpioGetInterrupt(const DeviceResources* dr, uint32_t index, uint32_t flags,
                                    zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGpioReleaseInterrupt(const DeviceResources* dr, uint32_t index);
    zx_status_t RpcGpioSetPolarity(const DeviceResources* dr, uint32_t index, uint32_t flags);
    zx_status_t RpcCanvasConfig(const DeviceResources* dr, zx_handle_t vmo, size_t offset,
                                canvas_info_t* info, uint8_t* canvas_idx);
    zx_status_t RpcCanvasFree(const DeviceResources* dr, uint8_t canvas_idx);
    zx_status_t RpcI2cTransact(const DeviceResources* dr, uint32_t txid, rpc_i2c_req_t* req,
                               uint8_t* data, zx_handle_t channel);
    zx_status_t RpcI2cGetMaxTransferSize(const DeviceResources* dr, uint32_t index,
                                         size_t* out_size);
    zx_status_t RpcClkEnable(const DeviceResources* dr, uint32_t index);
    zx_status_t RpcClkDisable(const DeviceResources* dr, uint32_t index);

    zx_status_t GetZbiMetadata(uint32_t type, uint32_t extra, const void** out_metadata,
                               uint32_t* out_size);

    PlatformBus* bus_;
    char name_[ZX_DEVICE_NAME_MAX + 1];
    const uint32_t flags_;
    const uint32_t vid_;
    const uint32_t pid_;
    const uint32_t did_;
    const serial_port_info_t serial_port_info_;

    // Tree of platform bus resources for this device and its children.
    DeviceResources resource_tree_;

    // Flattened list of DeviceResources, indexed by device ID.
    // device_index_[0] returns the DeviceResources for this top level device.
    fbl::Vector<const DeviceResources*> device_index_;
};

} // namespace platform_bus
