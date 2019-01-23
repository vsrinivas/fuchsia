// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include "bus.h"
#include "bridge.h"
#include "common.h"
#include "config.h"
#include "device.h"

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/vector.h>

namespace pci {
Bus::AllDevicesList Bus::device_list_;

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

    // Stash the ops/ctx pointers for the pciroot protocol so we can pass
    // them to the allocators provided by Pci(e)Root. The initial root is
    // created to manage the start of the bus id range given to use by the
    // pciroot protocol.
    fbl::AllocChecker ac;
    root_ = fbl::unique_ptr<PciRoot>(new (&ac) PciRoot(info_.start_bus_num, &pciroot_));
    if (!ac.check()) {
        pci_errorf("failed to allocate root bookkeeping!\n");
        return ZX_ERR_NO_MEMORY;
    }

    // Begin our bus scan starting at our root
    ScanDownstream();
    pci_infof("AllDevicesList:\n");
    for (auto& dev : device_list_) {
        pci_infof("\t%s %s\n", dev.config()->addr(), dev.is_bridge() ? "(b)" : "");
    }

    pci_infof("cleaning up devices\n");
    root_->DisableDownstream();
    root_->UnplugDownstream();
    pci_infof("done.\n");

    // Ensure the topology was cleaned up properly.
    ZX_DEBUG_ASSERT(device_list_.size() == 0);
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

zx_status_t Bus::MakeConfig(pci_bdf_t bdf, fbl::RefPtr<Config>* out_config) {
    zx_status_t status;
    if (has_ecam_) {
        status = MmioConfig::Create(bdf, &ecam_, info_.start_bus_num, info_.end_bus_num, out_config);
    } else {
        status = ProxyConfig::Create(bdf, &pciroot_, out_config);
    }

    if (status != ZX_OK) {
        pci_errorf("failed to create config for %02x:%02x:%1x: %d!\n", bdf.bus_id, bdf.device_id,
                   bdf.function_id, status);
    }

    return status;
}

// Scan downstream starting at the bus id managed by the Bus's Root.
// In the process of scanning, take note of bridges found and configure any that are
// unconfigured. In the end the Bus should have a list of all devides, and all bridges
// should have a list of pointers to their own downstream devices.
zx_status_t Bus::ScanDownstream(void) {
    BridgeList bridge_list;
    pci_tracef("ScanDownstream %u:%u\n", info_.start_bus_num, info_.end_bus_num);
    // First scan the root.
    ScanBus(root_.get(), &bridge_list);
    // Process any bridges found underthe root, any bridges under those bridges, etc...
    // It's important that we scan in the order we discover bridges (DFS) because
    // when we implement bus id assignment it will affect the overall numbering
    // scheme of the bus topology.
    while (bridge_list.size() > 0) {
        auto bridge = bridge_list.erase(0);
        // 1. Scan the bus below to add devices to our downstream
        ScanBus(bridge.get(), &bridge_list);
        // 2. Confirm devices are in our downstream list
        // 3. Allocate bars for the downstream list
        // 4. Enable the downstream devices
    }
    return ZX_OK;
}

void Bus::ScanBus(UpstreamNode* upstream, BridgeList* bridge_list) {
    uint32_t bus_id = upstream->managed_bus_id();
    pci_tracef("scanning bus %d\n", bus_id);
    for (uint8_t dev_id = 0; dev_id < PCI_MAX_DEVICES_PER_BUS; dev_id++) {
        for (uint8_t func_id = 0; func_id < PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
            fbl::RefPtr<Config> config;
            pci_bdf_t bdf = {static_cast<uint8_t>(bus_id), dev_id, func_id};
            zx_status_t status = MakeConfig(bdf, &config);
            if (status != ZX_OK) {
                continue;
            }

            // Check that the device is valid by verifying the vendor and device ids.
            if (config->Read(Config::kVendorId) == PCI_INVALID_VENDOR_ID) {
                continue;
            }

            bool is_bridge = ((config->Read(Config::kHeaderType) & PCI_HEADER_TYPE_MASK)
                                == PCI_HEADER_TYPE_BRIDGE);
            printf("\tfound %s at %02x:%02x.%1x\n", (is_bridge) ? "bridge" : "device",
                   bus_id, dev_id, func_id);

            // If we found a bridge, add it to our bridge list and initialize / enumerate it after
            // we finish scanning this bus
            if (is_bridge) {
                fbl::RefPtr<Bridge> bridge;
                uint8_t mbus_id = config->Read(Config::kSecondaryBusId);
                status = Bridge::Create(std::move(config), upstream, mbus_id, &bridge);
                if (status != ZX_OK) {
                    continue;
                }

                bridge_list->push_back(bridge);
            } else {
                // Create a device
                pci::Device::Create(std::move(config), upstream);
            }
        }
    }
}

void Bus::DdkRelease() {
    if (ecam_.vaddr) {
        mmio_buffer_release(&ecam_);
    }
    delete this;
}

} // namespace pci

static zx_status_t pci_bus_bind(void* ctx, zx_device_t* parent) {
    return pci::Bus::Create(parent);
}

static zx_driver_ops_t pci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = pci_bus_bind,
    .create = nullptr,
    .release = nullptr,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(pci, pci_driver_ops, "zircon", "0.1", 5)
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_PCIROOT),
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_KPCI),
ZIRCON_DRIVER_END(pci)
