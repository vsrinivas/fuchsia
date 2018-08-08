// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddk/protocol/canvas.h>
#include <ddk/protocol/scpi.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <ddktl/protocol/platform-bus.h>
#include <ddktl/protocol/platform-device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>

// An overview of PlatformDevice and PlatformProxy.
//
// Both this class and PlatformProxy implement the platform device protocol.
// At this time, this protocol provides the following methods:
//     map_mmio
//     map_interrupt
//     get_bti
//     get_device_info
// The implementation in this file implements the platform device protocol for drivers that
// exist within the platform bus process. The implementation of the protocol
// in PlatformProxy is for drivers that live in their own devhost and perform
// RPC calls to the platform bus over a channel. In that case, RPC calls are
// handled by PlatformDevice::DdkRxrpc and then handled by relevant Rpc* methods.
// Any resource handles passed back to the proxy are then used to create/map mmio
// and irq objects within the proxy process. This ensures if the proxy driver dies
// we will release their address space resources back to the kernel if necessary.

typedef struct pdev_req pdev_req_t;

namespace platform_bus {

class PlatformBus;

class PlatformDevice;
using PlatformDeviceType = ddk::Device<PlatformDevice, ddk::GetProtocolable, ddk::Rxrpcable>;

// This class represents a platform device attached to the platform bus.
// Instances of this class are created by PlatformBus at boot time when the board driver
// calls the platform bus protocol method pbus_device_add().
// The PlatformDevice instances are never destroyed, but their underlying device in the devmger
// can be added and removed dynamically via pbus_device_enable() method in the platform bus protoool.

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

    // Platform device protocol implementation.
    zx_status_t MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                        zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t GetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info);

    // Adds or removes the underlying devmgr device.
    // Called in response to the pbus_device_enable() method in the platform bus protocol.
    zx_status_t Enable(bool enable);

private:
    // *flags* contains zero or more PDEV_ADD_* flags from the platform bus protocol.
    explicit PlatformDevice(zx_device_t* parent, PlatformBus* bus, uint32_t flags,
                            const pbus_dev_t* pdev);
    zx_status_t Init(const pbus_dev_t* pdev);

    // Handlers for RPCs from PlatformProxy. 
    zx_status_t RpcGetMmio(uint32_t index, zx_paddr_t* out_paddr, size_t *out_length,
                           zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGetInterrupt(uint32_t index, uint32_t* out_irq, uint32_t* out_mode,
                                zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcGetBti(uint32_t index, zx_handle_t* out_handle, uint32_t* out_handle_count);
    zx_status_t RpcUmsSetMode(usb_mode_t mode);
    zx_status_t RpcGpioConfig(uint32_t index, uint32_t flags);
    zx_status_t RpcGpioSetAltFunction(uint32_t index, uint64_t function);
    zx_status_t RpcGpioRead(uint32_t index, uint8_t* out_value);
    zx_status_t RpcGpioWrite(uint32_t index, uint8_t value);
    zx_status_t RpcGpioGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle,
                                    uint32_t* out_handle_count);
    zx_status_t RpcGpioReleaseInterrupt(uint32_t index);
    zx_status_t RpcGpioSetPolarity(uint32_t index, uint32_t flags);
    zx_status_t RpcCanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
                                uint8_t* canvas_idx);
    zx_status_t RpcCanvasFree(uint8_t canvas_idx);
    zx_status_t RpcScpiGetSensor(char* name, uint32_t *sensor_id);
    zx_status_t RpcScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value);
    zx_status_t RpcScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps);
    zx_status_t RpcScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx);
    zx_status_t RpcScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx);
    zx_status_t RpcI2cTransact(pdev_req_t* req, uint8_t* data, zx_handle_t channel);
    zx_status_t RpcI2cGetMaxTransferSize(uint32_t index, size_t* out_size);
    zx_status_t RpcClkEnable(uint32_t index);
    zx_status_t RpcDisable(uint32_t index);

    zx_status_t AddMetaData(const pbus_metadata_t& pbm);

    PlatformBus* bus_;
    char name_[ZX_DEVICE_NAME_MAX + 1];
    const uint32_t flags_;
    const uint32_t vid_;
    const uint32_t pid_;
    const uint32_t did_;
    const serial_port_info_t serial_port_info_;
    bool enabled_ = false;

    fbl::Vector<pbus_mmio_t> mmios_;
    fbl::Vector<pbus_irq_t> irqs_;
    fbl::Vector<pbus_gpio_t> gpios_;
    fbl::Vector<pbus_i2c_channel_t> i2c_channels_;
    fbl::Vector<pbus_clk_t> clks_;
    fbl::Vector<pbus_bti_t> btis_;
    fbl::Vector<pbus_metadata_t> metadata_;
};

} // namespace platform_bus
