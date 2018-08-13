// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-bus.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

#include "platform-device.h"

namespace platform_bus {

zx_status_t PlatformBus::GetBti(uint32_t iommu_index, uint32_t bti_id, zx_handle_t* out_handle) {
    if (iommu_index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_bti_create(iommu_handle_.get(), 0, bti_id, out_handle);
}

zx_status_t PlatformBus::SetProtocol(uint32_t proto_id, void* protocol) {
    if (!protocol) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::AllocChecker ac;

    switch (proto_id) {
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        ums_.reset(new (&ac) ddk::UmsProtocolProxy(
                                    static_cast<usb_mode_switch_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_GPIO: {
        gpio_.reset(new (&ac) ddk::GpioProtocolProxy(static_cast<gpio_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_I2C_IMPL: {
        auto proto = static_cast<i2c_impl_protocol_t*>(protocol);
        auto status = I2cInit(proto);
        if (status != ZX_OK) {
            return status;
         }

        i2c_impl_.reset(new (&ac) ddk::I2cImplProtocolProxy(proto));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_CLK: {
        clk_.reset(new (&ac) ddk::ClkProtocolProxy(static_cast<clk_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_IOMMU: {
        iommu_.reset(new (&ac) ddk::IommuProtocolProxy(static_cast<iommu_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_MAILBOX: {
        mailbox_.reset(new (&ac) ddk::MailboxProtocolProxy(
                                                    static_cast<mailbox_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_SCPI: {
        scpi_.reset(new (&ac) ddk::ScpiProtocolProxy(static_cast<scpi_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    case ZX_PROTOCOL_CANVAS: {
        canvas_.reset(new (&ac) ddk::CanvasProtocolProxy(
                                                    static_cast<canvas_protocol_t*>(protocol)));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        break;
    }
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AutoLock lock(&mutex_);
    sync_completion_signal(&proto_completion_);
    return ZX_OK;
}

zx_status_t PlatformBus::WaitProtocol(uint32_t proto_id) {
    platform_bus_protocol_t dummy;

    mutex_.Acquire();
    while (DdkGetProtocol(proto_id, &dummy) == ZX_ERR_NOT_SUPPORTED) {
        sync_completion_reset(&proto_completion_);
        mutex_.Release();
        zx_status_t status = sync_completion_wait(&proto_completion_, ZX_SEC(10));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s failed for protocol %08x\n", __FUNCTION__, proto_id);
            return status;
        }
        mutex_.Acquire();
    }
    mutex_.Release();
    return ZX_OK;
}

zx_status_t PlatformBus::DeviceAdd(const pbus_dev_t* pdev, uint32_t flags) {
    if (flags & ~(PDEV_ADD_PBUS_DEVHOST)) {
        return ZX_ERR_INVALID_ARGS;
    }
    if (!pdev->name) {
        return ZX_ERR_INVALID_ARGS;
    }

    zx_device_t* parent_dev;
    if (pdev->vid == PDEV_VID_GENERIC && pdev->pid == PDEV_PID_GENERIC &&
        pdev->did == PDEV_DID_KPCI) {
        // Add PCI root at top level.
        parent_dev = parent();
    } else {
        parent_dev = zxdev();
    }

    fbl::unique_ptr<platform_bus::PlatformDevice> dev;
    auto status = PlatformDevice::Create(pdev, parent_dev, this, flags, &dev);
    if (status != ZX_OK) {
        return status;
    }

    size_t index = devices_.size();
    fbl::AllocChecker ac;
    devices_.push_back(fbl::move(dev), &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return devices_[index]->DeviceAdd();
}

const char* PlatformBus::GetBoardName() {
    return board_info_.board_name;
}

zx_status_t PlatformBus::SetBoardInfo(const pbus_board_info_t* info) {
    board_info_.board_revision = info->board_revision;
    return ZX_OK;
}

zx_status_t PlatformBus::GetBoardInfo(pdev_board_info_t* out_info) {
    memcpy(out_info, &board_info_, sizeof(board_info_));
    return ZX_OK;
}

zx_status_t PlatformBus::DdkGetProtocol(uint32_t proto_id, void* protocol) {
    switch (proto_id) {
    case ZX_PROTOCOL_PLATFORM_BUS: {
        auto proto = static_cast<platform_bus_protocol_t*>(protocol);
        proto->ctx = this;
        proto->ops = &pbus_proto_ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH:
        if (ums_ != nullptr) {
            ums_->GetProto(static_cast<usb_mode_switch_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_GPIO:
        if (gpio_ != nullptr) {
            gpio_->GetProto(static_cast<gpio_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_I2C_IMPL:
        if (i2c_impl_ != nullptr) {
            i2c_impl_->GetProto(static_cast<i2c_impl_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_CLK:
        if (clk_ != nullptr) {
            clk_->GetProto(static_cast<clk_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_IOMMU:
        if (iommu_ != nullptr) {
            iommu_->GetProto(static_cast<iommu_protocol_t*>(protocol));
        } else {
            // return default implementation
            auto proto = static_cast<iommu_protocol_t*>(protocol);
            proto->ctx = this;
            proto->ops = &iommu_proto_ops_;
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_MAILBOX:
        if (mailbox_ != nullptr) {
            mailbox_->GetProto(static_cast<mailbox_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_SCPI:
        if (scpi_ != nullptr) {
            scpi_->GetProto(static_cast<scpi_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_CANVAS:
        if (canvas_ != nullptr) {
            canvas_->GetProto(static_cast<canvas_protocol_t*>(protocol));
            return ZX_OK;
        }
        break;
    default:
        // TODO(voydanoff) consider having a registry of arbitrary protocols
        return ZX_ERR_NOT_SUPPORTED;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PlatformBus::ReadZbi(zx::vmo zbi) {
    zbi_header_t header;

    zx_status_t status = zbi.read(&header, 0, sizeof(header));
    if (status != ZX_OK) {
        return status;
    }
    if ((header.type != ZBI_TYPE_CONTAINER) || (header.extra != ZBI_CONTAINER_MAGIC)) {
        zxlogf(ERROR, "platform_bus: ZBI VMO not contain ZBI container\n");
        return ZX_ERR_INTERNAL;
    }

    size_t zbi_length = header.length;

    // compute size of ZBI records we need to save for metadata
    uint8_t* metadata = nullptr;
    size_t metadata_size = 0;
    size_t len = zbi_length;
    size_t off = sizeof(header);

    while (len > sizeof(header)) {
        auto status = zbi.read(&header, off, sizeof(header));
        if (status < 0) {
            zxlogf(ERROR, "zbi.read() failed: %d\n", status);
            return status;
        }
        size_t itemlen = ZBI_ALIGN(sizeof(zbi_header_t) + header.length);
        if (itemlen > len) {
            zxlogf(ERROR, "platform_bus: ZBI item too large (%zd > %zd)\n", itemlen, len);
            break;
        }
        if (ZBI_TYPE_DRV_METADATA(header.type)) {
            metadata_size += itemlen;
        }
        off += itemlen;
        len -= itemlen;
    }

    if (metadata_size) {
        fbl::AllocChecker ac;
        metadata_.reset(new (&ac) uint8_t[metadata_size], metadata_size);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }
        metadata = metadata_.get();
    }

    bool got_platform_id = false;
    zx_off_t metadata_offset = 0;
    len = zbi_length;
    off = sizeof(header);

    // find platform ID record and copy metadata records
    while (len > sizeof(header)) {
        auto status = zbi.read(&header, off, sizeof(header));
        if (status < 0) {
            break;
        }
        const size_t itemlen = ZBI_ALIGN(sizeof(zbi_header_t) + header.length);
        if (itemlen > len) {
            zxlogf(ERROR, "platform_bus: ZBI item too large (%zd > %zd)\n", itemlen, len);
            break;
        }
        if (header.type == ZBI_TYPE_PLATFORM_ID) {
            zbi_platform_id_t platform_id;
            status = zbi.read(&platform_id, off + sizeof(zbi_header_t), sizeof(platform_id));
            if (status != ZX_OK) {
                zxlogf(ERROR, "zbi.read() failed: %d\n", status);
                return status;
            }
            board_info_.vid = platform_id.vid;
            board_info_.pid = platform_id.pid;
            memcpy(board_info_.board_name, platform_id.board_name, sizeof(board_info_.board_name));
            // This is optionally set later by the board driver.
            board_info_.board_revision = 0;
            got_platform_id = true;
        } else if (ZBI_TYPE_DRV_METADATA(header.type)) {
            status = zbi.read(metadata + metadata_offset, off, itemlen);
            if (status != ZX_OK) {
                zxlogf(ERROR, "zbi.read() failed: %d\n", status);
                return status;
            }
            metadata_offset += itemlen;
        }
        off += itemlen;
        len -= itemlen;
    }

    if (!got_platform_id) {
         zxlogf(ERROR, "platform_bus: ZBI_TYPE_PLATFORM_ID not found\n");
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t PlatformBus::I2cInit(i2c_impl_protocol_t* i2c) {
    if (!i2c_buses_.is_empty()) {
        // already initialized
        return ZX_ERR_BAD_STATE;
    }

    uint32_t bus_count = i2c_impl_get_bus_count(i2c);
    if (!bus_count) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    i2c_buses_.reserve(bus_count, &ac);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    for (uint32_t i = 0; i < bus_count; i++) {
        fbl::unique_ptr<PlatformI2cBus> i2c_bus(new (&ac) PlatformI2cBus(i2c, i));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        auto status = i2c_bus->Start();
        if (status != ZX_OK) {
            return status;
        }

        i2c_buses_.push_back(fbl::move(i2c_bus));
    }

    return ZX_OK;
}

zx_status_t PlatformBus::I2cTransact(uint32_t txid, rpc_i2c_req_t* req, pbus_i2c_channel_t* channel,
                                     const void* write_buf, zx_handle_t channel_handle) {
    if (channel->bus_id >= i2c_buses_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    auto i2c_bus = i2c_buses_[channel->bus_id].get();
    return i2c_bus->Transact(txid, req, channel->address, write_buf, channel_handle);
}

void PlatformBus::DdkRelease() {
    delete this;
}

static zx_protocol_device_t sys_device_proto = {};

zx_status_t PlatformBus::Create(zx_device_t* parent, const char* name, zx::vmo zbi) {
    // This creates the "sys" device.
    sys_device_proto.version = DEVICE_OPS_VERSION;

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "sys";
    args.ops = &sys_device_proto;
    args.flags = DEVICE_ADD_NON_BINDABLE;

    // Add child of sys for the board driver to bind to.
    auto status = device_add(parent, &args, &parent);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::PlatformBus> bus(new (&ac) platform_bus::PlatformBus(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = bus->Init(fbl::move(zbi));
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = bus.release();
    return ZX_OK;
}

PlatformBus::PlatformBus(zx_device_t* parent)
        : PlatformBusType(parent) {
    sync_completion_reset(&proto_completion_);
}

zx_status_t PlatformBus::Init(zx::vmo zbi) {
    auto status = ReadZbi(fbl::move(zbi));
    if (status != ZX_OK) {
        return status;
    }

    // Set up a dummy IOMMU protocol to use in the case where our board driver does not
    // set a real one.
    zx_iommu_desc_dummy_t desc;
    status = zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY,  &desc, sizeof(desc),
                             iommu_handle_.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    // Then we attach the platform-bus device below it.
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, board_info_.vid},
        {BIND_PLATFORM_DEV_PID, 0, board_info_.pid},
    };

    return DdkAdd("platform", 0, props, countof(props));
}

} // namespace platform_bus

zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                const char* args, zx_handle_t zbi_vmo_handle) {
    zx::vmo zbi(zbi_vmo_handle);
    return platform_bus::PlatformBus::Create(parent, name, fbl::move(zbi));
}
