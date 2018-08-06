// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/clk.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <fbl/unique_ptr.h>

#include "platform-proxy.h"
#include "proxy-protocol.h"

// The implementation of the platform bus protocol in this file is for use by
// drivers that exist in a proxy devhost and communicate with the platform bus
// over an RPC channel.
//
// More information can be found at the top of platform-device.c.

namespace platform_bus {

zx_status_t PlatformProxy::Rpc(pdev_req_t* req, uint32_t req_length, pdev_resp_t* resp,
                               uint32_t resp_length, zx_handle_t* in_handles,
                               uint32_t in_handle_count, zx_handle_t* out_handles,
                               uint32_t out_handle_count, uint32_t* out_data_received) {
    uint32_t resp_size, handle_count;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = req_length,
        .wr_num_handles = in_handle_count,
        .rd_num_bytes = resp_length,
        .rd_num_handles = out_handle_count,
    };
    auto status = rpc_channel_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
        return status;
    } else if (resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "%s: PlatformProxy::Rpc resp_size too short: %u\n", name_, resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (handle_count != out_handle_count) {
        zxlogf(ERROR, "%s: PlatformProxy::Rpc handle count %u expected %u\n", name_, handle_count,
               out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    status = resp->status;
    if (out_data_received) {
        *out_data_received = static_cast<uint32_t>(resp_size - sizeof(pdev_resp_t));
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

zx_status_t PlatformProxy::UmsSetMode(usb_mode_t mode) {
    pdev_req_t req = {};
    req.op = PDEV_UMS_SET_MODE;
    req.usb_mode = mode;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::GpioConfig(uint32_t index, uint32_t flags) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_CONFIG;
    req.index = index;
    req.gpio_flags = flags;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::GpioSetAltFunction(uint32_t index, uint64_t function) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_SET_ALT_FUNCTION;
    req.index = index;
    req.gpio_alt_function = function;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::GpioGetInterrupt(uint32_t index, uint32_t flags,
                                            zx_handle_t* out_handle) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_GET_INTERRUPT;
    req.index = index;
    req.flags = flags;
    pdev_resp_t resp;

    return Rpc(&req, sizeof(req), &resp, sizeof(resp), nullptr, 0, out_handle, 1, nullptr);
}

zx_status_t PlatformProxy::GpioSetPolarity(uint32_t index, uint32_t polarity) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_SET_POLARITY;
    req.index = index;
    req.flags = polarity;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::GpioReleaseInterrupt(uint32_t index) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_RELEASE_INTERRUPT;
    req.index = index;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::GpioRead(uint32_t index, uint8_t* out_value) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_READ;
    req.index = index;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);

    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.gpio_value;
    return ZX_OK;
}

zx_status_t PlatformProxy::GpioWrite(uint32_t index, uint8_t value) {
    pdev_req_t req = {};
    req.op = PDEV_GPIO_WRITE;
    req.index = index;
    req.gpio_value = value;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value) {
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_SENSOR_VALUE;
    req.scpi.sensor_id = sensor_id;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status == ZX_OK) {
        *sensor_value = resp.scpi.sensor_value;
    }
    return status;
}

zx_status_t PlatformProxy::ScpiGetSensor(const char* name, uint32_t* sensor_id) {
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_SENSOR;
    uint32_t max_len = sizeof(req.scpi.name);
    size_t len = strnlen(name, max_len);
    if (len == max_len) {
        return ZX_ERR_INVALID_ARGS;
    }
    memcpy(&req.scpi.name, name, len + 1);
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status == ZX_OK) {
        *sensor_id = resp.scpi.sensor_id;
    }
    return status;
}

zx_status_t PlatformProxy::ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps) {
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_DVFS_INFO;
    req.scpi.power_domain = power_domain;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status == ZX_OK) {
        memcpy(opps, &resp.scpi.opps, sizeof(scpi_opp_t));
    }
    return status;
}

zx_status_t PlatformProxy::ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx) {
    pdev_req_t req = {};
    req.op = PDEV_SCPI_GET_DVFS_IDX;
    req.scpi.power_domain = power_domain;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status == ZX_OK) {
        *idx = resp.scpi.dvfs_idx;
    }
    return status;
}

