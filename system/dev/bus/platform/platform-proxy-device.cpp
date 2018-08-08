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
// More information can be found at the top of platform-device.cpp.

namespace platform_bus {

zx_status_t ProxyDevice::UmsSetMode(usb_mode_t mode) {
    rpc_ums_req_t req = {};
    req.header.protocol = ZX_PROTOCOL_USB_MODE_SWITCH;
    req.header.op = UMS_SET_MODE;
    req.usb_mode = mode;
    rpc_rsp_header_t resp;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyDevice::GpioConfig(uint32_t index, uint32_t flags) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_CONFIG;
    req.index = index;
    req.flags = flags;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::GpioSetAltFunction(uint32_t index, uint64_t function) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_ALT_FUNCTION;
    req.index = index;
    req.alt_function = function;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::GpioGetInterrupt(uint32_t index, uint32_t flags,
                                            zx_handle_t* out_handle) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_GET_INTERRUPT;
    req.index = index;
    req.flags = flags;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr,
                       0, out_handle, 1, nullptr);
}

zx_status_t ProxyDevice::GpioSetPolarity(uint32_t index, uint32_t polarity) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_SET_POLARITY;
    req.index = index;
    req.polarity = polarity;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::GpioReleaseInterrupt(uint32_t index) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_RELEASE_INTERRUPT;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::GpioRead(uint32_t index, uint8_t* out_value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_READ;
    req.index = index;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));

    if (status != ZX_OK) {
        return status;
    }
    *out_value = resp.value;
    return ZX_OK;
}

zx_status_t ProxyDevice::GpioWrite(uint32_t index, uint8_t value) {
    rpc_gpio_req_t req = {};
    rpc_gpio_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_GPIO;
    req.header.op = GPIO_WRITE;
    req.index = index;
    req.value = value;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::ScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value) {
    rpc_scpi_req_t req = {};
    rpc_scpi_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_SCPI;
    req.header.op = SCPI_GET_SENSOR_VALUE;
    req.sensor_id = sensor_id;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        *sensor_value = resp.sensor_value;
    }
    return status;
}

zx_status_t ProxyDevice::ScpiGetSensor(const char* name, uint32_t* sensor_id) {
    rpc_scpi_req_t req = {};
    rpc_scpi_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_SCPI;
    req.header.op = SCPI_GET_SENSOR;
    uint32_t max_len = sizeof(req.name);
    size_t len = strnlen(name, max_len);
    if (len >= max_len) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(&req.name, name, len + 1);

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        *sensor_id = resp.sensor_id;
    }
    return status;
}

zx_status_t ProxyDevice::ScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps) {
    rpc_scpi_req_t req = {};
    rpc_scpi_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_SCPI;
    req.header.op = SCPI_GET_DVFS_INFO;
    req.power_domain = power_domain;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        memcpy(opps, &resp.opps, sizeof(scpi_opp_t));
    }
    return status;
}

zx_status_t ProxyDevice::ScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx) {
    rpc_scpi_req_t req = {};
    rpc_scpi_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_SCPI;
    req.header.op = SCPI_GET_DVFS_IDX;
    req.power_domain = power_domain;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        *idx = resp.dvfs_idx;
    }
    return status;
}

zx_status_t ProxyDevice::ScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx) {
    rpc_scpi_req_t req = {};
    rpc_scpi_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_SCPI;
    req.header.op = SCPI_SET_DVFS_IDX;
    req.power_domain = power_domain;
    req.idx = idx;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::CanvasConfig(zx_handle_t vmo, size_t offset, canvas_info_t* info,
                                      uint8_t* canvas_idx) {
    rpc_canvas_req_t req = {};
    rpc_canvas_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_CANVAS;
    req.header.op = CANVAS_CONFIG;

    memcpy((void*)&req.info, info, sizeof(canvas_info_t));
    req.offset = offset;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp),
                              &vmo, 1, nullptr, 0, nullptr);
    if (status == ZX_OK) {
        *canvas_idx = resp.idx;
    }
    return status;
}

zx_status_t ProxyDevice::CanvasFree(uint8_t canvas_idx) {
    rpc_canvas_req_t req = {};
    rpc_canvas_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_CANVAS;
    req.header.op = CANVAS_FREE;
    req.idx = canvas_idx;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
}

zx_status_t ProxyDevice::I2cGetMaxTransferSize(uint32_t index, size_t* out_size) {
    rpc_i2c_req_t req = {};
    rpc_i2c_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_I2C;
    req.header.op = I2C_GET_MAX_TRANSFER;
    req.index = index;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status == ZX_OK) {
        *out_size = resp.max_transfer;
    }
    return status;
}

