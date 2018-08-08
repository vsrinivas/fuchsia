// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-device.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/protocol/platform-defs.h>
#include <zircon/syscalls/resource.h>

#include "platform-bus.h"
#include "proxy-protocol.h"

namespace platform_bus {

zx_status_t PlatformDevice::Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                                   uint32_t flags,
                                   fbl::unique_ptr<platform_bus::PlatformDevice>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::PlatformDevice> dev(new (&ac)
                                            platform_bus::PlatformDevice(parent, bus, flags, pdev));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto status = dev->Init(pdev);
    if (status != ZX_OK) {
        return status;
    }
    out->swap(dev);
    return ZX_OK;
}

PlatformDevice::PlatformDevice(zx_device_t* parent, PlatformBus* bus, uint32_t flags,
                               const pbus_dev_t* pdev)
    : PlatformDeviceType(parent), bus_(bus), flags_(flags), vid_(pdev->vid), pid_(pdev->pid),
      did_(pdev->did), serial_port_info_(pdev->serial_port_info) {
    strlcpy(name_, pdev->name, sizeof(name_));
}

zx_status_t PlatformDevice::Init(const pbus_dev_t* pdev) {
    fbl::AllocChecker ac;

    if (pdev->mmio_count) {
        mmios_.reserve(pdev->mmio_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->mmio_count; i++) {
            mmios_.push_back(pdev->mmios[i]);
        }
    }
    if (pdev->irq_count) {
        irqs_.reserve(pdev->irq_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->irq_count; i++) {
            irqs_.push_back(pdev->irqs[i]);
        }
    }
    if (pdev->gpio_count) {
        gpios_.reserve(pdev->gpio_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->gpio_count; i++) {
            gpios_.push_back(pdev->gpios[i]);
        }
    }
    if (pdev->i2c_channel_count) {
        i2c_channels_.reserve(pdev->i2c_channel_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->i2c_channel_count; i++) {
            i2c_channels_.push_back(pdev->i2c_channels[i]);
        }
    }
    if (pdev->clk_count) {
        clks_.reserve(pdev->clk_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->clk_count; i++) {
            clks_.push_back(pdev->clks[i]);
        }
    }
    if (pdev->bti_count) {
        btis_.reserve(pdev->bti_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->bti_count; i++) {
            btis_.push_back(pdev->btis[i]);
        }
    }
    if (pdev->metadata_count) {
        metadata_.reserve(pdev->metadata_count, &ac);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        for (uint32_t i = 0; i < pdev->metadata_count; i++) {
            metadata_.push_back(pdev->metadata[i]);
        }
    }

    return ZX_OK;
}


zx_status_t PlatformDevice::MapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                                    size_t* out_size, zx_paddr_t* out_paddr,
                                    zx_handle_t* out_handle) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const pbus_mmio_t& mmio = mmios_[index];
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, PAGE_SIZE);
    zx_handle_t vmo_handle;
    zx_status_t status = zx_vmo_create_physical(bus_->GetResource(), vmo_base, vmo_size,
                                                &vmo_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    status = zx_vmo_set_cache_policy(vmo_handle, cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        goto fail;
    }

    uintptr_t virt;
    status = zx_vmar_map(zx_vmar_root_self(), 0, vmo_handle, 0, vmo_size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE | ZX_VM_FLAG_MAP_RANGE,
                         &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        goto fail;
    }

    *out_size = mmio.length;
    *out_handle = vmo_handle;
    if (out_paddr) {
        *out_paddr = vmo_base;
    }
    *out_vaddr = reinterpret_cast<void*>(virt + (mmio.base - vmo_base));
    return ZX_OK;

fail:
    zx_handle_close(vmo_handle);
    return status;
}

