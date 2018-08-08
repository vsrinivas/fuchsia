// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform-proxy-device.h"

#include <ddktl/device.h>
#include <ddktl/protocol/canvas.h>
#include <ddktl/protocol/clk.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/platform-device.h>
#include <ddktl/protocol/scpi.h>
#include <ddktl/protocol/usb-mode-switch.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy;
class ProxyDevice;
using ProxyDeviceType = ddk::FullDevice<ProxyDevice>;

class ProxyDevice : public ProxyDeviceType, public ddk::PlatformDevProtocol<ProxyDevice>,
                    public ddk::CanvasProtocol<ProxyDevice>, public ddk::ClkProtocol<ProxyDevice>,
                    public ddk::GpioProtocol<ProxyDevice>, public ddk::I2cProtocol<ProxyDevice>,
                    public ddk::ScpiProtocol<ProxyDevice>, public ddk::UmsProtocol<ProxyDevice> {
public:
    static zx_status_t Create(zx_device_t* parent, uint32_t device_id,
                              fbl::RefPtr<PlatformProxy> proxy, device_add_args_t* args);

    // Full device protocol implementation.
    // For child devices, these call through to the device protocol passed via pdev_device_add().
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkOpenAt(zx_device_t** dev_out,  const char* path, uint32_t flags);
    zx_status_t DdkClose(uint32_t flags);
    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_status_t DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual);
    zx_off_t DdkGetSize();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* actual);
    zx_status_t DdkSuspend(uint32_t flags);
    zx_status_t DdkResume(uint32_t flags);
    zx_status_t DdkRxrpc(zx_handle_t channel);

    // Platform device protocol implementation.
    zx_status_t MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                        zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t GetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t DeviceAdd(uint32_t index, device_add_args_t* args, zx_device_t** out);

    // Canvas protocol implementation.
    zx_status_t CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
                             uint8_t* canvas_idx);
    zx_status_t CanvasFree(uint8_t canvas_idx);

    // Clock protocol implementation.
    zx_status_t ClkEnable(uint32_t index);
    zx_status_t ClkDisable(uint32_t index);

    // GPIO protocol implementation.
    zx_status_t GpioConfig(uint32_t index, uint32_t flags);
    zx_status_t GpioSetAltFunction(uint32_t index, uint64_t function);
    zx_status_t GpioRead(uint32_t index, uint8_t* out_value);
    zx_status_t GpioWrite(uint32_t index, uint8_t value);
    zx_status_t GpioGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GpioReleaseInterrupt(uint32_t index);
    zx_status_t GpioSetPolarity(uint32_t index, uint32_t polarity);

    // I2C protocol implementation.
    zx_status_t I2cTransact(uint32_t index, const void* write_buf, size_t write_length,
                            size_t read_length, i2c_complete_cb complete_cb, void* cookie);
    zx_status_t I2cGetMaxTransferSize(uint32_t index, size_t* out_size);

    // SCPI protocol implementation.
    zx_status_t ScpiGetSensor(const char* name, uint32_t* sensor_id);
    zx_status_t ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value);
    zx_status_t ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps);
    zx_status_t ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx);
    zx_status_t ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx);

    // USB mode switch protocol implementation.
    zx_status_t UmsSetMode(usb_mode_t mode);

private:
    struct Mmio {
        zx_paddr_t base;
        size_t length;
        zx::handle resource;
    };
    struct Irq {
        uint32_t irq;
        // ZX_INTERRUPT_MODE_* flags
        uint32_t mode;
        zx::handle resource;
    };

    explicit ProxyDevice(zx_device_t* parent, uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy)
        : ProxyDeviceType(parent), device_id_(device_id), proxy_(proxy) {}

    zx_status_t Init(device_add_args_t* args);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ProxyDevice);

    uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
    fbl::Vector<Mmio> mmios_;
    fbl::Vector<Irq> irqs_;
    char name_[ZX_MAX_NAME_LEN];

    // These fields are saved values from the device_add_args_t passed to pdev_device_add().
    // These are unused for top level devices created via pbus_device_add().
    void* ctx_ = nullptr;
    zx_protocol_device_t* device_ops_ = nullptr;
    uint32_t proto_id_ = 0;
    void* proto_ops_ = nullptr;
};

} // namespace platform_bus