zx_status_t ProxyDevice::I2cTransact(uint32_t index, const void* write_buf, size_t write_length,
                                     size_t read_length, i2c_complete_cb complete_cb,
                                     void* cookie) {
    if (!read_length && !write_length) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (write_length > I2C_MAX_TRANSFER_SIZE || read_length > I2C_MAX_TRANSFER_SIZE) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    struct {
        rpc_i2c_req_t i2c;
        uint8_t data[I2C_MAX_TRANSFER_SIZE];
    } req = {};
    req.i2c.header.protocol = ZX_PROTOCOL_I2C;
    req.i2c.header.op = I2C_TRANSACT;
    req.i2c.index = index;
    req.i2c.write_length = write_length;
    req.i2c.read_length = read_length;
    req.i2c.complete_cb = complete_cb;
    req.i2c.cookie = cookie;
    struct {
        rpc_i2c_rsp_t i2c;
        uint8_t data[I2C_MAX_TRANSFER_SIZE];
    } resp;

    if (write_length) {
        memcpy(req.data, write_buf, write_length);
    }
    uint32_t actual;
    auto status = proxy_->Rpc(device_id_, &req.i2c.header,
                              static_cast<uint32_t>(sizeof(req.i2c) + write_length),
                              &resp.i2c.header, sizeof(resp), nullptr, 0, nullptr, 0, &actual);
    if (status != ZX_OK) {
        return status;
    }

    // TODO(voydanoff) This proxying code actually implements i2c_transact synchronously
    // due to the fact that it is unsafe to respond asynchronously on the devmgr rxrpc channel.
    // In the future we may want to redo the plumbing to allow this to be truly asynchronous.

    if (actual - sizeof(resp.i2c) != read_length) {
        status = ZX_ERR_INTERNAL;
    } else {
        status = resp.i2c.header.status;
    }
    if (complete_cb) {
        complete_cb(status, resp.data, resp.i2c.cookie);
    }

    return ZX_OK;
}

zx_status_t ProxyDevice::ClkEnable(uint32_t index) {
    rpc_clk_req_t req = {};
    rpc_rsp_header_t resp = {};
    req.header.protocol = ZX_PROTOCOL_CLK;
    req.header.op = CLK_ENABLE;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyDevice::ClkDisable(uint32_t index) {
    rpc_clk_req_t req = {};
    rpc_rsp_header_t resp = {};
    req.header.protocol = ZX_PROTOCOL_CLK;
    req.header.op = CLK_DISABLE;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp, sizeof(resp));
}

zx_status_t ProxyDevice::MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                                 size_t* out_size, zx_paddr_t* out_paddr,
                                 zx_handle_t* out_handle) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
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

zx_status_t ProxyDevice::MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
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

zx_status_t ProxyDevice::GetBti(uint32_t index, zx_handle_t* out_handle) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
    req.header.op = PDEV_GET_BTI;
    req.index = index;

    return proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp), nullptr,
                      0, out_handle, 1, nullptr);
}

zx_status_t ProxyDevice::GetDeviceInfo(pdev_device_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
    req.header.op = PDEV_GET_DEVICE_INFO;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.device_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ProxyDevice::GetBoardInfo(pdev_board_info_t* out_info) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
    req.header.op = PDEV_GET_BOARD_INFO;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }
    memcpy(out_info, &resp.board_info, sizeof(*out_info));
    return ZX_OK;
}

zx_status_t ProxyDevice::DeviceAdd(uint32_t index, device_add_args_t* args, zx_device_t** out) {
    rpc_pdev_req_t req = {};
    rpc_pdev_rsp_t resp = {};
    req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
    req.header.op = PDEV_DEVICE_ADD;
    req.index = index;

    auto status = proxy_->Rpc(device_id_, &req.header, sizeof(req), &resp.header, sizeof(resp));
    if (status != ZX_OK) {
        return status;
    }

    // TODO(voydanoff) We need to provide a way for metadata passed from the platform bus
    // to be attached to this new device.
    return Create(zxdev(), resp.device_id, proxy_, args);
}

zx_status_t ProxyDevice::Create(zx_device_t* parent, uint32_t device_id,
                                fbl::RefPtr<PlatformProxy> proxy, device_add_args_t* args) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::ProxyDevice> dev(new (&ac)
                                            platform_bus::ProxyDevice(parent, device_id, proxy));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->Init(args);
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

zx_status_t ProxyDevice::Init(device_add_args_t* args) {
    pdev_device_info_t info;
    auto status = GetDeviceInfo(&info);
    if (status != ZX_OK) {
        return status;
    }
    memcpy(name_, info.name, sizeof(name_));

    fbl::AllocChecker ac;

    if (info.mmio_count) {
        for (uint32_t i = 0; i < info.mmio_count; i++) {
            rpc_pdev_req_t req = {};
            rpc_pdev_rsp_t resp = {};
            zx_handle_t rsrc_handle;

            req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
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
    }

    if (info.irq_count) {
        for (uint32_t i = 0; i < info.irq_count; i++) {
            rpc_pdev_req_t req = {};
            rpc_pdev_rsp_t resp = {};
            zx_handle_t rsrc_handle;

            req.header.protocol = ZX_PROTOCOL_PLATFORM_DEV;
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
    }

    if (args) {
        ctx_ = args->ctx;
        device_ops_ = args->ops;
        proto_id_ = args->proto_id;
        proto_ops_ = args->proto_ops;

        return DdkAdd(args->name, args->flags, args->props, args->prop_count);
    } else {
        return DdkAdd(name_);
    }
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
    case ZX_PROTOCOL_PLATFORM_DEV: {
        proto->ops = &pdev_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        proto->ops = &usb_mode_switch_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_GPIO: {
        proto->ops = &gpio_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_I2C: {
        proto->ops = &i2c_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_CLK: {
        proto->ops = &clk_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_SCPI: {
        proto->ops = &scpi_proto_ops_;
        break;
    }
    case ZX_PROTOCOL_CANVAS: {
        proto->ops = &canvas_proto_ops_;
        break;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }

    proto->ctx = this;
    return ZX_OK;
}

zx_status_t ProxyDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    if (device_ops_ && device_ops_->open) {
        return device_ops_->open(ctx_, dev_out, flags);
    }
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProxyDevice::DdkOpenAt(zx_device_t** dev_out,  const char* path, uint32_t flags) {
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
