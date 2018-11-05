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
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/sysinfo/c/fidl.h>
#include <zircon/boot/driver-config.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/syscalls/iommu.h>

namespace platform_bus {

zx_status_t PlatformBus::Proxy(
    const void* req_buffer, size_t req_size, const zx_handle_t* req_handle_list,
    size_t req_handle_count, void* out_resp_buffer, size_t resp_size, size_t* out_resp_actual,
    zx_handle_t* out_resp_handle_list, size_t resp_handle_count,
    size_t* out_resp_handle_actual) {

    auto* req = static_cast<const platform_proxy_req*>(req_buffer);
    fbl::AutoLock lock(&proto_proxys_mutex_);
    auto proto_proxy = proto_proxys_.find(req->proto_id);
    if (!proto_proxy.IsValid()) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    proto_proxy->Proxy(req_buffer, req_size, req_handle_list, req_handle_count,
                       out_resp_buffer, resp_size, out_resp_actual, out_resp_handle_list,
                       resp_handle_count, out_resp_handle_actual);
    return ZX_OK;
}

zx_status_t PlatformBus::IommuGetBti(uint32_t iommu_index, uint32_t bti_id,
                                     zx_handle_t* out_handle) {
    if (iommu_index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    return zx_bti_create(iommu_handle_.get(), 0, bti_id, out_handle);
}

zx_status_t PlatformBus::PBusRegisterProtocol(uint32_t proto_id, const void* protocol,
                                              size_t protocol_size,
                                              const platform_proxy_cb_t* proxy_cb) {
    if (!protocol || protocol_size < sizeof(ddk::AnyProtocol)) {
        return ZX_ERR_INVALID_ARGS;
    }

    switch (proto_id) {
    case ZX_PROTOCOL_GPIO_IMPL: {
        if (proxy_cb->callback != nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        gpio_ = ddk::GpioImplProtocolProxy(static_cast<const gpio_impl_protocol_t*>(protocol));
        break;
    }
    case ZX_PROTOCOL_I2C_IMPL: {
        if (proxy_cb->callback != nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        auto proto = static_cast<const i2c_impl_protocol_t*>(protocol);
        auto status = I2cInit(proto);
        if (status != ZX_OK) {
            return status;
        }

        i2c_ = ddk::I2cImplProtocolProxy(proto);
        break;
    }
    case ZX_PROTOCOL_CLK: {
        if (proxy_cb->callback != nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        clk_ = ddk::ClkProtocolProxy(static_cast<const clk_protocol_t*>(protocol));
        break;
    }
    case ZX_PROTOCOL_IOMMU: {
        if (proxy_cb->callback != nullptr) {
            return ZX_ERR_INVALID_ARGS;
        }
        iommu_ = ddk::IommuProtocolProxy(static_cast<const iommu_protocol_t*>(protocol));
        break;
    }
    default: {
        if (proxy_cb->callback == nullptr) {
            return ZX_ERR_NOT_SUPPORTED;
        }

        fbl::AutoLock lock(&proto_proxys_mutex_);
        if (proto_proxys_.find(proto_id).IsValid()) {
            zxlogf(ERROR, "%s: protocol %08x has already been registered\n", __func__, proto_id);
            return ZX_ERR_BAD_STATE;
        }

        fbl::AllocChecker ac;
        auto proxy = fbl::make_unique_checked<ProtoProxy>(&ac, proto_id,
                                                          static_cast<const ddk::AnyProtocol*>(protocol),
                                                          *proxy_cb);
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        proto_proxys_.insert(fbl::move(proxy));
        sync_completion_signal(&proto_completion_);
        return ZX_OK;
    }
    }

    fbl::AutoLock lock(&proto_completion_mutex_);
    sync_completion_signal(&proto_completion_);
    return ZX_OK;
}

zx_status_t PlatformBus::PBusDeviceAdd(const pbus_dev_t* pdev) {
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
    auto status = PlatformDevice::Create(pdev, parent_dev, this, &dev);
    if (status != ZX_OK) {
        return status;
    }

    status = dev->Start();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();
    return ZX_OK;
}

zx_status_t PlatformBus::PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* pdev) {
    if (!pdev->name) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::unique_ptr<platform_bus::ProtocolDevice> dev;
    auto status = ProtocolDevice::Create(pdev, zxdev(), this, &dev);
    if (status != ZX_OK) {
        return status;
    }

    // Protocol devices run in our devhost.
    status = dev->Start();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = dev.release();

    // Wait for protocol implementation driver to register its protocol.
    ddk::AnyProtocol dummy_proto;

    proto_completion_mutex_.Acquire();
    while (DdkGetProtocol(proto_id, &dummy_proto) == ZX_ERR_NOT_SUPPORTED) {
        sync_completion_reset(&proto_completion_);
        proto_completion_mutex_.Release();
        zx_status_t status = sync_completion_wait(&proto_completion_, ZX_SEC(10));
        if (status != ZX_OK) {
            zxlogf(ERROR, "%s failed for protocol %08x\n", __FUNCTION__, proto_id);
            return status;
        }
        proto_completion_mutex_.Acquire();
    }
    proto_completion_mutex_.Release();
    return ZX_OK;
}

const char* PlatformBus::PBusGetBoardName() {
    return board_info_.board_name;
}

zx_status_t PlatformBus::PBusSetBoardInfo(const pbus_board_info_t* info) {
    board_info_.board_revision = info->board_revision;
    return ZX_OK;
}

zx_status_t PlatformBus::GetBoardInfo(pdev_board_info_t* out_info) {
    memcpy(out_info, &board_info_, sizeof(board_info_));
    return ZX_OK;
}

zx_status_t PlatformBus::DdkGetProtocol(uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_PBUS: {
        auto proto = static_cast<pbus_protocol_t*>(out);
        proto->ctx = this;
        proto->ops = &ops_;
        return ZX_OK;
    }
    case ZX_PROTOCOL_GPIO_IMPL:
        if (gpio_) {
            gpio_->GetProto(static_cast<gpio_impl_protocol_t*>(out));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_I2C_IMPL:
        if (i2c_) {
            i2c_->GetProto(static_cast<i2c_impl_protocol_t*>(out));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_CLK:
        if (clk_) {
            clk_->GetProto(static_cast<clk_protocol_t*>(out));
            return ZX_OK;
        }
        break;
    case ZX_PROTOCOL_IOMMU:
        if (iommu_) {
            iommu_->GetProto(static_cast<iommu_protocol_t*>(out));
        } else {
            // return default implementation
            auto proto = static_cast<iommu_protocol_t*>(out);
            proto->ctx = this;
            proto->ops = &iommu_protocol_ops_;
            return ZX_OK;
        }
        break;
    default: {
        fbl::AutoLock lock(&proto_proxys_mutex_);
        auto proto_proxy = proto_proxys_.find(proto_id);
        if (!proto_proxy.IsValid()) {
            return ZX_ERR_NOT_SUPPORTED;
        }
        proto_proxy->GetProtocol(out);
        return ZX_OK;
    }
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
        size_t itemlen = ZBI_ALIGN(
            static_cast<uint32_t>(sizeof(zbi_header_t)) + header.length);
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
    uint8_t interrupt_controller_type = fuchsia_sysinfo_InterruptControllerType_UNKNOWN;
    zx_off_t metadata_offset = 0;
    len = zbi_length;
    off = sizeof(header);

    // find platform ID record and copy metadata records
    while (len > sizeof(header)) {
        auto status = zbi.read(&header, off, sizeof(header));
        if (status < 0) {
            break;
        }
        const size_t itemlen = ZBI_ALIGN(
            static_cast<uint32_t>(sizeof(zbi_header_t)) + header.length);
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

            // Publish board name to sysinfo driver
            status = device_publish_metadata(parent(), "/dev/misc/sysinfo",
                                             DEVICE_METADATA_BOARD_NAME, platform_id.board_name,
                                             sizeof(platform_id.board_name));
            if (status != ZX_OK) {
                zxlogf(ERROR, "device_publish_metadata(board_name) failed: %d\n", status);
                return status;
            }
        } else if (header.type == ZBI_TYPE_KERNEL_DRIVER) {
            if (header.extra == KDRV_ARM_GIC_V2) {
                interrupt_controller_type = fuchsia_sysinfo_InterruptControllerType_GIC_V2;
            } else if (header.extra == KDRV_ARM_GIC_V3) {
                interrupt_controller_type = fuchsia_sysinfo_InterruptControllerType_GIC_V3;
            }
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

    // Publish interrupt controller type to sysinfo driver
    status = device_publish_metadata(parent(), "/dev/misc/sysinfo",
                                     DEVICE_METADATA_INTERRUPT_CONTROLLER_TYPE,
                                     &interrupt_controller_type, sizeof(interrupt_controller_type));
    if (status != ZX_OK) {
        zxlogf(ERROR, "device_publish_metadata(interrupt_controller_type) failed: %d\n", status);
        return status;
    }

    if (!got_platform_id) {
        zxlogf(ERROR, "platform_bus: ZBI_TYPE_PLATFORM_ID not found\n");
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

zx_status_t PlatformBus::GetZbiMetadata(uint32_t type, uint32_t extra, const void** out_metadata,
                                        uint32_t* out_size) {
    const uint8_t* metadata = metadata_.get();
    const size_t metadata_size = metadata_.size();
    size_t offset = 0;

    while (offset + sizeof(zbi_header_t) < metadata_size) {
        const auto header = reinterpret_cast<const zbi_header_t*>(metadata);
        const size_t length = ZBI_ALIGN(
            static_cast<uint32_t>(sizeof(zbi_header_t)) + header->length);
        if (offset + length > metadata_size) {
            break;
        }

        if (header->type == type && header->extra == extra) {
            *out_metadata = header + 1;
            *out_size = static_cast<uint32_t>(length - sizeof(zbi_header_t));
            return ZX_OK;
        }
        metadata += length;
        offset += length;
    }
    zxlogf(ERROR, "%s metadata not found for type %08x, extra %u\n", __FUNCTION__, type, extra);
    return ZX_ERR_NOT_FOUND;
}

zx_status_t PlatformBus::I2cInit(const i2c_impl_protocol_t* i2c) {
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

zx_status_t PlatformBus::I2cTransact(uint32_t txid, rpc_i2c_req_t* req,
                                     const pbus_i2c_channel_t* channel,
                                     zx_handle_t channel_handle) {
    if (channel->bus_id >= i2c_buses_.size()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    auto i2c_bus = i2c_buses_[channel->bus_id].get();
    return i2c_bus->Transact(txid, req, channel->address, channel_handle);
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
    status = zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                             iommu_handle_.reset_and_get_address());
    if (status != ZX_OK) {
        return status;
    }

    // Then we attach the platform-bus device below it.
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, board_info_.vid},
        {BIND_PLATFORM_DEV_PID, 0, board_info_.pid},
    };

    return DdkAdd("platform", 0, props, fbl::count_of(props));
}

} // namespace platform_bus

zx_status_t platform_bus_create(void* ctx, zx_device_t* parent, const char* name,
                                const char* args, zx_handle_t zbi_vmo_handle) {
    zx::vmo zbi(zbi_vmo_handle);
    return platform_bus::PlatformBus::Create(parent, name, fbl::move(zbi));
}
