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
    rpc_sysmem_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_SYSMEM;
    req.header.op = SYSMEM_CONNECT;
    zx_handle_t handle = allocator_request.release();

    return proxy_->Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle,
                       1, nullptr, 0, nullptr);
}

zx_status_t ProxySysmem::SysmemRegisterHeap(uint64_t heap,
                                            zx::channel heap_connection) {
    rpc_sysmem_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_SYSMEM;
    req.header.op = SYSMEM_REGISTER_HEAP;
    req.heap = heap;
    zx_handle_t handle = heap_connection.release();

    return proxy_->Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle,
                       1, nullptr, 0, nullptr);
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
