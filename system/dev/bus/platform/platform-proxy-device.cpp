// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy-device.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "platform-proxy.h"
#include "proxy-protocol.h"

// The implementation of the platform bus protocol in this file is for use by
// drivers that exist in a proxy devhost and communicate with the platform bus
// over an RPC channel.
//
// More information can be found at the top of platform-device.cpp.

namespace platform_bus {

zx_status_t ProxyDevice::GpioConfigIn(void* ctx, uint32_t flags) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_IN;
    req.index = gpio_ctx->index;
    req.flags = flags;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::GpioConfigOut(void* ctx, uint8_t initial_value) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_OUT;
    req.index = gpio_ctx->index;
    req.value = initial_value;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::GpioSetAltFunction(void* ctx, uint64_t function) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_ALT_FUNCTION;
    req.index = gpio_ctx->index;
    req.alt_function = function;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::GpioGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_handle) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_GET_INTERRUPT;
    req.index = gpio_ctx->index;
    req.flags = flags;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
                              nullptr, 0, out_handle, 1, nullptr);
}

zx_status_t ProxyDevice::GpioSetPolarity(void* ctx, uint32_t polarity) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_POLARITY;
    req.index = gpio_ctx->index;
    req.polarity = polarity;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::GpioReleaseInterrupt(void* ctx) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_RELEASE_INTERRUPT;
    req.index = gpio_ctx->index;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::GpioRead(void* ctx, uint8_t* out_value) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_READ;
    req.index = gpio_ctx->index;

    auto status = thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                                    sizeof(resp));

    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.value;
    return ZX_OK;
}

zx_status_t ProxyDevice::GpioWrite(void* ctx, uint8_t value) {
    auto gpio_ctx = static_cast<GpioCtx*>(ctx);
    auto thiz = gpio_ctx->thiz;
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_WRITE;
    req.index = gpio_ctx->index;
    req.value = value;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                             sizeof(resp));
}

zx_status_t ProxyDevice::I2cGetMaxTransferSize(void* ctx, size_t* out_size) {
    auto i2c_ctx = static_cast<I2cCtx*>(ctx);
    auto thiz = i2c_ctx->thiz;
    rpc_i2c_req_t req = {};
    rpc_i2c_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_I2C;
    req.header.op = I2C_GET_MAX_TRANSFER;
    req.index = i2c_ctx->index;

    auto status = thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp.header,
                                    sizeof(resp));
    if (status == ZX_OK) {
        *out_size = resp.max_transfer;
    }
    return status;
}

zx_status_t ProxyDevice::I2cGetInterrupt(void* ctx, uint32_t flags, zx_handle_t* out_handle) {
    return ZX_ERR_NOT_SUPPORTED;
}

