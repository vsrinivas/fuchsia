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
using ProxyDeviceType = ddk::Device<ProxyDevice, ddk::GetProtocolable>;

class ProxyDevice : public ProxyDeviceType, public ddk::PlatformDevProtocol<ProxyDevice>,
                    public ddk::CanvasProtocol<ProxyDevice>, public ddk::ClkProtocol<ProxyDevice>,
                    public ddk::GpioProtocol<ProxyDevice>, public ddk::I2cProtocol<ProxyDevice>,
                    public ddk::ScpiProtocol<ProxyDevice>, public ddk::UmsProtocol<ProxyDevice> {
public:
    static zx_status_t Create(zx_device_t* parent, fbl::RefPtr<PlatformProxy> proxy);

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();

    // Platform device protocol implementation.
    zx_status_t MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr, size_t* out_size,
                        zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t GetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t GetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t GetBoardInfo(pdev_board_info_t* out_info);

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

    explicit ProxyDevice(zx_device_t* parent, fbl::RefPtr<PlatformProxy> proxy)
        : ProxyDeviceType(parent), proxy_(proxy) {}

    zx_status_t Init();

    DISALLOW_COPY_ASSIGN_AND_MOVE(ProxyDevice);

    fbl::RefPtr<PlatformProxy> proxy_;
    fbl::Vector<Mmio> mmios_;
    fbl::Vector<Irq> irqs_;
    char name_[ZX_MAX_NAME_LEN];
};

} // namespace platform_bus