zx_status_t PlatformProxy::ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx) {
    pdev_req_t req = {};
    req.op = PDEV_SCPI_SET_DVFS_IDX;
    req.index = idx;
    req.scpi.power_domain = power_domain;
    pdev_resp_t resp;
    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
                                        uint8_t* canvas_idx) {
    zx_status_t status = ZX_OK;
    pdev_resp_t resp;
    pdev_req_t req = {};
    req.op = PDEV_CANVAS_CONFIG;

    memcpy((void*)&req.canvas.info, info, sizeof(canvas_info_t));
    req.canvas.offset = offset;

    status = Rpc(&req, sizeof(req), &resp, sizeof(resp), &vmo, 1, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *canvas_idx = resp.canvas_idx;
    }
    return status;
}

zx_status_t PlatformProxy::CanvasFree(uint8_t canvas_idx) {
    pdev_req_t req = {};
    req.op = PDEV_CANVAS_FREE;
    req.canvas_idx = canvas_idx;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::I2cGetMaxTransferSize(uint32_t index, size_t* out_size) {
    pdev_req_t req = {};
    req.op = PDEV_I2C_GET_MAX_TRANSFER;
    req.index = index;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status == ZX_OK) {
        *out_size = resp.i2c_max_transfer;
    }
    return status;
}

