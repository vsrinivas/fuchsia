// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "component-proxy.h"

#include <ddk/debug.h>

#include <memory>

namespace component {

zx_status_t ComponentProxy::Create(void* ctx, zx_device_t* parent, const char* name,
                                   const char* args, zx_handle_t raw_rpc) {
    zx::channel rpc(raw_rpc);
    auto dev = std::make_unique<ComponentProxy>(parent, std::move(rpc));
    auto status = dev->DdkAdd("component-proxy", DEVICE_ADD_NON_BINDABLE);
    if (status == ZX_OK) {
        // devmgr owns the memory now
        __UNUSED auto ptr = dev.release();
    }
    return status;
}

zx_status_t ComponentProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
    auto* proto = static_cast<ddk::AnyProtocol*>(out);
    proto->ctx = this;

    switch (proto_id) {
    case ZX_PROTOCOL_AMLOGIC_CANVAS:
        proto->ops = &amlogic_canvas_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_CLOCK:
        proto->ops = &clock_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_ETH_BOARD:
        proto->ops = &eth_board_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_GPIO:
        proto->ops = &gpio_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_PDEV:
        proto->ops = &pdev_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_POWER:
        proto->ops = &power_protocol_ops_;
        return ZX_OK;
    case ZX_PROTOCOL_SYSMEM:
        proto->ops = &sysmem_protocol_ops_;
        return ZX_OK;
    default:
        zxlogf(ERROR, "%s unsupported protocol \'%u\'\n", __func__, proto_id);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void ComponentProxy::DdkUnbind() {
    DdkRemove();
}

void ComponentProxy::DdkRelease() {
    delete this;
}

zx_status_t ComponentProxy::Rpc(const ProxyRequest* req, size_t req_length, ProxyResponse* resp,
                                size_t resp_length, const zx_handle_t* in_handles,
                                size_t in_handle_count, zx_handle_t* out_handles,
                                size_t out_handle_count, size_t* out_actual) {
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
    auto status = rpc_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
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

zx_status_t ComponentProxy::AmlogicCanvasConfig(zx::vmo vmo, size_t offset,
                                                    const canvas_info_t* info,
                                                    uint8_t* out_canvas_idx) {
    AmlogicCanvasProxyRequest req = {};
    AmlogicCanvasProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
    req.op = AmlogicCanvasOp::CONFIG;
    req.offset = offset;
    req.info = *info;
    zx_handle_t handle = vmo.release();

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), &handle, 1, nullptr, 0,
                      nullptr);
    if (status != ZX_OK) {
        return status;
    }
    *out_canvas_idx = resp.canvas_idx;
    return ZX_OK;
}

zx_status_t ComponentProxy::AmlogicCanvasFree(uint8_t canvas_idx) {
    AmlogicCanvasProxyRequest req = {};
    AmlogicCanvasProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_AMLOGIC_CANVAS;
    req.op = AmlogicCanvasOp::FREE;
    req.canvas_idx = canvas_idx;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::ClockEnable(uint32_t index) {
    ClockProxyRequest req = {};
    ProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.op = ClockOp::ENABLE;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ComponentProxy::ClockDisable(uint32_t index) {
    ClockProxyRequest req = {};
    ProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.op = ClockOp::DISABLE;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ComponentProxy::EthBoardResetPhy() {
    EthBoardProxyRequest req = {};
    ProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_ETH_BOARD;
    req.op = EthBoardOp::RESET_PHY;

    return Rpc(&req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ComponentProxy::GpioConfigIn(uint32_t flags) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::CONFIG_IN;
    req.flags = flags;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::GpioConfigOut(uint8_t initial_value) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::CONFIG_OUT;
    req.value = initial_value;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::GpioSetAltFunction(uint64_t function) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::SET_ALT_FUNCTION;
    req.alt_function = function;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::GET_INTERRUPT;
    req.flags = flags;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t ComponentProxy::GpioSetPolarity(uint32_t polarity) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::SET_POLARITY;
    req.polarity = polarity;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::GpioReleaseInterrupt() {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::RELEASE_INTERRUPT;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::GpioRead(uint8_t* out_value) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::READ;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));

    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.value;
    return ZX_OK;
}

zx_status_t ComponentProxy::GpioWrite(uint8_t value) {
    GpioProxyRequest req = {};
    GpioProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.op = GpioOp::WRITE;
    req.value = value;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_MMIO;
    req.index = index;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                      &out_mmio->vmo, 1, nullptr);
    if (status == ZX_OK) {
        out_mmio->offset = resp.offset;
        out_mmio->size = resp.size;
    }
    return status;
}

zx_status_t ComponentProxy::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                             zx::interrupt* out_irq) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_INTERRUPT;
    req.flags = flags;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t ComponentProxy::PDevGetBti(uint32_t index, zx::bti* out_bti) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_BTI;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_bti->reset_and_get_address(), 1, nullptr);
}

zx_status_t ComponentProxy::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_SMC;
    req.index = index;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
               out_resource->reset_and_get_address(), 1, nullptr);
}

zx_status_t ComponentProxy::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_DEVICE_INFO;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.device_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ComponentProxy::PDevGetBoardInfo(pdev_board_info_t* out_info) {
    PdevProxyRequest req = {};
    PdevProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.op = PdevOp::GET_BOARD_INFO;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.board_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ComponentProxy::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                          zx_device_t** device) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ComponentProxy::PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                            size_t protocol_size, size_t* protocol_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ComponentProxy::PowerEnablePowerDomain() {
    PowerProxyRequest req = {};
    PowerProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.op = PowerOp::ENABLE;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::PowerDisablePowerDomain() {
    PowerProxyRequest req = {};
    PowerProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.op = PowerOp::DISABLE;

    return Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ComponentProxy::PowerGetPowerDomainStatus(power_domain_status_t* out_status) {
    PowerProxyRequest req = {};
    PowerProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.op = PowerOp::GET_STATUS;

    auto status = Rpc(&req.header, sizeof(req), &resp.header,  sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    *out_status = resp.status;
    return status;
}

zx_status_t ComponentProxy::PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) {
    PowerProxyRequest req = {};
    PowerProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.op = PowerOp::WRITE_PMIC_CTRL_REG;
    req.reg_addr = reg_addr;
    req.reg_value = value;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    return status;
}

zx_status_t ComponentProxy::PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) {
    PowerProxyRequest req = {};
    PowerProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.op = PowerOp::READ_PMIC_CTRL_REG;
    req.reg_addr = reg_addr;

    auto status = Rpc(&req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.reg_value;
    return status;
}

zx_status_t ComponentProxy::SysmemConnect(zx::channel allocator2_request) {
    SysmemProxyRequest req = {};
    ProxyResponse resp = {};
    req.header.proto_id = ZX_PROTOCOL_SYSMEM;
    req.op = SysmemOp::CONNECT;
    zx_handle_t handle = allocator2_request.release();

    return Rpc(&req.header, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0, nullptr);
}

const zx_driver_ops_t driver_ops = []() {
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.create = ComponentProxy::Create;
    return ops;
}();

} // namespace component

ZIRCON_DRIVER_BEGIN(component_proxy, component::driver_ops, "zircon", "0.1", 1)
// Unmatchable.  This is loaded via the proxy driver mechanism instead of the binding process
BI_ABORT()
ZIRCON_DRIVER_END(component_proxy)
