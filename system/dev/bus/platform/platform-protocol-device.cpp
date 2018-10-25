// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-protocol-device.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <fbl/function.h>
#include <zircon/syscalls/resource.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include "platform-bus.h"

namespace platform_bus {

zx_status_t ProtocolDevice::Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                                   fbl::unique_ptr<platform_bus::ProtocolDevice>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::ProtocolDevice> dev(new (&ac)
                                  platform_bus::ProtocolDevice(parent, bus, pdev));
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

ProtocolDevice::ProtocolDevice(zx_device_t* parent, PlatformBus* bus, const pbus_dev_t* pdev)
    : ProtocolDeviceType(parent), bus_(bus), vid_(pdev->vid), pid_(pdev->pid),
      did_(pdev->did), resources_(ROOT_DEVICE_ID) {
    strlcpy(name_, pdev->name, sizeof(name_));
}

zx_status_t ProtocolDevice::Init(const pbus_dev_t* pdev) {
    auto status = resources_.Init(pdev);
    if (status != ZX_OK) {
        return status;
    }

    pbus_protocol_t pbus;
    status = device_get_protocol(parent(), ZX_PROTOCOL_PBUS, &pbus);
    if (status != ZX_OK) {
        return status;
    }

    pbus_ctx_ = pbus.ctx;
    // Make a copy of the platform bus protocol so we can replace some methods.
    pbus_ops_ = *pbus.ops;

    // Do not allow calling device_add and protocol_device_add.
    // Only the board driver should be calling those.
    pbus_ops_.device_add = [](void* ctx, const pbus_dev_t* dev) { return ZX_ERR_NOT_SUPPORTED; };
    pbus_ops_.protocol_device_add = [](void* ctx, uint32_t proto_id, const pbus_dev_t* dev)
                                    { return ZX_ERR_NOT_SUPPORTED; };
    return ZX_OK;
}

zx_status_t ProtocolDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    if (index >= resources_.mmio_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const pbus_mmio_t& mmio = resources_.mmio(index);
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;

    zx_status_t status = zx_vmo_create_physical(bus_->GetResource(), vmo_base, vmo_size,
                                                vmo.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: creating vmo failed %d\n", __FUNCTION__, status);
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "mmio %u", index);
    status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: setting vmo name failed %d\n", __FUNCTION__, status);
        return status;
    }

    out_mmio->offset = mmio.base - vmo_base;
    out_mmio->vmo = vmo.release();
    out_mmio->size = mmio.length;
    return ZX_OK;
}

// TODO(surajmalhotra): Remove after migrating all clients off.
zx_status_t ProtocolDevice::PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr,
                                        size_t* out_size, zx_paddr_t* out_paddr,
                                        zx_handle_t* out_handle) {
    if (index >= resources_.mmio_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const pbus_mmio_t& mmio = resources_.mmio(index);
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;
    zx_status_t status = zx_vmo_create_physical(bus_->GetResource(), vmo_base, vmo_size,
                                                vmo.reset_and_get_address());
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_create_physical failed %d\n", status);
        return status;
    }

    char name[32];
    snprintf(name, sizeof(name), "mmio %u", index);
    status = vmo.set_property(ZX_PROP_NAME, name, sizeof(name));
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: setting vmo name failed %d\n", __FUNCTION__, status);
        return status;
    }

    status = vmo.set_cache_policy(cache_policy);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmo_set_cache_policy failed %d\n", status);
        return status;
    }

    uintptr_t virt;
    status = zx::vmar::root_self()->map(0, vmo, 0, vmo_size, ZX_VM_PERM_READ |
                                        ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, &virt);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_mmio: zx_vmar_map failed %d\n", status);
        return status;
    }

    *out_size = mmio.length;
    *out_handle = vmo.release();
    if (out_paddr) {
        *out_paddr = vmo_base;
    }
    *out_vaddr = reinterpret_cast<void*>(virt + (mmio.base - vmo_base));
    return ZX_OK;
}

