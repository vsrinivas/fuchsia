// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include "bus.h"
#include "common.h"
#include "config.h"

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <fbl/alloc_checker.h>

namespace pci {

// Creates the PCI bus driver instance and attempts initialization.
zx_status_t Bus::Create(zx_device_t* parent) {
    zx_status_t status;
    pciroot_protocol_t pciroot;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &pciroot)) != ZX_OK) {
        pci_errorf("failed to obtain pciroot protocol: %d!\n", status);
        return status;
    }

    fbl::AllocChecker ac;
    Bus* bus = new (&ac) Bus(parent, &pciroot);
    if (!ac.check()) {
        pci_errorf("failed to allocate bus object.\n");
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = bus->Initialize()) != ZX_OK) {
        pci_errorf("failed to initialize bus driver: %d!\n", status);
        return status;
    }

    // Grab the info beforehand so we can get segment/bus information from it
    // and appropriately name our DDK device.
    pci_platform_info_t info;
    status = bus->GetPciPlatformInfo(&info);
    if (status != ZX_OK) {
        pci_errorf("failed to obtain platform information: %d!\n", status);
        return status;
    }

    // Name the bus instance with segment group and bus range, for example:
    // pci[0][0:255] for a legacy pci bus in segment group 0.
    char name[32];
    snprintf(name, sizeof(name), "pci[%u][%u:%u]", info.segment_group, info.start_bus_num,
             info.end_bus_num);

    return bus->DdkAdd(name);
}

// Maps a vmo as an mmio_buffer to be used as this Bus driver's ECAM region
// for config space access.
zx_status_t Bus::MapEcam(void) {
    ZX_DEBUG_ASSERT(info_.ecam_vmo != ZX_HANDLE_INVALID);

    size_t size;
    zx_status_t status = zx_vmo_get_size(info_.ecam_vmo, &size);
    if (status != ZX_OK) {
        pci_errorf("couldn't get ecam vmo size: %d!\n", status);
        return status;
    }

    status = mmio_buffer_init(&ecam_, 0, size, info_.ecam_vmo, ZX_CACHE_POLICY_UNCACHED);
    if (status != ZX_OK) {
        pci_errorf("couldn't map ecam vmo: %d!\n", status);
        return status;
    }

    pci_infof("ecam for segment %u mapped at %p (size: %#zx)\n", info_.segment_group,
              ecam_.vaddr, ecam_.size);
    return ZX_OK;
}

zx_status_t Bus::Initialize() {
    // Temporarily dump the config of bdf 00:00.0 to show proxy config
    // is working properly.
    pci_bdf_t bdf = {0, 0, 0};

    zx_status_t status = GetPciPlatformInfo(&info_);
    if (status != ZX_OK) {
        pci_errorf("failed to obtain platform information: %d!\n", status);
        return status;
    }

    if (info_.ecam_vmo != ZX_HANDLE_INVALID) {
        if ((status = MapEcam()) != ZX_OK) {
            return status;
        }

        // Temporary code to demonstrate MMIO works
        auto cfg = MmioConfig::Create(bdf, ecam_.vaddr, info_.start_bus_num);
        if (cfg) {
            cfg->DumpConfig(PCI_BASE_CONFIG_SIZE);
        }
    } else {
        pci_errorf("couldn't find vmo for ecam!\n");
    }

    return ZX_OK;
}

void Bus::DdkRelease() {
    if (ecam_.vaddr) {
        mmio_buffer_release(&ecam_);
    }
    delete this;
}

} // namespace pci

extern "C" zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent) {
    return pci::Bus::Create(parent);
}