void ProxyDevice::I2cTransact(void* ctx, const i2c_op_t* ops, size_t cnt,
                              i2c_transact_callback transact_cb, void* cookie) {
    auto i2c_ctx = static_cast<I2cCtx*>(ctx);
    auto thiz = i2c_ctx->thiz;
    size_t writes_length = 0;
    size_t reads_length = 0;
    for (size_t i = 0; i < cnt; ++i) {
        if (ops[i].is_read) {
            reads_length += ops[i].data_size;
        } else {
            writes_length += ops[i].data_size;
        }
    }
    if (!writes_length && !reads_length) {
        transact_cb(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
        return;
    }

    size_t req_length = sizeof(rpc_i2c_req_t) + cnt * sizeof(i2c_rpc_op_t) + writes_length;
    if (req_length >= PROXY_MAX_TRANSFER_SIZE) {
        return transact_cb(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
    }
    uint8_t req_buffer[PROXY_MAX_TRANSFER_SIZE];
    auto req = reinterpret_cast<rpc_i2c_req_t*>(req_buffer);
    req->header.proto_id = ZX_PROTOCOL_I2C;
    req->header.op = I2C_TRANSACT;
    req->index = i2c_ctx->index;
    req->cnt = cnt;
    req->transact_cb = transact_cb;
    req->cookie = cookie;

    auto rpc_ops = reinterpret_cast<i2c_rpc_op_t*>(req + 1);
    ZX_ASSERT(cnt < I2C_MAX_RW_OPS);
    for (size_t i = 0; i < cnt; ++i) {
        rpc_ops[i].length = ops[i].data_size;
        rpc_ops[i].is_read = ops[i].is_read;
        rpc_ops[i].stop = ops[i].stop;
    }
    uint8_t* p_writes = reinterpret_cast<uint8_t*>(rpc_ops) + cnt * sizeof(i2c_rpc_op_t);
    for (size_t i = 0; i < cnt; ++i) {
        if (!ops[i].is_read) {
            memcpy(p_writes, ops[i].data_buffer, ops[i].data_size);
            p_writes += ops[i].data_size;
        }
    }

    const size_t resp_length = sizeof(rpc_i2c_rsp_t) + reads_length;
    if (resp_length >= PROXY_MAX_TRANSFER_SIZE) {
        transact_cb(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
        return;
    }
    uint8_t resp_buffer[PROXY_MAX_TRANSFER_SIZE];
    rpc_i2c_rsp_t* rsp = reinterpret_cast<rpc_i2c_rsp_t*>(resp_buffer);
    size_t actual;
    auto status = thiz->proxy_->Rpc(thiz->device_id_, &req->header,
                                    static_cast<uint32_t>(req_length),
                                    &rsp->header, static_cast<uint32_t>(resp_length),
                                    nullptr, 0, nullptr, 0, &actual);
    if (status != ZX_OK) {
        transact_cb(cookie, status, nullptr, 0);
        return;
    }

    // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
    // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
    // In the future we may want to redo the plumbing to allow this to be truly asynchronous.

    if (actual != resp_length) {
        status = ZX_ERR_INTERNAL;
    } else {
        status = rsp->header.status;
    }
    i2c_op_t read_ops[I2C_MAX_RW_OPS];
    size_t read_ops_cnt = 0;
    uint8_t* p_reads = reinterpret_cast<uint8_t*>(rsp + 1);
    for (size_t i = 0; i < cnt; ++i) {
        if (ops[i].is_read) {
            read_ops[read_ops_cnt] = ops[i];
            read_ops[read_ops_cnt].data_buffer = p_reads;
            read_ops_cnt++;
            p_reads += ops[i].data_size;
        }
    }
    transact_cb(rsp->cookie, status, read_ops, read_ops_cnt);

    return;
}

zx_status_t ProxyDevice::ClkEnable(void* ctx, uint32_t index) {
    ProxyDevice* thiz = static_cast<ProxyDevice*>(ctx);
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLK;
    req.header.op = CLK_ENABLE;
    req.index = index;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyDevice::ClkDisable(void* ctx, uint32_t index) {
    ProxyDevice* thiz = static_cast<ProxyDevice*>(ctx);
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLK;
    req.header.op = CLK_DISABLE;
    req.index = index;

    return thiz->proxy_->Rpc(thiz->device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const Mmio& mmio = mmios_[index];
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;

    zx_status_t status = zx_vmo_create_physical(mmio.resource.get(), vmo_base, vmo_size,
                                                vmo.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: creating vmo failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "%s mmio %u", name_, index);
    status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: setting vmo name failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    out_mmio->offset = mmio.base - vmo_base;
    out_mmio->vmo = vmo.release();
    out_mmio->size = mmio.length;
    return ZX_OK;
}

// TODO(surajmalhotra): Remove after migrating all clients off.
zx_status_t ProxyDevice::PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                                 size_t* out_size, zx_paddr_t* out_paddr,
                                 zx_handle_t* out_handle) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const Mmio& mmio = mmios_[index];
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;

    zx_status_t status = zx_vmo_create_physical(mmio.resource.get(), vmo_base, vmo_size,
                                                vmo.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: creating vmo failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "%s mmio %u", name_, index);
    status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: setting vmo name failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    status = vmo.set_cache_policy(cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: setting cache policy failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    uintptr_t virt;
    status = zx::vmar::root_self()->map(0, vmo, 0, vmo_size, ZX_VM_PERM_READ |
                                        ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: mapping vmar failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    *out_size = mmio.length;
    if (out_paddr) {
        *out_paddr = mmio.base;
    }
    *out_vaddr = reinterpret_cast<void*>(virt + (mmio.base - vmo_base));
    *out_handle = vmo.release();
    return ZX_OK;

}

zx_status_t ProxyDevice::PDevGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
    if (index >= irqs_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    Irq* irq = &irqs_[index];
    if (flags == 0) {
        flags = irq->mode;
    }
    zx_handle_t handle;
    zx_status_t status = zx_interrupt_create(irq->resource.get(), irq->irq, flags, &handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: creating interrupt failed: %d\n", name_, __FUNCTION__, status);
        return status;
    }

    *out_handle = handle;
    return ZX_OK;
}

zx_status_t ProxyDevice::PDevGetBti(uint32_t index, zx_handle_t* out_handle) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_BTI;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_handle, 1, nullptr);
}

zx_status_t ProxyDevice::PDevGetSmc(uint32_t index, zx_handle_t* out_handle) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_SMC;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_handle, 1, nullptr);
}

zx_status_t ProxyDevice::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_DEVICE_INFO;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.device_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ProxyDevice::PDevGetBoardInfo(pdev_board_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_BOARD_INFO;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.board_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ProxyDevice::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                       zx_device_t** device) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_DEVICE_ADD;
    req.index = index;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }

    return CreateChild(zxdev(), resp.device_id, proxy_, args, device);
}

