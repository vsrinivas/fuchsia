// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/protocol/amlogiccanvas.h>
#include <ddktl/protocol/clock.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/power.h>
#include <ddktl/protocol/sysmem.h>
#include <ddktl/protocol/platform/device.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy;

class ProxyGpio : public ddk::GpioProtocol<ProxyGpio> {
public:
    explicit ProxyGpio(uint32_t index, PlatformProxy* proxy)
        : index_(index), proxy_(proxy) {}

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
    uint32_t index_;
    PlatformProxy* proxy_;
};

class ProxyI2c : public ddk::I2cProtocol<ProxyI2c> {
public:
    explicit ProxyI2c(uint32_t index, PlatformProxy* proxy)
        : index_(index), proxy_(proxy) {}

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
    uint32_t index_;
    PlatformProxy* proxy_;
};

class ProxyClock : public ddk::ClockProtocol<ProxyClock> {
public:
    explicit ProxyClock(uint32_t index, PlatformProxy* proxy)
        : index_(index), proxy_(proxy) {}

    // Clock protocol implementation.
    zx_status_t ClockEnable();
    zx_status_t ClockDisable();

    void GetProtocol(clock_protocol_t* proto) {
        proto->ops = &clock_protocol_ops_;
        proto->ctx = this;
    }

private:
    uint32_t index_;
    PlatformProxy* proxy_;
};

class ProxySysmem : public ddk::SysmemProtocol<ProxySysmem> {
public:
    explicit ProxySysmem(PlatformProxy* proxy)
        : proxy_(proxy) {}

    // Sysmem protocol implementation.
    zx_status_t SysmemConnect(zx::channel allocator2_request);

    void GetProtocol(sysmem_protocol_t* proto) {
        proto->ops = &sysmem_protocol_ops_;
        proto->ctx = this;
    }

private:
    PlatformProxy* proxy_;
};

class ProxyAmlogicCanvas : public ddk::AmlogicCanvasProtocol<ProxyAmlogicCanvas> {
public:
    explicit ProxyAmlogicCanvas(PlatformProxy* proxy)
        : proxy_(proxy) {}

    // Amlogic Canvas protocol implementation.
    zx_status_t AmlogicCanvasConfig(zx::vmo vmo, size_t offset, const canvas_info_t* info,
                                    uint8_t* out_canvas_idx);
    zx_status_t AmlogicCanvasFree(uint8_t canvas_idx);

    void GetProtocol(amlogic_canvas_protocol_t* proto) {
        proto->ops = &amlogic_canvas_protocol_ops_;
        proto->ctx = this;
    }

private:
    PlatformProxy* proxy_;
};

using PlatformProxyType = ddk::Device<PlatformProxy, ddk::GetProtocolable>;

// This is the main class for the proxy side platform bus driver.
// It handles RPC communication with the main platform bus driver in the root devhost.

class PlatformProxy : public PlatformProxyType,
                      public ddk::PDevProtocol<PlatformProxy, ddk::base_protocol> {
public:
    explicit PlatformProxy(zx_device_t* parent, zx_handle_t rpc_channel)
        :  PlatformProxyType(parent), rpc_channel_(rpc_channel), sysmem_(this), canvas_(this) {}

    static zx_status_t Create(void* ctx, zx_device_t* parent, const char* name,
                              const char* args, zx_handle_t rpc_channel);

    // Device protocol implementation.
    zx_status_t DdkGetProtocol(uint32_t proto_id, void* out);
    void DdkRelease();

    // Platform device protocol implementation.
    zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio);
    zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq);
    zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti);
    zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource);
    zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                size_t protocol_size, size_t* protocol_actual);

    zx_status_t Rpc(const platform_proxy_req_t* req, size_t req_length, 
                    platform_proxy_rsp_t* resp, size_t resp_length,
                    const zx_handle_t* in_handles, size_t in_handle_count,
                    zx_handle_t* out_handles, size_t out_handle_count,
                    size_t* out_actual);

    inline zx_status_t Rpc(const platform_proxy_req_t* req, size_t req_length,
                           platform_proxy_rsp_t* resp, size_t resp_length) {
        return Rpc(req, req_length, resp, resp_length, nullptr, 0, nullptr, 0, nullptr);
    }

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

    DISALLOW_COPY_ASSIGN_AND_MOVE(PlatformProxy);

    zx_status_t Init(zx_device_t* parent);

    const zx::channel rpc_channel_;

    char name_[ZX_MAX_NAME_LEN];
    uint32_t metadata_count_;

    fbl::Vector<Mmio> mmios_;
    fbl::Vector<Irq> irqs_;
    fbl::Vector<ProxyGpio> gpios_;
    fbl::Vector<ProxyI2c> i2cs_;
    fbl::Vector<ProxyClock> clocks_;
    ProxySysmem sysmem_;
    ProxyAmlogicCanvas canvas_;
};

} // namespace platform_bus
