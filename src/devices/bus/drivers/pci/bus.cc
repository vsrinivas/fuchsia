// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bus.h"

#include <list>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/vector.h>

#include "bridge.h"
#include "common.h"
#include "config.h"
#include "device.h"

namespace pci {

// Creates the PCI bus driver instance and attempts initialization.
zx_status_t Bus::Create(zx_device_t* parent) {
  zx_status_t status;
  pciroot_protocol_t pciroot;
  if ((status = device_get_protocol(parent, ZX_PROTOCOL_PCIROOT, &pciroot)) != ZX_OK) {
    pci_errorf("failed to obtain pciroot protocol: %d!\n", status);
    return status;
  }

  Bus* bus = new Bus(parent, &pciroot);
  if (!bus) {
    pci_errorf("failed to allocate bus object.\n");
    return ZX_ERR_NO_MEMORY;
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
  if ((status = bus->DdkAdd(name)) != ZX_OK) {
    pci_errorf("failed to add bus driver: %d\n", status);
    return status;
  }

  if ((status = bus->Initialize()) != ZX_OK) {
    pci_errorf("failed to initialize bus driver: %d!\n", status);
    bus->DdkAsyncRemove();
    return status;
  }

  return ZX_OK;
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
  root_ = std::unique_ptr<PciRoot>(new PciRoot(info_.start_bus_num, pciroot_));

  // Begin our bus scan starting at our root
  ScanDownstream();
  root_->ConfigureDownstreamDevices();
  pci_infof("AllDevicesList:\n");
  {
    fbl::AutoLock devices_lock(&devices_lock_);
    for (auto& dev : devices_) {
      dev.Dump();
    }
  }

  pci_infof("%s init done.\n", info_.name);
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

  status =
      ddk::MmioBuffer::Create(0, size, zx::vmo(info_.ecam_vmo), ZX_CACHE_POLICY_UNCACHED, &ecam_);
  if (status != ZX_OK) {
    pci_errorf("couldn't map ecam vmo: %d!\n", status);
    return status;
  }

  pci_infof("ecam for segment %u mapped at %p (size: %#zx)\n", info_.segment_group, ecam_->get(),
            ecam_->get_size());
  return ZX_OK;
}

zx_status_t Bus::MakeConfig(pci_bdf_t bdf, std::unique_ptr<Config>* out_config) {
  zx_status_t status;
  if (ecam_) {
    status = MmioConfig::Create(bdf, &(*ecam_), info_.start_bus_num, info_.end_bus_num, out_config);
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
  std::list<BusScanEntry> scan_list;
  // First scan the bus id associated with our root.
  BusScanEntry entry = {{static_cast<uint8_t>(root_->managed_bus_id()), 0, 0}, root_.get()};
  entry.upstream = root_.get();
  scan_list.push_back(entry);

  // Process any bridges found under the root, any bridges under those bridges, etc...
  // It's important that we scan in the order we discover bridges (DFS) because
  // when we implement bus id assignment it will affect the overall numbering
  // scheme of the bus topology.
  while (!scan_list.empty()) {
    auto entry = scan_list.back();
    pci_tracef("scanning from %02x:%02x.%01x upstream: %s\n", entry.bdf.bus_id, entry.bdf.device_id,
               entry.bdf.function_id,
               (entry.upstream->type() == UpstreamNode::Type::ROOT)
                   ? "root"
                   : static_cast<Bridge*>(entry.upstream)->config()->addr());
    // Remove this entry, otherwise we'll pop the wrong child off if the scan
    // adds any new bridges / resume points.
    scan_list.pop_back();
    ScanBus(entry, &scan_list);
  }

  return ZX_OK;
}

void Bus::ScanBus(BusScanEntry entry, std::list<BusScanEntry>* scan_list) {
  uint32_t bus_id = entry.bdf.bus_id;  // 32bit so bus_id won't overflow 8bit in the loop
  uint8_t _dev_id = entry.bdf.device_id;
  uint8_t _func_id = entry.bdf.function_id;
  UpstreamNode* upstream = entry.upstream;
  for (uint8_t dev_id = _dev_id; dev_id < PCI_MAX_DEVICES_PER_BUS; dev_id++) {
    for (uint8_t func_id = _func_id; func_id < PCI_MAX_FUNCTIONS_PER_DEVICE; func_id++) {
      std::unique_ptr<Config> config;
      pci_bdf_t bdf = {static_cast<uint8_t>(bus_id), dev_id, func_id};
      zx_status_t status = MakeConfig(bdf, &config);
      if (status != ZX_OK) {
        continue;
      }

      // Check that the device is valid by verifying the vendor and device ids.
      if (config->Read(Config::kVendorId) == PCI_INVALID_VENDOR_ID) {
        continue;
      }

      bool is_bridge =
          ((config->Read(Config::kHeaderType) & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE);
      pci_tracef("\tfound %s at %02x:%02x.%1x\n", (is_bridge) ? "bridge" : "device", bus_id, dev_id,
                 func_id);

      // If we found a bridge, add it to our bridge list and initialize /
      // enumerate it after we finish scanning this bus
      if (is_bridge) {
        fbl::RefPtr<Bridge> bridge;
        uint8_t mbus_id = config->Read(Config::kSecondaryBusId);
        status = Bridge::Create(zxdev(), std::move(config), upstream, this, mbus_id, &bridge);
        if (status != ZX_OK) {
          continue;
        }

        // Create scan entries for the next device we would have looked
        // at in the current level of the tree, as well as the new
        // bridge. Since we always work our way from the top of the scan
        // stack we effectively scan the bus in a DFS manner. |func_id|
        // is always incremented by one to ensure we don't scan this
        // same bdf again. If the incremented value is invalid then the
        // device_id loop will iterate and we'll be in a good state
        // again.
        BusScanEntry resume_entry{};
        resume_entry.bdf.bus_id = static_cast<uint8_t>(bus_id);
        resume_entry.bdf.device_id = dev_id;
        resume_entry.bdf.function_id = static_cast<uint8_t>(func_id + 1);
        resume_entry.upstream = upstream;  // Same upstream as this scan
        scan_list->push_back(resume_entry);

        BusScanEntry bridge_entry{};
        bridge_entry.bdf.bus_id = static_cast<uint8_t>(bridge->managed_bus_id());
        bridge_entry.upstream = bridge.get();  // the new bridge will be this scan's upstream
        scan_list->push_back(bridge_entry);
        // Quit this scan and pick up again based on the scan entries found.
        return;
      } else {
        // Create a device
        pci::Device::Create(zxdev(), std::move(config), upstream, this);
      }
    }

    // Reset _func_id to zero here so that after we resume a single function
    // scan we'll be able to scan the full 8 functions of a given device.
    _func_id = 0;
  }
}

void Bus::DdkRelease() { delete this; }

}  // namespace pci