zx_status_t PlatformProxy::I2cTransact(uint32_t index, const void* write_buf, size_t write_length,
                                       size_t read_length, i2c_complete_cb complete_cb,
                                       void* cookie) {
    if (!read_length && !write_length) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (write_length > PDEV_I2C_MAX_TRANSFER_SIZE ||
        read_length > PDEV_I2C_MAX_TRANSFER_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    struct {
        pdev_req_t req;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } req = {
        .req = {
            .txid = 0,
            .op = PDEV_I2C_TRANSACT,
            .index = index,
            .i2c_txn = {
                .write_length = write_length,
                .read_length = read_length,
                .complete_cb = complete_cb,
                .cookie = cookie,
            },
        },
        .data = {},
    };
    struct {
        pdev_resp_t resp;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } resp;

    if (write_length) {
        memcpy(req.data, write_buf, write_length);
    }
    uint32_t data_received;
    auto status = Rpc(&req.req, static_cast<uint32_t>(sizeof(req.req) + write_length),
                      &resp.resp, sizeof(resp), nullptr, 0, nullptr, 0, &data_received);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
    // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
    // In the future we may want to redo the plumbing to allow this to be truly asynchronous.
    if (data_received != read_length) {
        status = ZX_ERR_INTERNAL;
    } else {
        status = resp.resp.status;
    }
    if (complete_cb) {
        complete_cb(status, resp.data, resp.resp.i2c_txn.cookie);
    }

    return ZX_OK;
}

zx_status_t PlatformProxy::ClkEnable(uint32_t index) {
    pdev_req_t req = {};
    req.op = PDEV_CLK_ENABLE;
    req.index = index;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::ClkDisable(uint32_t index) {
    pdev_req_t req = {};
    req.op = PDEV_CLK_DISABLE;
    req.index = index;
    pdev_resp_t resp;

    return Rpc(&req, &resp);
}

zx_status_t PlatformProxy::MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                                   size_t* out_size, zx_paddr_t* out_paddr,
                                   zx_handle_t* out_handle) {
    if (index > mmios_.size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    Mmio* mmio = &mmios_[index];
    zx_paddr_t vmo_base = ROUNDDOWN(mmio->base, PAGE_SIZE);
    size_t vmo_size = ROUNDUP(mmio->base + mmio->length - vmo_base, PAGE_SIZE);
    zx_handle_t vmo_handle;

    zx_status_t status = zx_vmo_create_physical(mmio->resource.get(), vmo_base, vmo_size,
                                                &vmo_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: creating vmo failed %d\n", name_, __FUNCTION__, status);
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "%s mmio %u", name_, index);
    status = zx_object_set_property(vmo_handle, ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: setting vmo name failed %d\n", name_, __FUNCTION__, status);
        goto fail;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: setting cache policy failed %d\n", name_, __FUNCTION__,status);
        goto fail;
    }

    uintptr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s %s: mapping vmar failed %d\n", name_, __FUNCTION__, status);
        goto fail;
    }

    *out_size = mmio->length;
    if (out_paddr) {
        *out_paddr = mmio->base;
    }
    *out_vaddr = (void *)(virt + (mmio->base - vmo_base));
    *out_handle = vmo_handle;
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

zx_status_t PlatformProxy::MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
    if (index > irqs_.size()) {
        return ZX_ERR_INVALID_ARGS;
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

zx_status_t PlatformProxy::GetBti(uint32_t index, zx_handle_t* out_handle) {
    pdev_req_t req = {};
    req.op = PDEV_GET_BTI;
    req.index = index;
    pdev_resp_t resp;

    return Rpc(&req, sizeof(req), &resp, sizeof(resp), nullptr, 0, out_handle, 1, nullptr);
}

zx_status_t PlatformProxy::GetDeviceInfo(pdev_device_info_t* out_info) {
    pdev_req_t req = {};
    req.op = PDEV_GET_DEVICE_INFO;
    pdev_resp_t resp;

    auto status = Rpc(&req, &resp);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t PlatformProxy::Create(zx_device_t* parent, const char* name, zx_handle_t rpc_channel) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::PlatformProxy> proxy(new (&ac)
                                                 platform_bus::PlatformProxy(parent, rpc_channel));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = proxy->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = proxy.release();
    return ZX_OK;
}

zx_status_t PlatformProxy::Init() {
    pdev_device_info_t info;
    auto status = GetDeviceInfo(&info);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(name_, info.name, sizeof(name_));

    fbl::AllocChecker ac;

    if (info.mmio_count) {
        for (uint32_t i = 0; i < info.mmio_count; i++) {
            pdev_req_t req = {};
            pdev_resp_t resp;
            zx_handle_t rsrc_handle;
        
            req.op = PDEV_GET_MMIO;
            req.index = i;
            status = Rpc(&req, sizeof(req), &resp, sizeof(resp), NULL, 0, &rsrc_handle, 1, NULL);
            if (status != ZX_OK) {
                return status;
            }

            Mmio mmio;
            mmio.base = resp.mmio.paddr;
            mmio.length = resp.mmio.length;
            mmio.resource.reset(rsrc_handle);            
            mmios_.push_back(fbl::move(mmio), &ac);
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }

            zxlogf(SPEW, "%s: received MMIO %u (base %#lx length %#lx handle %#x)\n", name_, i,
                   mmio.base, mmio.length, mmio.resource.get());
        }
    }
    
    if (info.irq_count) {
        for (uint32_t i = 0; i < info.irq_count; i++) {
            pdev_req_t req = {};
            pdev_resp_t resp;
            zx_handle_t rsrc_handle;

            req.op = PDEV_GET_INTERRUPT;
            req.index = i;
            status = Rpc(&req, sizeof(req), &resp, sizeof(resp), NULL, 0, &rsrc_handle, 1, NULL);
            if (status != ZX_OK) {
                return status;
            }

            Irq irq;
            irq.irq = resp.irq.irq;
            irq.mode = resp.irq.mode;
            irq.resource.reset(rsrc_handle);
            irqs_.push_back(fbl::move(irq), &ac);
            if (!ac.check()) {
                return ZX_ERR_NO_MEMORY;
            }

            zxlogf(SPEW, "%s: received IRQ %u (irq %#x handle %#x)\n", name_, i, irq.irq,
                   irq.resource.get());
        }
    }

    return DdkAdd(name_);
}

zx_status_t PlatformProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
    auto* proto = static_cast<ddk::AnyProtocol*>(out);
    proto->ctx = this;

    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_DEV: {
        proto->ops = &pdev_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        proto->ops = &usb_mode_switch_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO: {
        proto->ops = &gpio_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_I2C: {
        proto->ops = &i2c_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_CLK: {
        proto->ops = &clk_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_SCPI: {
        proto->ops = &scpi_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_CANVAS: {
        proto->ops = &canvas_proto_ops_;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

void PlatformProxy::DdkRelease() {
    delete this;
}

} // namespace platform_bus

zx_status_t platform_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    return platform_bus::PlatformProxy::Create(parent, name, rpc_channel);
}
