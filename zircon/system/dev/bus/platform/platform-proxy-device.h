// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform-proxy-device.h"

#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/platform/device.h>
#include <ddktl/protocol/sysmem.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy;

class ProxyGpio : public ddk::GpioProtocol<ProxyGpio> {
public:
    explicit ProxyGpio(uint32_t device_id, uint32_t index, fbl::RefPtr<PlatformProxy> proxy)
        : device_id_(device_id), index_(index), proxy_(proxy) {}

    // GPIO protocol implementation.
    zx_status_t GpioConfigIn(uint32_t flags);
    zx_status_t GpioConfigOut(uint8_t initial_value);
    zx_status_t GpioSetAltFunction(uint64_t function);
    zx_status_t GpioRead(uint8_t* out_value);
    zx_status_t GpioWrite(uint8_t value);
    zx_status_t GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq);
    zx_status_t GpioReleaseInterrupt();
    zx_status_t GpioSetPolarity(uint32_t polarity);

    void GetProtocol(gpio_protocol_t* proto) {
        proto->ops = &gpio_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t device_id_;
    uint32_t index_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

class ProxyI2c : public ddk::I2cProtocol<ProxyI2c> {
public:
    explicit ProxyI2c(uint32_t device_id, uint32_t index, fbl::RefPtr<PlatformProxy> proxy)
        : device_id_(device_id), index_(index), proxy_(proxy) {}

    // I2C protocol implementation.
    void I2cTransact(const i2c_op_t* ops, size_t cnt, i2c_transact_callback transact_cb,
                     void* cookie);
    zx_status_t I2cGetMaxTransferSize(size_t* out_size);
    zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq);

    void GetProtocol(i2c_protocol_t* proto) {
        proto->ops = &i2c_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t device_id_;
    uint32_t index_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

class ProxyClock : public ddk::ClockProtocol<ProxyClock> {
public:
    explicit ProxyClock(uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy)
        : device_id_(device_id), proxy_(proxy) {}

    // Clock protocol implementation.
    zx_status_t ClockEnable(uint32_t index);
    zx_status_t ClockDisable(uint32_t index);

    void GetProtocol(clock_protocol_t* proto) {
        proto->ops = &clock_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

class ProxySysmem : public ddk::SysmemProtocol<ProxySysmem> {
public:
    explicit ProxySysmem(uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy)
        : device_id_(device_id), proxy_(proxy) {}

    // Sysmem protocol implementation.
    zx_status_t SysmemConnect(zx::channel allocator2_request);

    void GetProtocol(sysmem_protocol_t* proto) {
        proto->ops = &sysmem_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

class ProxyAmlogicCanvas : public ddk::AmlogicCanvasProtocol<ProxyAmlogicCanvas> {
public:
    explicit ProxyAmlogicCanvas(uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy)
        : device_id_(device_id), proxy_(proxy) {}

    // Amlogic Canvas protocol implementation.
    zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                    uint8_t* out_canvas_idx);
    zx_status_t AmlogicCanvasFree(uint8_t canvas_idx);

    void GetProtocol(amlogic_canvas_protocol_t* proto) {
        proto->ops = &amlogic_canvas_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
};

class ProxyDevice;
using ProxyDeviceType = ddk::FullDevice<ProxyDevice>;

class ProxyDevice : public ProxyDeviceType,
                    public ddk::PDevProtocol<ProxyDevice, ddk::base_protocol> {
public:
    explicit ProxyDevice(zx_device_t* parent, uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy)
        : ProxyDeviceType(parent), device_id_(device_id), proxy_(proxy), clk_(device_id, proxy),
          sysmem_(device_id, proxy), canvas_(device_id, proxy) {}

    // Creates a ProxyDevice to be the root platform device.
    static zx_status_t CreateRoot(zx_device_t* parent, fbl::RefPtr<PlatformProxy> proxy);

    // Creates a ProxyDevice to be a child platform device or a proxy client device.
    static zx_status_t CreateChild(zx_device_t* parent, uint32_t device_id,
                                   fbl::RefPtr<PlatformProxy> proxy, const device_add_args_t* args,
                                   zx_device_t** device);

    // Full device protocol implementation.
    // For child devices, these call through to the device protocol passed via pdev_device_add().
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
    zx_status_t DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags);
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
    zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
    zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
    zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource);
    zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t** device);
    zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                size_t protocol_size, size_t* protocol_actual);

private:
    struct Mmio {
        zx_paddr_t base;
        size_t length;
        zx::resource resource;
    };
    struct Irq {
        uint32_t irq;
        // ZX_INTERRUPT_MODE_* flags
        uint32_t mode;
        zx::resource resource;
    };

    zx_status_t InitCommon();
    zx_status_t InitRoot();
    zx_status_t InitChild(const device_add_args_t* args, zx_device_t** device);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ProxyDevice);

    const uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
    fbl::Vector<Mmio> mmios_;
    fbl::Vector<Irq> irqs_;
    fbl::Vector<ProxyGpio> gpios_;
    fbl::Vector<ProxyI2c> i2cs_;
    ProxyClock clk_;
    ProxySysmem sysmem_;
    ProxyAmlogicCanvas canvas_;

    char name_[ZX_MAX_NAME_LEN];
    uint32_t metadata_count_;

    // These fields are saved values from the device_add_args_t passed to pdev_device_add().
    // These are unused for top level devices created via pbus_device_add().
    void* ctx_ = nullptr;
    const zx_protocol_device_t* device_ops_ = nullptr;
    uint32_t proto_id_ = 0;
    void* proto_ops_ = nullptr;
};

} // namespace platform_bus
