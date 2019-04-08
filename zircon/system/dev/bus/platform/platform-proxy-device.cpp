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
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <utility>

#include "platform-proxy.h"
#include "proxy-protocol.h"

// The implementation of the platform bus protocol in this file is for use by
// drivers that exist in a proxy devhost and communicate with the platform bus
// over an RPC channel.
//
// More information can be found at the top of platform-device.cpp.

namespace platform_bus {

zx_status_t ProxyGpio::GpioConfigIn(uint32_t flags) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_IN;
    req.index = index_;
    req.flags = flags;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioConfigOut(uint8_t initial_value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG_OUT;
    req.index = index_;
    req.value = initial_value;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioSetAltFunction(uint64_t function) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_ALT_FUNCTION;
    req.index = index_;
    req.alt_function = function;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_GET_INTERRUPT;
    req.index = index_;
    req.flags = flags;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_irq->reset_and_get_address(), 1, nullptr);
}

zx_status_t ProxyGpio::GpioSetPolarity(uint32_t polarity) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_POLARITY;
    req.index = index_;
    req.polarity = polarity;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioReleaseInterrupt() {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_RELEASE_INTERRUPT;
    req.index = index_;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyGpio::GpioRead(uint8_t* out_value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_READ;
    req.index = index_;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));

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

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyI2c::I2cGetMaxTransferSize(size_t* out_size) {
    rpc_i2c_req_t req = {};
    rpc_i2c_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_I2C;
    req.header.op = I2C_GET_MAX_TRANSFER;
    req.index = index_;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
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
    auto status = proxy_->Rpc(device_id_, &req->header, static_cast<uint32_t>(req_length),
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

zx_status_t ProxyClock::ClockEnable(uint32_t index) {
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.header.op = CLK_ENABLE;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyClock::ClockDisable(uint32_t index) {
    rpc_clk_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_CLOCK;
    req.header.op = CLK_DISABLE;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyPower::PowerEnablePowerDomain() {
    rpc_power_req_t req = {};
    rpc_power_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.header.op = POWER_ENABLE;
    req.index = index_;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyPower::PowerDisablePowerDomain() {
    rpc_power_req_t req = {};
    rpc_power_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.header.op = POWER_DISABLE;
    req.index = index_;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyPower::PowerGetPowerDomainStatus(power_domain_status_t* out_status) {
    rpc_power_req_t req = {};
    rpc_power_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.header.op = POWER_GET_STATUS;
    req.index = index_;

    zx_status_t status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header,
                                     sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    *out_status = resp.status;
    return status;
}

zx_status_t ProxyPower::PowerWritePmicCtrlReg(uint32_t reg_addr, uint32_t value) {
    rpc_power_req_t req = {};
    rpc_power_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.header.op = POWER_WRITE_PMIC_CTRL_REG;
    req.index = index_;
    req.reg_addr = reg_addr;
    req.reg_value = value;

    zx_status_t status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header,
                                     sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    return status;
}

zx_status_t ProxyPower::PowerReadPmicCtrlReg(uint32_t reg_addr, uint32_t* out_value) {
    rpc_power_req_t req = {};
    rpc_power_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_POWER;
    req.header.op = POWER_READ_PMIC_CTRL_REG;
    req.index = index_;
    req.reg_addr = reg_addr;

    zx_status_t status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header,
                                     sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.reg_value;
    return status;
}

zx_status_t ProxySysmem::SysmemConnect(zx::channel allocator2_request) {
    platform_proxy_req_t req = {};
    platform_proxy_rsp_t resp = {};
    req.proto_id = ZX_PROTOCOL_SYSMEM;
    req.op = SYSMEM_CONNECT;
    zx_handle_t handle = allocator2_request.release();

    return proxy_->Rpc(device_id_, &req, sizeof(req), &resp, sizeof(resp), &handle, 1, nullptr, 0,
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

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
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

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
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

zx_status_t ProxyDevice::PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
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

zx_status_t ProxyDevice::PDevGetBti(uint32_t index, zx::bti* out_bti) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_BTI;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_bti->reset_and_get_address(), 1, nullptr);
}

zx_status_t ProxyDevice::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.proto_id = ZX_PROTOCOL_PDEV;
    req.header.op = PDEV_GET_SMC;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr, 0,
                       out_resource->reset_and_get_address(), 1, nullptr);
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

    if (proto_id == ZX_PROTOCOL_POWER) {
        if (index >= power_domains_.size()) {
            return ZX_ERR_OUT_OF_RANGE;
        }
        auto* proto = static_cast<power_protocol_t*>(out_protocol);
        power_domains_[index].GetProtocol(proto);
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
        status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
                             NULL, 0, &rsrc_handle, 1, NULL);
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
        ProxyGpio gpio(device_id_, i, proxy_);
        gpios_.push_back(std::move(gpio), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    for (uint32_t i = 0; i < info.power_domain_count; i++) {
        ProxyPower power_domain(device_id_, i, proxy_);
        power_domains_.push_back(std::move(power_domain), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
    }

    for (uint32_t i = 0; i < info.i2c_channel_count; i++) {
        ProxyI2c i2c(device_id_, i, proxy_);
        i2cs_.push_back(std::move(i2c), &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
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
    case ZX_PROTOCOL_POWER: {
        auto count = power_domains_.size();
        if (count == 0) {
            return ZX_ERR_NOT_SUPPORTED;
        } else if (count > 1) {
            zxlogf(ERROR, "%s: device has more than one power domain\n", __func__);
            return ZX_ERR_BAD_STATE;
        }
        // Return zeroth power domain resource.
        auto* proto = static_cast<power_protocol_t*>(out);
        power_domains_[0].GetProtocol(proto);
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
        auto* proto = static_cast<clock_protocol_t*>(out);
        clk_.GetProtocol(proto);
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

zx_status_t ProxyDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    if (device_ops_ && device_ops_->open) {
        return device_ops_->open(ctx_, dev_out, flags);
    }
    return ZX_OK;
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
    return ZX_OK;
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
