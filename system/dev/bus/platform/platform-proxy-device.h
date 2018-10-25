// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "platform-proxy-device.h"

#include <ddk/protocol/clk.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddktl/device.h>
#include <ddktl/protocol/platform-device.h>
#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <lib/zx/channel.h>
#include <lib/zx/handle.h>

#include "proxy-protocol.h"

namespace platform_bus {

class PlatformProxy;
class ProxyDevice;
using ProxyDeviceType = ddk::FullDevice<ProxyDevice>;

class ProxyDevice : public ProxyDeviceType, public ddk::PDevProtocol<ProxyDevice> {
public:
    explicit ProxyDevice(zx_device_t* parent, uint32_t device_id, fbl::RefPtr<PlatformProxy> proxy);

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
    zx_status_t PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                            size_t* out_size, zx_paddr_t* out_paddr, zx_handle_t* out_handle);
    zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle);
    zx_status_t PDevGetBti(uint32_t index, zx_handle_t* out_handle);
    zx_status_t PDevGetSmc(uint32_t index, zx_handle_t* out_handle);
    zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info);
    zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info);
    zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, zx_device_t** device);
    zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                size_t protocol_size, size_t* protocol_actual);

    // Clock protocol implementation.
    static zx_status_t ClkEnable(void* ctx, uint32_t index);
    static zx_status_t ClkDisable(void* ctx, uint32_t index);

    // GPIO protocol implementation.
    static zx_status_t GpioConfigIn(void* ctx, uint32_t flags);
    static zx_status_t GpioConfigOut(void* ctx, uint8_t initial_value);
    static zx_status_t GpioSetAltFunction(void* ctx, uint64_t function);
    static zx_status_t GpioRead(void* ctx, uint8_t* out_value);
    static zx_status_t GpioWrite(void* ctx, uint8_t value);
    static zx_status_t GpioGetInterrupt(void* ctx, uint32_t flags,
                                        zx_handle_t* out_handle);
    static zx_status_t GpioReleaseInterrupt(void* ctx);
    static zx_status_t GpioSetPolarity(void* ctx, uint32_t polarity);

    // I2C protocol implementation.
    static void I2cTransact(void* ctx, const i2c_op_t* ops, size_t cnt,
                            i2c_transact_callback transact_cb, void* cookie);
    static zx_status_t I2cGetMaxTransferSize(void* ctx, size_t* out_size);
    static zx_status_t I2cGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_handle);

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
    struct GpioCtx {
        ProxyDevice* thiz;
        uint32_t index;
    };
    struct I2cCtx {
        ProxyDevice* thiz;
        uint32_t index;
    };

    zx_status_t InitCommon();
    zx_status_t InitRoot();
    zx_status_t InitChild(const device_add_args_t* args, zx_device_t** device);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ProxyDevice);

    const uint32_t device_id_;
    fbl::RefPtr<PlatformProxy> proxy_;
    fbl::Vector<Mmio> mmios_;
    fbl::Vector<Irq> irqs_;
    char name_[ZX_MAX_NAME_LEN];
    uint32_t metadata_count_;

    // We can't used ddktl for these because ddktl only allows a device to implement one protocol,
    // and we are using ddktl for the platform device protocol.
    clk_protocol_ops_t clk_proto_ops_;
    gpio_protocol_ops_t gpio_proto_ops_;
    i2c_protocol_ops_t i2c_proto_ops_;

    // Contexts
    fbl::Array<GpioCtx> gpio_ctxs_;
    fbl::Array<I2cCtx> i2c_ctxs_;

    // These fields are saved values from the device_add_args_t passed to pdev_device_add().
    // These are unused for top level devices created via pbus_device_add().
    void* ctx_ = nullptr;
    zx_protocol_device_t* device_ops_ = nullptr;
    uint32_t proto_id_ = 0;
    void* proto_ops_ = nullptr;
};

} // namespace platform_bus