zx_status_t PlatformDevice::MapInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_handle) {
    if (index >= irqs_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_handle == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    pbus_irq_t& irq = irqs_[index];
    if (flags == 0) {
        flags = irq.mode;
    }
    zx_status_t status = zx_interrupt_create(bus_->GetResource(), irq.irq, flags, out_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_create failed %d\n", status);
        return status;
    }
    return status;
}

zx_status_t PlatformDevice::GetBti(uint32_t index, zx_handle_t* out_handle) {
    if (index >= btis_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_handle == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    pbus_bti_t& bti = btis_[index];

    return bus_->GetBti(bti.iommu_index, bti.bti_id, out_handle);
}

zx_status_t PlatformDevice::GetDeviceInfo(pdev_device_info_t* out_info) {
    memset(out_info, 0, sizeof(*out_info));
    out_info->vid = vid_;
    out_info->pid = pid_;
    out_info->did = did_;
    memcpy(&out_info->serial_port_info, &serial_port_info_, sizeof(out_info->serial_port_info));
    out_info->mmio_count = static_cast<uint32_t>(mmios_.size());
    out_info->irq_count = static_cast<uint32_t>(irqs_.size());
    out_info->gpio_count = static_cast<uint32_t>(gpios_.size());
    out_info->i2c_channel_count = static_cast<uint32_t>(i2c_channels_.size());
    out_info->clk_count = static_cast<uint32_t>(clks_.size());
    out_info->bti_count = static_cast<uint32_t>(btis_.size());
    out_info->metadata_count = static_cast<uint32_t>(metadata_.size());
    memcpy(out_info->name, name_, sizeof(out_info->name));

    return ZX_OK;
}

zx_status_t PlatformDevice::GetBoardInfo(pdev_board_info_t* out_info) {
    return bus_->GetBoardInfo(out_info);
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create/map the VMO in the driver process.
zx_status_t PlatformDevice::RpcGetMmio(uint32_t index, zx_paddr_t* out_paddr, size_t *out_length,
                                       zx_handle_t* out_handle, uint32_t* out_handle_count) {
    if (index >= mmios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    pbus_mmio_t* mmio = &mmios_[index];
    zx_handle_t handle;
    char rsrc_name[ZX_MAX_NAME_LEN];
    snprintf(rsrc_name, ZX_MAX_NAME_LEN-1, "%s.pbus[%u]", name_, index);
    zx_status_t status = zx_resource_create(bus_->GetResource(), ZX_RSRC_KIND_MMIO, mmio->base,
                                            mmio->length, rsrc_name, sizeof(rsrc_name), &handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: pdev_rpc_get_mmio: zx_resource_create failed: %d\n", name_, status);
        return status;
    }

    *out_paddr = mmio->base;
    *out_length = mmio->length;
    *out_handle_count = 1;
    *out_handle = handle;
    return ZX_OK;
}

// Create a resource and pass it back to the proxy along with necessary metadata
// to create the IRQ in the driver process.
zx_status_t PlatformDevice::RpcGetInterrupt(uint32_t index, uint32_t* out_irq, uint32_t* out_mode,
                                            zx_handle_t* out_handle, uint32_t* out_handle_count) {
    if (index >= irqs_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_handle_t handle;
    pbus_irq_t* irq = &irqs_[index];
    uint32_t options = ZX_RSRC_KIND_IRQ | ZX_RSRC_FLAG_EXCLUSIVE;
    char rsrc_name[ZX_MAX_NAME_LEN];
    snprintf(rsrc_name, ZX_MAX_NAME_LEN-1, "%s.pbus[%u]", name_, index);
    zx_status_t status = zx_resource_create(bus_->GetResource(), options, irq->irq, 1, rsrc_name,
                                            sizeof(rsrc_name), &handle);
    if (status != ZX_OK) {
        return status;
    }

    *out_irq = irq->irq;
    *out_mode = irq->mode;
    *out_handle_count = 1;
    *out_handle = handle;
    return status;
}

zx_status_t PlatformDevice::RpcGetBti(uint32_t index, zx_handle_t* out_handle,
                                      uint32_t* out_handle_count) {
    zx_status_t status = GetBti(index, out_handle);
    if (status == ZX_OK) {
        *out_handle_count = 1;
    }
    return status;
}

zx_status_t PlatformDevice::RpcUmsSetMode(usb_mode_t mode) {
    if (bus_->ums() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->ums()->SetMode(mode);
}

zx_status_t PlatformDevice::RpcGpioConfig(uint32_t index, uint32_t flags) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->gpio()->Config(gpios_[index].gpio, flags);
}

zx_status_t PlatformDevice::RpcGpioSetAltFunction(uint32_t index, uint64_t function) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->gpio()->SetAltFunction(gpios_[index].gpio, function);
}

zx_status_t PlatformDevice::RpcGpioRead(uint32_t index, uint8_t* out_value) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->gpio()->Read(gpios_[index].gpio, out_value);
}

zx_status_t PlatformDevice::RpcGpioWrite(uint32_t index, uint8_t value) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->gpio()->Write(gpios_[index].gpio, value);
}

zx_status_t PlatformDevice::RpcGpioGetInterrupt(uint32_t index, uint32_t flags,
                                                zx_handle_t* out_handle,
                                                uint32_t* out_handle_count) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    zx_status_t status = bus_->gpio()->GetInterrupt(gpios_[index].gpio, flags, out_handle);
    if (status == ZX_OK) {
        *out_handle_count = 1;
    }
    return status;
}

zx_status_t PlatformDevice::RpcGpioReleaseInterrupt(uint32_t index) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return bus_->gpio()->ReleaseInterrupt(gpios_[index].gpio);
}

