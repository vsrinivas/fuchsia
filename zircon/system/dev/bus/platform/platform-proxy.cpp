// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include <utility>

namespace platform_bus {

zx_status_t ProxyGpio::GpioConfigIn(uint32_t flags) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_IN;
    req.index = index_;
    req.flags = flags;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioConfigOut(uint8_t initial_value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_OUT;
    req.index = index_;
    req.value = initial_value;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioSetAltFunction(uint64_t function) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_ALT_FUNCTION;
    req.index = index_;
    req.alt_function = function;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_GET_INTERRUPT;
    req.index = index_;
    req.flags = flags;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t ProxyGpio::GpioSetPolarity(uint32_t polarity) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_POLARITY;
    req.index = index_;
    req.polarity = polarity;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioReleaseInterrupt() {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_RELEASE_INTERRUPT;
    req.index = index_;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioRead(uint8_t* out_value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_READ;
    req.index = index_;

    auto status = proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.value;
    return ZX_OK;
}

zx_status_t ProxyGpio::GpioWrite(uint8_t value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_WRITE;
    req.index = index_;
    req.value = value;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyI2c::I2cGetMaxTransferSize(size_t* out_size) {
    rpc_i2c_req_t req = {};
    rpc_i2c_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_I2C;
    req.header.op = I2C_GET_MAX_TRANSFER;
    req.index = index_;

    auto status = proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        *out_size = resp.max_transfer;
    }
    return status;
}

zx_status_t ProxyI2c::I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
}

void ProxyI2c::I2cTransact(const i2c_op_t* ops, size_t cnt, i2c_transact_callback transact_cb,
                              void* cookie) {
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
    req->index = index_;
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
    auto status = proxy_->Rpc(&req->header, static_cast<uint32_t>(req_length),
                              &rsp->header, static_cast<uint32_t>(resp_length), nullptr, 0, nullptr,
                              0, &actual);
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

zx_status_t ProxyClock::ClockEnable() {
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.header.op = CLK_ENABLE;
    req.index = index_;

    return proxy_->Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyClock::ClockDisable() {
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.header.op = CLK_DISABLE;
    req.index = index_;

    return proxy_->Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxySysmem::SysmemConnect(zx::channel allocator_request) {
    platform_proxy_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.proto_id = ZX_PROTOCOL_SYSMEM;
    req.op = SYSMEM_CONNECT;
    zx_handle_t handle = allocator_request.release();

    return proxy_->Rpc(&req, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0,
                       nullptr);
}

zx_status_t ProxyAmlogicCanvas::AmlogicCanvasConfig(zx::vmo vmo, size_t offset,
                                                    const canvas_info_t* info,
                                                    uint8_t* out_canvas_idx) {
    rpc_amlogic_canvas_req_t req = {};
    rpc_amlogic_canvas_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
    req.header.op = AMLOGIC_CANVAS_CONFIG;
    req.offset = offset;
    req.info = *info;
    zx_handle_t handle = vmo.release();

    auto status = proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp),
                              &handle, 1, nullptr, 0, nullptr);
    if (status != ZX_OK) {
        return status;
    }
    *out_canvas_idx = resp.canvas_idx;
    return ZX_OK;
}

zx_status_t ProxyAmlogicCanvas::AmlogicCanvasFree(uint8_t canvas_idx) {
    rpc_amlogic_canvas_req_t req = {};
    rpc_amlogic_canvas_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
    req.header.op = AMLOGIC_CANVAS_FREE;
    req.canvas_idx = canvas_idx;

    return proxy_->Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t PlatformProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
    auto* proto = static_cast<ddk::AnyProtocol*>(out);

    switch (proto_id) {
    case ZX_PROTOCOL_PDEV: {
        proto->ops = &pdev_protocol_ops_;
        proto->ctx = this;
        return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO: {
        auto count = gpios_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one GPIO\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth GPIO resource.
        auto* proto = static_cast<gpio_protocol_t*>(out);
        gpios_[0].GetProtocol(proto);
        return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
        auto count = i2cs_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one I2C channel\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth I2C resource.
        auto* proto = static_cast<i2c_protocol_t*>(out);
        i2cs_[0].GetProtocol(proto);
        return ZX_OK;
    }
    case ZX_PROTOCOL_CLOCK: {
        auto count = clocks_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one clock\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth clock resource.
        auto* proto = static_cast<clock_protocol_t*>(out);
        clocks_[0].GetProtocol(proto);
        return ZX_OK;
    }
    case ZX_PROTOCOL_SYSMEM: {
        auto* proto = static_cast<sysmem_protocol_t*>(out);
        sysmem_.GetProtocol(proto);
        return ZX_OK;
    }
    case ZX_PROTOCOL_AMLOGIC_CANVAS: {
        auto* proto = static_cast<amlogic_canvas_protocol_t*>(out);
        canvas_.GetProtocol(proto);
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void PlatformProxy::DdkRelease() {
    delete this;
}

zx_status_t PlatformProxy::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const Mmio& mmio = mmios_[index];
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;

    zx_status_t status = zx::vmo::create_physical(mmio.resource, vmo_base, vmo_size, &vmo);
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

zx_status_t PlatformProxy::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                            zx::interrupt* out_irq) {
    if (index >= irqs_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    Irq* irq = &irqs_[index];
    if (flags == 0) {
        flags = irq->mode;
    }
    zx_status_t status = zx::interrupt::create(irq->resource, irq->irq, flags, out_irq);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: creating interrupt failed: %d\n", name_, __FUNCTION__, status);
        return status;
    }

    return ZX_OK;
}

zx_status_t PlatformProxy::PDevGetBti(uint32_t index, zx::bti* out_bti) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_BTI;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_bti->reset_and_get_address(), 1, nullptr);
}

zx_status_t PlatformProxy::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_SMC;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_resource->reset_and_get_address(), 1, nullptr);
}

zx_status_t PlatformProxy::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_DEVICE_INFO;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.device_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t PlatformProxy::PDevGetBoardInfo(pdev_board_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_BOARD_INFO;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.board_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t PlatformProxy::PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                         size_t protocol_size, size_t* protocol_actual) {
    if (protocol_size < sizeof(ddk::AnyProtocol)) {
        return ZX_ERR_INVALID_ARGS;
    }
    *protocol_actual = sizeof(ddk::AnyProtocol);

    // Return the GPIO protocol for the given index.
    if (proto_id == ZX_PROTOCOL_GPIO) {
        if (index >= gpios_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto* proto = static_cast<gpio_protocol_t*>(out_protocol);
        gpios_[index].GetProtocol(proto);
        return ZX_OK;
    }

    if (proto_id == ZX_PROTOCOL_I2C) {
        if (index >= i2cs_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto* proto = static_cast<i2c_protocol_t*>(out_protocol);
        i2cs_[index].GetProtocol(proto);
        return ZX_OK;
    }

    if (proto_id == ZX_PROTOCOL_CLOCK) {
        if (index >= clocks_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto* proto = static_cast<clock_protocol_t*>(out_protocol);
        clocks_[index].GetProtocol(proto);
        return ZX_OK;
    }

    // For other protocols, fall through to DdkGetProtocol if index is zero
    if (index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return DdkGetProtocol(proto_id, out_protocol);
}

zx_status_t PlatformProxy::Rpc(const platform_proxy_req_t* req, size_t req_length, 
                               platform_proxy_rsp_t* resp, size_t resp_length, 
                               const zx_handle_t* in_handles, size_t in_handle_count,
                               zx_handle_t* out_handles, size_t out_handle_count,
                               size_t* out_actual) {
    uint32_t resp_size, handle_count;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = static_cast<uint32_t>(req_length),
        .wr_num_handles = static_cast<uint32_t>(in_handle_count),
        .rd_num_bytes = static_cast<uint32_t>(resp_length),
        .rd_num_handles = static_cast<uint32_t>(out_handle_count),
    };
    auto status = rpc_channel_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
        // This is a fairly serious error; subsequent requests are very likely
        // to also fail.
        //
        // TODO(ZX-3833): Make this less likely and/or handle differently.
        zxlogf(ERROR, "PlatformProxy::Rpc rpc_channel_.call failed - status: %d\n", status);
        return status;
    }

    status = resp->status;

    if (status == ZX_OK && resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "PlatformProxy::Rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (status == ZX_OK && handle_count != out_handle_count) {
        zxlogf(ERROR, "PlatformProxy::Rpc handle count %u expected %zu\n", handle_count,
               out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    if (out_actual) {
        *out_actual = resp_size;
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

zx_status_t PlatformProxy::Create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    fbl::AllocChecker ac;

    auto proxy = fbl::make_unique_checked<PlatformProxy>(&ac, parent, rpc_channel);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    auto status = proxy->Init(parent);
    if (status != ZX_OK) {
        return status;
    }
 
    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = proxy.release();

    return ZX_OK;
}

zx_status_t PlatformProxy::Init(zx_device_t* parent) {
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
        status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), NULL, 0, &rsrc_handle,
                     1, NULL);
        if (status != ZX_OK) {
            return status;
        }

        Mmio mmio;
        mmio.base = resp.paddr;
        mmio.length = resp.length;
        mmio.resource.reset(rsrc_handle);
        mmios_.push_back(std::move(mmio), &ac);
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
        status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), NULL, 0, &rsrc_handle,
                     1, NULL);
        if (status != ZX_OK) {
            return status;
        }

        Irq irq;
        irq.irq = resp.irq;
        irq.mode = resp.mode;
        irq.resource.reset(rsrc_handle);
        irqs_.push_back(std::move(irq), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        zxlogf(SPEW, "%s: received IRQ %u (irq %#x handle %#x)\n", name_, i, irq.irq,
               irq.resource.get());
    }

    for (uint32_t i = 0; i < info.gpio_count; i++) {
        ProxyGpio gpio(i, this);
        gpios_.push_back(std::move(gpio), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    for (uint32_t i = 0; i < info.i2c_channel_count; i++) {
        ProxyI2c i2c(i, this);
        i2cs_.push_back(std::move(i2c), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    for (uint32_t i = 0; i < info.clk_count; i++) {
        ProxyClock clock(i, this);
        clocks_.push_back(std::move(clock), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    return DdkAdd(name_);
}

static zx_driver_ops_t proxy_driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.create = PlatformProxy::Create;
    return ops;
}();

} // namespace platform_bus

ZIRCON_DRIVER_BEGIN(platform_bus_proxy, platform_bus::proxy_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus_proxy)