zx_status_t ProtocolDevice::PDevGetInterrupt(uint32_t index, uint32_t flags,
                                             zx_handle_t* out_handle) {
    if (index >= resources_.irq_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_handle == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_irq_t& irq = resources_.irq(index);
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

zx_status_t ProtocolDevice::PDevGetBti(uint32_t index, zx_handle_t* out_handle) {
    if (index >= resources_.bti_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_handle == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_bti_t& bti = resources_.bti(index);

    return bus_->IommuGetBti(bti.iommu_index, bti.bti_id, out_handle);
}

zx_status_t ProtocolDevice::PDevGetSmc(uint32_t index, zx_handle_t* out_handle) {
    if (index >= resources_.smc_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_handle == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_smc_t& smc = resources_.smc(index);

    zx_handle_t handle;
    uint32_t options = ZX_RSRC_KIND_SMC | ZX_RSRC_FLAG_EXCLUSIVE;
    char rsrc_name[ZX_MAX_NAME_LEN];
    snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
    zx_status_t status = zx_resource_create(bus_->GetResource(), options, smc.service_call_num_base,
                                            smc.count, rsrc_name, sizeof(rsrc_name), &handle);
    if (status != ZX_OK) {
        return status;
    }

    *out_handle = handle;
    return status;
}

zx_status_t ProtocolDevice::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    pdev_device_info_t info = {
        .vid = vid_,
        .pid = pid_,
        .did = did_,
        .mmio_count = static_cast<uint32_t>(resources_.mmio_count()),
        .irq_count = static_cast<uint32_t>(resources_.irq_count()),
        .gpio_count = static_cast<uint32_t>(resources_.gpio_count()),
        .i2c_channel_count = static_cast<uint32_t>(resources_.i2c_channel_count()),
        .clk_count = static_cast<uint32_t>(resources_.clk_count()),
        .bti_count = static_cast<uint32_t>(resources_.bti_count()),
        .smc_count = static_cast<uint32_t>(resources_.smc_count()),
        .metadata_count = static_cast<uint32_t>(resources_.metadata_count()),
        .reserved = {},
        .name = {},
    };
    static_assert(sizeof(info.name) == sizeof(name_), "");
    memcpy(info.name, name_, sizeof(out_info->name));
    memcpy(out_info, &info, sizeof(info));

    return ZX_OK;
}

zx_status_t ProtocolDevice::PDevGetBoardInfo(pdev_board_info_t* out_info) {
    return bus_->GetBoardInfo(out_info);
}

zx_status_t ProtocolDevice::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                          zx_device_t** device) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t ProtocolDevice::PDevGetProtocol(uint32_t proto_id, uint32_t index, void* out_protocol,
                                            size_t protocol_size, size_t* protocol_actual) {
    // Pass through to DdkGetProtocol if index is zero
    if (index != 0) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (protocol_size < sizeof(ddk::AnyProtocol)) {
        return ZX_ERR_INVALID_ARGS;
    }
    *protocol_actual = sizeof(ddk::AnyProtocol);
    return DdkGetProtocol(proto_id, out_protocol);
}

zx_status_t ProtocolDevice::DdkGetProtocol(uint32_t proto_id, void* out) {
    if (proto_id == ZX_PROTOCOL_PDEV) {
        auto proto = static_cast<pdev_protocol_t*>(out);
        proto->ops = &ops_;
        proto->ctx = this;
        return ZX_OK;
    } else if (proto_id == ZX_PROTOCOL_PBUS) {
        // Protocol implementation drivers get a restricted subset of the platform bus protocol
        auto proto = static_cast<pbus_protocol_t*>(out);
        proto->ops = &pbus_ops_;
        proto->ctx = pbus_ctx_;
        return ZX_OK;
    } else {
        return bus_->DdkGetProtocol(proto_id, out);
    }
}

void ProtocolDevice::DdkRelease() {
    delete this;
}

zx_status_t ProtocolDevice::Start() {
    zx_device_prop_t props[] = {
        {BIND_PLATFORM_DEV_VID, 0, vid_},
        {BIND_PLATFORM_DEV_PID, 0, pid_},
        {BIND_PLATFORM_DEV_DID, 0, did_},
    };

    char name[ZX_DEVICE_NAME_MAX];
    if (vid_ == PDEV_VID_GENERIC && pid_ == PDEV_PID_GENERIC && did_ == PDEV_DID_KPCI) {
        strlcpy(name, "pci", sizeof(name));
    } else {
        snprintf(name, sizeof(name), "%02x:%02x:%01x", vid_, pid_, did_);
    }

    // Protocol devices run in our devhost.
    uint32_t device_add_flags = 0;

    const size_t metadata_count = resources_.metadata_count();
    const size_t boot_metadata_count = resources_.boot_metadata_count();
    if (metadata_count > 0 || boot_metadata_count > 0) {
        // Keep device invisible until after we add its metadata.
        device_add_flags |= DEVICE_ADD_INVISIBLE;
    }

    auto status = DdkAdd(name, device_add_flags, props, fbl::count_of(props));
    if (status != ZX_OK) {
        return status;
    }

    if (metadata_count > 0 || boot_metadata_count > 0) {
        for (size_t i = 0; i < metadata_count; i++) {
            const auto& metadata = resources_.metadata(i);
            status = DdkAddMetadata(metadata.type, metadata.data_buffer, metadata.data_size);
            if (status != ZX_OK) {
                DdkRemove();
                return status;
            }
        }

        for (size_t i = 0; i < boot_metadata_count; i++) {
            const auto& metadata = resources_.boot_metadata(i);
            const void* data;
            uint32_t length;
            status = bus_->GetZbiMetadata(metadata.zbi_type, metadata.zbi_extra, &data, &length);
            if (status == ZX_OK) {
                status = DdkAddMetadata(metadata.zbi_type, data, length);
            }
            if (status != ZX_OK) {
                DdkRemove();
                return status;
            }
        }

        DdkMakeVisible();
    }

    return ZX_OK;
}

} // namespace platform_bus