zx_status_t PlatformDevice::RpcGpioSetPolarity(uint32_t index, uint32_t flags) {
    if (bus_->gpio() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= gpios_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return bus_->gpio()->SetPolarity(gpios_[index].gpio, flags);
}

zx_status_t PlatformDevice::RpcCanvasConfig(zx_handle_t vmo, size_t offset,
                                            canvas_info_t* info, uint8_t* canvas_idx) {
    if (bus_->canvas() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->canvas()->Config(vmo, offset, info, canvas_idx);
}

zx_status_t PlatformDevice::RpcCanvasFree(uint8_t canvas_idx) {
    if (bus_->canvas() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->canvas()->Free(canvas_idx);
}

zx_status_t PlatformDevice::RpcScpiGetSensor(char* name, uint32_t *sensor_id) {
    if (bus_->scpi() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->scpi()->GetSensor(name, sensor_id);
}

zx_status_t PlatformDevice::RpcScpiGetSensorValue(uint32_t sensor_id, uint32_t* sensor_value) {
    if (bus_->scpi() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->scpi()->GetSensorValue(sensor_id, sensor_value);
}

zx_status_t PlatformDevice::RpcScpiGetDvfsInfo(uint8_t power_domain, scpi_opp_t* opps) {
    if (bus_->scpi() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->scpi()->GetDvfsInfo(power_domain, opps);
}

zx_status_t PlatformDevice::RpcScpiGetDvfsIdx(uint8_t power_domain, uint16_t* idx) {
    if (bus_->scpi() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->scpi()->GetDvfsIdx(power_domain, idx);
}

zx_status_t PlatformDevice::RpcScpiSetDvfsIdx(uint8_t power_domain, uint16_t idx) {
    if (bus_->scpi() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    return bus_->scpi()->SetDvfsIdx(power_domain, idx);
}

zx_status_t PlatformDevice::RpcI2cTransact(pdev_req_t* req, uint8_t* data, zx_handle_t channel) {
    if (bus_->i2c_impl() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    uint32_t index = req->index;
    if (index >= i2c_channels_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    pbus_i2c_channel_t* pdev_channel = &i2c_channels_[index];

    return bus_->I2cTransact(req, pdev_channel, data, channel);
}

zx_status_t PlatformDevice::RpcI2cGetMaxTransferSize(uint32_t index, size_t* out_size) {
    if (bus_->i2c_impl() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= i2c_channels_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    pbus_i2c_channel_t* pdev_channel = &i2c_channels_[index];

    return bus_->i2c_impl()->GetMaxTransferSize(pdev_channel->bus_id, out_size);
}

zx_status_t PlatformDevice::RpcClkEnable(uint32_t index) {
    if (bus_->clk() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= clks_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->clk()->Enable(clks_[index].clk);
}

zx_status_t PlatformDevice::RpcDisable(uint32_t index) {
    if (bus_->clk() == nullptr) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (index >= clks_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return bus_->clk()->Disable(clks_[index].clk);
}

zx_status_t PlatformDevice::DdkRxrpc(zx_handle_t channel) {
    if (channel == ZX_HANDLE_INVALID) {
        // proxy device has connected
        return ZX_OK;
    }

    struct {
        pdev_req_t req;
        uint8_t data[PDEV_I2C_MAX_TRANSFER_SIZE];
    } req_data;
    pdev_req_t* req = &req_data.req;
    pdev_resp_t resp;
    uint32_t len = sizeof(req_data);
    zx_handle_t in_handle;
    uint32_t in_handle_count = 1;

    zx_status_t status = zx_channel_read(channel, 0, &req_data, &in_handle, len, in_handle_count,
                                        &len, &in_handle_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_read failed %d\n", status);
        return status;
    }

    resp.txid = req->txid;
    zx_handle_t handle = ZX_HANDLE_INVALID;
    uint32_t handle_count = 0;

    switch (req->op) {
    case PDEV_GET_MMIO:
        resp.status = RpcGetMmio(req->index, &resp.mmio.paddr, &resp.mmio.length, &handle,
                                 &handle_count);
        break;
    case PDEV_GET_INTERRUPT:
        resp.status = RpcGetInterrupt(req->index, &resp.irq.irq, &resp.irq.mode, &handle,
                                     &handle_count);
        break;
    case PDEV_GET_BTI:
        resp.status = RpcGetBti(req->index, &handle, &handle_count);
        break;
    case PDEV_GET_DEVICE_INFO:
        resp.status = GetDeviceInfo(&resp.device_info);
        break;
    case PDEV_GET_BOARD_INFO:
        resp.status = bus_->GetBoardInfo(&resp.board_info);
        break;
    case PDEV_UMS_SET_MODE:
        resp.status = RpcUmsSetMode(req->usb_mode);
        break;
    case PDEV_GPIO_CONFIG:
        resp.status = RpcGpioConfig(req->index, req->gpio_flags);
        break;
    case PDEV_GPIO_SET_ALT_FUNCTION:
        resp.status = RpcGpioSetAltFunction(req->index, req->gpio_alt_function);
        break;
    case PDEV_GPIO_READ:
        resp.status = RpcGpioRead(req->index, &resp.gpio_value);
        break;
    case PDEV_GPIO_WRITE:
        resp.status = RpcGpioWrite(req->index, req->gpio_value);
        break;
    case PDEV_GPIO_GET_INTERRUPT:
        resp.status = RpcGpioGetInterrupt(req->index, req->flags, &handle, &handle_count);
        break;
    case PDEV_GPIO_RELEASE_INTERRUPT:
        resp.status = RpcGpioReleaseInterrupt(req->index);
        break;
    case PDEV_GPIO_SET_POLARITY:
        resp.status = RpcGpioSetPolarity(req->index, req->flags);
        break;
    case PDEV_SCPI_GET_SENSOR:
        resp.status = RpcScpiGetSensor(req->scpi.name, &resp.scpi.sensor_id);
        break;
    case PDEV_SCPI_GET_SENSOR_VALUE:
        resp.status = RpcScpiGetSensorValue(req->scpi.sensor_id, &resp.scpi.sensor_value);
        break;
    case PDEV_SCPI_GET_DVFS_INFO:
        resp.status = RpcScpiGetDvfsInfo(req->scpi.power_domain, &resp.scpi.opps);
        break;
    case PDEV_SCPI_GET_DVFS_IDX:
        resp.status = RpcScpiGetDvfsIdx(req->scpi.power_domain, &resp.scpi.dvfs_idx);
        break;
    case PDEV_SCPI_SET_DVFS_IDX:
        resp.status = RpcScpiSetDvfsIdx(req->scpi.power_domain, static_cast<uint16_t>(req->index));
        break;
    case PDEV_I2C_GET_MAX_TRANSFER:
        resp.status = RpcI2cGetMaxTransferSize(req->index, &resp.i2c_max_transfer);
        break;
    case PDEV_I2C_TRANSACT:
        resp.status = RpcI2cTransact(req, req_data.data, channel);
        if (resp.status == ZX_OK) {
            // If platform_i2c_transact succeeds, we return immmediately instead of calling
            // zx_channel_write below. Instead we will respond in platform_i2c_complete().
            return ZX_OK;
        }
        break;
    case PDEV_CLK_ENABLE:
        resp.status = RpcClkEnable(req->index);
        break;
    case PDEV_CLK_DISABLE:
        resp.status = RpcDisable(req->index);
        break;
    case PDEV_CANVAS_CONFIG:
        resp.status = RpcCanvasConfig(in_handle, req->canvas.offset, &req->canvas.info,
                                      &resp.canvas_idx);
        break;
    case PDEV_CANVAS_FREE:
        resp.status = RpcCanvasFree(req->canvas_idx);
        break;
    default:
        zxlogf(ERROR, "platform_dev_rxrpc: unknown op %u\n", req->op);
        return ZX_ERR_INTERNAL;
    }

    // set op to match request so zx_channel_write will return our response
    status = zx_channel_write(channel, 0, &resp, sizeof(resp),
                              (handle_count == 1 ? &handle : nullptr), handle_count);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_rxrpc: zx_channel_write failed %d\n", status);
    }
    return status;
}

zx_status_t PlatformDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
    if (proto_id == ZX_PROTOCOL_PLATFORM_DEV) {
        auto proto = static_cast<platform_device_protocol_t*>(out);
        proto->ops = &pdev_proto_ops_;
        proto->ctx = this;
        return ZX_OK;
    } else {
        return bus_->DdkGetProtocol(proto_id, out);
    }
}

void PlatformDevice::DdkRelease() {
    delete this;
}

zx_status_t PlatformDevice::AddMetaData(const pbus_metadata_t& pbm) {
    const uint32_t type = pbm.type;
    const uint32_t extra = pbm.extra;
    const uint8_t* metadata = bus_->metadata();
    const size_t metadata_size = bus_->metadata_size();
    size_t offset = 0;

    while (offset < metadata_size) {
        auto header = reinterpret_cast<const zbi_header_t*>(metadata);
        size_t length = ZBI_ALIGN(sizeof(zbi_header_t) + header->length);

        if (header->type == type && header->extra == extra) {
            return DdkAddMetadata(type, header + 1, length - sizeof(zbi_header_t));
        }
        metadata += length;
        offset += length;
    }
    zxlogf(ERROR, "%s metadata not found for type %08x, extra %u\n", __FUNCTION__, type, extra);
    return ZX_ERR_NOT_FOUND;
}

zx_status_t PlatformDevice::Enable(bool enable) {
    zx_status_t status = ZX_OK;

    if (enable && !enabled_) {
        zx_device_prop_t props[] = {
            {BIND_PLATFORM_DEV_VID, 0, vid_},
            {BIND_PLATFORM_DEV_PID, 0, pid_},
            {BIND_PLATFORM_DEV_DID, 0, did_},
        };

        char namestr[ZX_DEVICE_NAME_MAX];
        if (vid_ == PDEV_VID_GENERIC && pid_ == PDEV_PID_GENERIC && did_ == PDEV_DID_KPCI) {
            strlcpy(namestr, "pci", sizeof(namestr));
        } else {

            snprintf(namestr, sizeof(namestr), "%02x:%02x:%01x", vid_, pid_, did_);
        }
        char argstr[64];
        snprintf(argstr, sizeof(argstr), "pdev:%s,", namestr);
        uint32_t flags = 0;
        if (!(flags_ & PDEV_ADD_PBUS_DEVHOST)) {
            flags |= DEVICE_ADD_MUST_ISOLATE;
        }
        if (metadata_.size() > 0) {
            flags |= DEVICE_ADD_INVISIBLE;
        }

        status = DdkAdd(namestr, flags, props, countof(props), argstr);

        if (metadata_.size() > 0) {
            for (const auto& pbm : metadata_) {
                if (pbm.data && pbm.len) {
                    DdkAddMetadata(pbm.type, pbm.data, pbm.len);
                } else {
                    AddMetaData(pbm);
                }
            }
            DdkMakeVisible();
        }
     } else if (!enable && enabled_) {
        DdkRemove();
    }

    if (status == ZX_OK) {
        enabled_ = enable;
    }

    return status;
}

} // namespace platform_bus
