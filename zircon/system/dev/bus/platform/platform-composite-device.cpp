// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-composite-device.h"

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

zx_status_t CompositeDevice::Create(const pbus_dev_t* pdev, zx_device_t* parent, PlatformBus* bus,
                                    fbl::unique_ptr<platform_bus::CompositeDevice>* out) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<platform_bus::CompositeDevice> dev(new (&ac)
                                  platform_bus::CompositeDevice(parent, bus, pdev));
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

CompositeDevice::CompositeDevice(zx_device_t* parent, PlatformBus* bus, const pbus_dev_t* pdev)
    : CompositeDeviceType(parent), bus_(bus), vid_(pdev->vid), pid_(pdev->pid),
      did_(pdev->did), resources_() {
    strlcpy(name_, pdev->name, sizeof(name_));
}

zx_status_t CompositeDevice::Init(const pbus_dev_t* pdev) {
    return resources_.Init(pdev);
}

zx_status_t CompositeDevice::PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    if (index >= resources_.mmio_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    const pbus_mmio_t& mmio = resources_.mmio(index);
    const zx_paddr_t vmo_base = ROUNDDOWN(mmio.base, ZX_PAGE_SIZE);
    const size_t vmo_size = ROUNDUP(mmio.base + mmio.length - vmo_base, ZX_PAGE_SIZE);
    zx::vmo vmo;

    zx_status_t status = zx::vmo::create_physical(*bus_->GetResource(), vmo_base, vmo_size, &vmo);
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

zx_status_t CompositeDevice::PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    if (index >= resources_.irq_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_irq == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_irq_t& irq = resources_.irq(index);
    if (flags == 0) {
        flags = irq.mode;
    }
    zx_status_t status = zx::interrupt::create(*bus_->GetResource(), irq.irq, flags, out_irq);
    if (status != ZX_OK) {
        zxlogf(ERROR, "platform_dev_map_interrupt: zx_interrupt_create failed %d\n", status);
        return status;
    }
    return status;
}

zx_status_t CompositeDevice::PDevGetBti(uint32_t index, zx::bti* out_bti) {
    if (index >= resources_.bti_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_bti == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_bti_t& bti = resources_.bti(index);

    return bus_->IommuGetBti(bti.iommu_index, bti.bti_id, out_bti);
}

zx_status_t CompositeDevice::PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    if (index >= resources_.smc_count()) {
        return ZX_ERR_OUT_OF_RANGE;
    }
    if (out_resource == nullptr) {
        return ZX_ERR_INVALID_ARGS;
    }

    const pbus_smc_t& smc = resources_.smc(index);

    uint32_t options = ZX_RSRC_KIND_SMC;
    if (smc.exclusive)
        options |= ZX_RSRC_FLAG_EXCLUSIVE;
    char rsrc_name[ZX_MAX_NAME_LEN];
    snprintf(rsrc_name, ZX_MAX_NAME_LEN - 1, "%s.pbus[%u]", name_, index);
    return zx::resource::create(*bus_->GetResource(), options, smc.service_call_num_base, smc.count,
                                rsrc_name, sizeof(rsrc_name), out_resource);
}

zx_status_t CompositeDevice::PDevGetDeviceInfo(pdev_device_info_t* out_info) {
    pdev_device_info_t info = {
        .vid = vid_,
        .pid = pid_,
        .did = did_,
        .mmio_count = static_cast<uint32_t>(resources_.mmio_count()),
        .irq_count = static_cast<uint32_t>(resources_.irq_count()),
        .gpio_count = static_cast<uint32_t>(resources_.gpio_count()),
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

zx_status_t CompositeDevice::PDevGetBoardInfo(pdev_board_info_t* out_info) {
    return bus_->PBusGetBoardInfo(out_info);
}

zx_status_t CompositeDevice::PDevDeviceAdd(uint32_t index, const device_add_args_t* args,
                                           zx_device_t** out_device) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t CompositeDevice::PDevGetProtocol(uint32_t proto_id, uint32_t index,
                                             void* out_out_protocol_buffer,
                                             size_t out_protocol_size,
                                             size_t* out_out_protocol_actual) {
    return ZX_ERR_NOT_SUPPORTED;
}

void CompositeDevice::DdkRelease() {
    delete this;
}

zx_status_t CompositeDevice::Start() {
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
            fbl::Array<uint8_t> data;
            status = bus_->GetBootItem(metadata.zbi_type, metadata.zbi_extra, &data);
            if (status == ZX_OK) {
                status = DdkAddMetadata(metadata.zbi_type, data.get(), data.size());
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