zx_status_t ProxyDevice::PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                         size_t protocol_size, size_t* protocol_actual) {
    if (protocol_size < sizeof(ddk::AnyProtocol)) {
        return ZX_ERR_INVALID_ARGS;
    }
    *protocol_actual = sizeof(ddk::AnyProtocol);

    // Return the GPIO protocol for the given index.
    if (proto_id == ZX_PROTOCOL_GPIO) {
        if (index >= gpio_ctxs_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto proto = static_cast<gpio_protocol_t*>(out_protocol);
        proto->ops = &gpio_proto_ops_;
        proto->ctx = &gpio_ctxs_[index];
        return ZX_OK;
    }

    if (proto_id == ZX_PROTOCOL_I2C) {
        if (index >= i2c_ctxs_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto proto = static_cast<i2c_protocol_t*>(out_protocol);
        proto->ops = &i2c_proto_ops_;
        proto->ctx = &i2c_ctxs_[index];
        return ZX_OK;
    }

    // For other protocols, fall through to DdkGetProtocol if index is zero
    if (index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return DdkGetProtocol(proto_id, out_protocol);
}

zx_status_t ProxyDevice::CreateRoot(zx_device_t* parent, fbl::RefPtr<PlatformProxy> proxy) {
    fbl::AllocChecker ac;
    auto dev = fbl::make_unique_checked<ProxyDevice>(&ac,parent, ROOT_DEVICE_ID, proxy);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->InitRoot();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

zx_status_t ProxyDevice::CreateChild(zx_device_t* parent, uint32_t device_id,
                                     fbl::RefPtr<PlatformProxy> proxy,
                                     const device_add_args_t* args,
                                     zx_device_t** device) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<ProxyDevice> dev(new (&ac) platform_bus::ProxyDevice(parent, device_id, proxy));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->InitChild(args, device);
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

ProxyDevice::ProxyDevice(zx_device_t* parent, uint32_t device_id,
                         fbl::RefPtr<PlatformProxy> proxy)
    : ProxyDeviceType(parent), device_id_(device_id), proxy_(proxy) {
    // Initialize protocol ops
    clk_proto_ops_.enable = ClkEnable;
    clk_proto_ops_.disable = ClkDisable;
    gpio_proto_ops_.config_in = GpioConfigIn;
    gpio_proto_ops_.config_out = GpioConfigOut;
    gpio_proto_ops_.set_alt_function = GpioSetAltFunction;
    gpio_proto_ops_.read = GpioRead;
    gpio_proto_ops_.write = GpioWrite;
    gpio_proto_ops_.get_interrupt = GpioGetInterrupt;
    gpio_proto_ops_.release_interrupt = GpioReleaseInterrupt;
    gpio_proto_ops_.set_polarity = GpioSetPolarity;
    i2c_proto_ops_.transact = I2cTransact;
    i2c_proto_ops_.get_max_transfer_size = I2cGetMaxTransferSize;
    i2c_proto_ops_.get_interrupt = I2cGetInterrupt;
}

zx_status_t ProxyDevice::InitCommon() {
    pdev_device_info_t info;
    auto status = PDevGetDeviceInfo(&info);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(name_, info.name, sizeof(name_));
    metadata_count_ = info.metadata_count;

    fbl::AllocChecker ac;

    for (uint32_t i = 0; i < info.mmio_count; i++) {
        rpc_pdev_req_t req = {};
        rpc_pdev_rsp_t resp = {};
        zx_handle_t rsrc_handle;

        req.header.proto_id = ZX_PROTOCOL_PDEV;
        req.header.op = PDEV_GET_MMIO;
        req.index = i;
        status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
                             NULL, 0, &rsrc_handle, 1, NULL);
        if (status != ZX_OK) {
            return status;
        }

        Mmio mmio;
        mmio.base = resp.paddr;
        mmio.length = resp.length;
        mmio.resource.reset(rsrc_handle);
        mmios_.push_back(fbl::move(mmio), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zxlogf(SPEW, "%s: received MMIO %u (base %#lx length %#lx handle %#x)\n", name_, i,
               mmio.base, mmio.length, mmio.resource.get());
    }

    for (uint32_t i = 0; i < info.irq_count; i++) {
        rpc_pdev_req_t req = {};
        rpc_pdev_rsp_t resp = {};
        zx_handle_t rsrc_handle;

        req.header.proto_id = ZX_PROTOCOL_PDEV;
        req.header.op = PDEV_GET_INTERRUPT;
        req.index = i;
        status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
                             NULL, 0, &rsrc_handle, 1, NULL);
        if (status != ZX_OK) {
            return status;
        }

        Irq irq;
        irq.irq = resp.irq;
        irq.mode = resp.mode;
        irq.resource.reset(rsrc_handle);
        irqs_.push_back(fbl::move(irq), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zxlogf(SPEW, "%s: received IRQ %u (irq %#x handle %#x)\n", name_, i, irq.irq,
               irq.resource.get());
    }

    uint32_t gpio_count = info.gpio_count;
    if (gpio_count > 0) {
        gpio_ctxs_.reset(new (&ac) GpioCtx[gpio_count], gpio_count);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        for (uint32_t i = 0; i < info.gpio_count; i++) {
            gpio_ctxs_[i].thiz = this;
            gpio_ctxs_[i].index = i;
        }
    }

    uint32_t i2c_count = info.i2c_channel_count;
    if (i2c_count > 0) {
        i2c_ctxs_.reset(new (&ac) I2cCtx[i2c_count], i2c_count);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        for (uint32_t i = 0; i < i2c_count; i++) {
            i2c_ctxs_[i].thiz = this;
            i2c_ctxs_[i].index = i;
        }
    }

    return ZX_OK;
}

zx_status_t ProxyDevice::InitRoot() {
    auto status = InitCommon();
    if (status != ZX_OK) {
        return status;
    }
    return DdkAdd(name_);
}

zx_status_t ProxyDevice::InitChild(const device_add_args_t* args, zx_device_t** device) {
    auto status = InitCommon();
    if (status != ZX_OK) {
        return status;
    }

    ctx_ = args->ctx;
    device_ops_ = args->ops;
    proto_id_ = args->proto_id;
    proto_ops_ = args->proto_ops;

    device_add_args_t new_args = *args;
    // Replace ctx and device protocol ops with ours so we can intercept device_get_protocol().
    new_args.ctx = this;
    new_args.ops = &ddk_device_proto_;

    if (!device) {
        device = &zxdev_;
    }
    if (metadata_count_ == 0) {
        auto status = device_add(parent(), &new_args, device);
        if (status == ZX_OK) zxdev_ = *device;
        return status;
    }

    new_args.flags |= DEVICE_ADD_INVISIBLE;
    status = device_add(parent(), &new_args, device);
    if (status != ZX_OK) {
        return status;
    }
    zxdev_ = *device;
    // Remove ourselves from the devmgr if something goes wrong.
    auto cleanup = fbl::MakeAutoCall([this]() { DdkRemove(); });

    for (uint32_t i = 0; i < metadata_count_; i++) {
        rpc_pdev_req_t req = {};
        rpc_pdev_metadata_rsp_t resp = {};
        req.header.proto_id = ZX_PROTOCOL_PDEV;
        req.header.op = PDEV_GET_METADATA;
        req.index = i;

        status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.pdev.header,
                             sizeof(resp));
        if (status == ZX_OK) {
           status = DdkAddMetadata(resp.pdev.metadata_type, resp.metadata,
                                   resp.pdev.metadata_length);
        }
        if (status != ZX_OK) {
            zxlogf(WARN, "%s failed to add metadata for new device\n", __func__);
        }
    }

    cleanup.cancel();
    // Make ourselves visible after all metadata has been added successfully.
    DdkMakeVisible();
    return ZX_OK;
}

zx_status_t ProxyDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
    auto* proto = static_cast<ddk::AnyProtocol*>(out);

    // Try driver's get_protocol() first, if it is implemented.
    if (device_ops_ && device_ops_->get_protocol) {
        if (device_ops_->get_protocol(ctx_, proto_id, out) == ZX_OK) {
            return ZX_OK;
        }
    }

    // Next try driver's primary protocol.
    if (proto_ops_ && proto_id_ == proto_id) {
        proto->ops = proto_ops_;
        proto->ctx = ctx_;
        return ZX_OK;
    }

    // Finally, protocols provided by platform bus.
    proto->ctx = this;
    switch (proto_id) {
    case ZX_PROTOCOL_PDEV: {
        proto->ops = &ops_;
        break;
    }
    case ZX_PROTOCOL_GPIO: {
        auto count = gpio_ctxs_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one GPIO\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth GPIO resource.
        proto->ops = &gpio_proto_ops_;
        proto->ctx = &gpio_ctxs_[0];
        return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
        auto count = i2c_ctxs_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one I2C channel\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth I2C resource.
        proto->ops = &i2c_proto_ops_;
        proto->ctx = &i2c_ctxs_[0];
        return ZX_OK;
    }
    case ZX_PROTOCOL_CLK: {
        proto->ops = &clk_proto_ops_;
        break;
    }
    default:
        return proxy_->GetProtocol(proto_id, out);;
    }
    return ZX_OK;
}

zx_status_t ProxyDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    if (device_ops_ && device_ops_->open) {
        return device_ops_->open(ctx_, dev_out, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags) {
    if (device_ops_ && device_ops_->open_at) {
        return device_ops_->open_at(ctx_, dev_out, path, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkClose(uint32_t flags) {
    if (device_ops_ && device_ops_->close) {
        return device_ops_->close(ctx_, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

void ProxyDevice::DdkUnbind() {
    if (device_ops_ && device_ops_->unbind) {
        device_ops_->unbind(ctx_);
    }
}

void ProxyDevice::DdkRelease() {
    if (device_ops_ && device_ops_->release) {
        device_ops_->release(ctx_);
    }
    delete this;
}

zx_status_t ProxyDevice::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (device_ops_ && device_ops_->read) {
        return device_ops_->read(ctx_, buf, count, off, actual);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkWrite(const void* buf, size_t count, zx_off_t off, size_t* actual) {
    if (device_ops_ && device_ops_->write) {
        return device_ops_->write(ctx_, buf, count, off, actual);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_off_t ProxyDevice::DdkGetSize() {
    if (device_ops_ && device_ops_->get_size) {
        return device_ops_->get_size(ctx_);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                                  size_t out_len, size_t* actual) {
    if (device_ops_ && device_ops_->ioctl) {
        return device_ops_->ioctl(ctx_, op, in_buf, in_len, out_buf, out_len, actual);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkSuspend(uint32_t flags) {
    if (device_ops_ && device_ops_->suspend) {
        return device_ops_->suspend(ctx_, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkResume(uint32_t flags) {
    if (device_ops_ && device_ops_->resume) {
        return device_ops_->resume(ctx_, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkRxrpc(zx_handle_t channel) {
    if (device_ops_ && device_ops_->rxrpc) {
        return device_ops_->rxrpc(ctx_, channel);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

} // namespace platform_bus
