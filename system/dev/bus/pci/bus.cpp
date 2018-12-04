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
    status = bus->pciroot().GetPciPlatformInfo(&info);
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

zx_status_t Bus::Initialize() {
    zx_status_t status = pciroot_.GetPciPlatformInfo(&info_);
    if (status != ZX_OK) {
        pci_errorf("failed to obtain platform information: %d!\n", status);
        return status;
    }

    if (info_.ecam_vmo != ZX_HANDLE_INVALID) {
        if ((status = MapEcam()) != ZX_OK) {
            pci_errorf("failed to map ecam: %d!\n", status);
            return status;
        }
    }

    ScanDownstream();
    return ZX_OK;
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
    has_ecam_ = true;
    return ZX_OK;
}

zx_status_t Bus::MakeConfig(pci_bdf_t bdf, fbl::RefPtr<Config>* config) {
    if (has_ecam_) {
        return MmioConfig::Create(bdf, &ecam_, info_.start_bus_num, info_.end_bus_num, config);
    } else {
        return ProxyConfig::Create(bdf, &pciroot_, config);
    }
}

// Scan downstream starting at the start bus number provided to use by the platform.
// In the process of scanning, take note of bridges found and configure any that are
// unconfigured. In the end the Bus should have a list of all devides, and all bridges
// should have a list of references to their own downstream devices.
zx_status_t Bus::ScanDownstream(void) {
    pci_infof("ScanDownstream %u:%u\n", info_.start_bus_num, info_.end_bus_num);
    for (uint16_t bus_id = info_.start_bus_num; bus_id <= info_.end_bus_num; bus_id++) {
        for (uint8_t dev_id = 0; dev_id < PCI_MAX_DEVICES_PER_BUS; dev_id++) {
            for (uint8_t func_id = 0; func_id < PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
                fbl::RefPtr<Config> config;
                pci_bdf_t bdf = { static_cast<uint8_t>(bus_id), dev_id, func_id };
                zx_status_t status = MakeConfig(bdf, &config);
                if (status == ZX_OK) {
                    if (config->vendor_id() != 0xFFFF) {
                        pci_infof("found device at %02x:%02x.%1x\n", bus_id, dev_id, func_id);
                    }
                }
            }
        }
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
