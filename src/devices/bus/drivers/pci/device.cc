// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <array>

#include <ddk/binding.h>
#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>
#include <pretty/sizes.h>

#include "bus_device_interface.h"
#include "common.h"
#include "ref_counted.h"
#include "upstream_node.h"

namespace pci {

namespace {  // anon namespace.  Externals do not need to know about DeviceImpl

class DeviceImpl : public Device {
 public:
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Config>&& cfg,
                            UpstreamNode* upstream, BusDeviceInterface* bdi);

  // Implement ref counting, do not let derived classes override.
  PCI_IMPLEMENT_REFCOUNTED;

  // Disallow copying, assigning and moving.
  DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceImpl);

 protected:
  DeviceImpl(zx_device_t* parent, std::unique_ptr<Config>&& cfg, UpstreamNode* upstream,
             BusDeviceInterface* bdi)
      : Device(parent, std::move(cfg), upstream, bdi, false) {}
};

zx_status_t DeviceImpl::Create(zx_device_t* parent, std::unique_ptr<Config>&& cfg,
                               UpstreamNode* upstream, BusDeviceInterface* bdi) {
  fbl::AllocChecker ac;
  auto raw_dev = new (&ac) DeviceImpl(parent, std::move(cfg), upstream, bdi);
  if (!ac.check()) {
    zxlogf(ERROR, "Out of memory attemping to create PCIe device %s.", cfg->addr());
    return ZX_ERR_NO_MEMORY;
  }

  auto dev = fbl::AdoptRef(static_cast<Device*>(raw_dev));
  zx_status_t status = raw_dev->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to initialize PCIe device (res %d)", status);
    return status;
  }

  bdi->LinkDevice(dev);
  return ZX_OK;
}

}  // namespace

Device::~Device() {
  // We should already be unlinked from the bus's device tree.
  ZX_DEBUG_ASSERT(disabled_);
  ZX_DEBUG_ASSERT(!plugged_in_);

  // Make certain that all bus access (MMIO, PIO, Bus mastering) has been
  // disabled.  Also, explicitly disable legacy IRQs.
  // TODO(cja/ZX-3147)): Only use the PCIe int disable if PCIe
  ModifyCmd(PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN, PCIE_CFG_COMMAND_INT_DISABLE);

  caps_.list.clear();
  caps_.ext_list.clear();
  // TODO(cja/ZX-3147): Remove this after porting is finished.
  zxlogf(TRACE, "%s [%s] dtor finished", is_bridge() ? "bridge" : "device", cfg_->addr());
}

zx_status_t Device::CreateProxy() {
  // TODO(cja): Workaround due to ZX-3888
  char proxy_arg[2] = ",";
  char name[ZX_MAX_NAME_LEN];
  snprintf(name, sizeof(name), "%02x:%02x.%1x", bus_id(), dev_id(), func_id());
  zx_device_prop_t device_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, vendor_id_},
      {BIND_PCI_DID, 0, device_id_},
      {BIND_PCI_CLASS, 0, class_id_},
      {BIND_PCI_SUBCLASS, 0, subclass_},
      {BIND_PCI_INTERFACE, 0, prog_if_},
      {BIND_PCI_REVISION, 0, rev_id_},
      {BIND_TOPO_PCI, 0, static_cast<uint32_t>(BIND_TOPO_PCI_PACK(bus_id(), dev_id(), func_id()))},
  };

  // Create an isolated devhost to load the proxy pci driver containing the DeviceProxy
  // instance which will talk to this device.
  return DdkAdd(cfg_->addr(), DEVICE_ADD_MUST_ISOLATE, device_props, countof(device_props),
                ZX_PROTOCOL_PCI, proxy_arg);
}

zx_status_t Device::Create(zx_device_t* parent, std::unique_ptr<Config>&& config,
                           UpstreamNode* upstream, BusDeviceInterface* bdi) {
  return DeviceImpl::Create(parent, std::move(config), upstream, bdi);
}

zx_status_t Device::Init() {
  fbl::AutoLock dev_lock(&dev_lock_);

  zx_status_t status = InitLocked();
  if (status != ZX_OK) {
    zxlogf(ERROR, "failed to initialize device %s: %d", cfg_->addr(), status);
    return status;
  }

  // Things went well and the device is in a good state. Flag the device as
  // plugged in and link ourselves up to the graph. This will keep the device
  // alive as long as the Bus owns it.
  upstream_->LinkDevice(this);
  plugged_in_ = true;

  return status;
}

zx_status_t Device::InitLocked() {
  // Cache basic device info
  vendor_id_ = cfg_->Read(Config::kVendorId);
  device_id_ = cfg_->Read(Config::kDeviceId);
  class_id_ = cfg_->Read(Config::kBaseClass);
  subclass_ = cfg_->Read(Config::kSubClass);
  prog_if_ = cfg_->Read(Config::kProgramInterface);
  rev_id_ = cfg_->Read(Config::kRevisionId);

  // Disable the device in event of a failure initializing. TA is disabled
  // because it cannot track the scope of AutoCalls and their associated
  // locking semantics. The lock is grabbed by |Init| and held at this point.
  auto disable = fbl::MakeAutoCall([this]() __TA_NO_THREAD_SAFETY_ANALYSIS { DisableLocked(); });

  // Parse and sanity check the capabilities and extended capabilities lists
  // if they exist
  zx_status_t st = ProbeCapabilities();
  if (st != ZX_OK) {
    zxlogf(ERROR, "device %s encountered an error parsing capabilities: %d", cfg_->addr(), st);
    return st;
  }

  // Now that we know what our capabilities are, initialize our internal IRQ
  // bookkeeping
  // TODO(cja): IRQ initialization

  st = CreateProxy();
  if (st != ZX_OK) {
    zxlogf(ERROR, "device %s couldn't spawn its proxy driver_host: %d", cfg_->addr(), st);
    return st;
  }

  disable.cancel();
  return ZX_OK;
}

zx_status_t Device::ModifyCmd(uint16_t clr_bits, uint16_t set_bits) {
  fbl::AutoLock dev_lock(&dev_lock_);
  // In order to keep internal bookkeeping coherent, and interactions between
  // MSI/MSI-X and Legacy IRQ mode safe, API users may not directly manipulate
  // the legacy IRQ enable/disable bit.  Just ignore them if they try to
  // manipulate the bit via the modify cmd API.
  // TODO(cja) This only applies to PCI(e)
  clr_bits = static_cast<uint16_t>(clr_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);
  set_bits = static_cast<uint16_t>(set_bits & ~PCIE_CFG_COMMAND_INT_DISABLE);

  if (plugged_in_) {
    ModifyCmdLocked(clr_bits, set_bits);
    return ZX_OK;
  }

  return ZX_ERR_UNAVAILABLE;
}

void Device::ModifyCmdLocked(uint16_t clr_bits, uint16_t set_bits) {
  fbl::AutoLock cmd_reg_lock(&cmd_reg_lock_);
  cfg_->Write(Config::kCommand,
              static_cast<uint16_t>((cfg_->Read(Config::kCommand) & ~clr_bits) | set_bits));
}

void Device::Disable() {
  fbl::AutoLock dev_lock(&dev_lock_);
  DisableLocked();
}

void Device::DisableLocked() {
  // Disable a device because we cannot allocate space for all of its BARs (or
  // forwarding windows, in the case of a bridge).  Flag the device as
  // disabled from here on out.
  zxlogf(TRACE, "[%s]%s %s", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);

  // Flag the device as disabled.  Close the device's MMIO/PIO windows, shut
  // off device initiated accesses to the bus, disable legacy interrupts.
  // Basically, prevent the device from doing anything from here on out.
  disabled_ = true;
  AssignCmdLocked(PCIE_CFG_COMMAND_INT_DISABLE);

  // Release all BAR allocations back into the pool they came from.
  for (auto& bar : bars_) {
    bar.allocation = nullptr;
  }
}

zx_status_t Device::EnableBusMaster(bool enabled) {
  fbl::AutoLock dev_lock(&dev_lock_);
  if (enabled && disabled_) {
    return ZX_ERR_BAD_STATE;
  }

  ModifyCmdLocked(enabled ? 0 : PCI_COMMAND_BUS_MASTER_EN, enabled ? PCI_COMMAND_BUS_MASTER_EN : 0);
  return upstream_->EnableBusMasterUpstream(enabled);
}

zx_status_t Device::ProbeBar(uint32_t bar_id) {
  if (bar_id >= bar_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  // If we hit an issue, or a BAR reads as all zeroes then we will bail out
  // and set the size of it to 0. This will result in us not using it further
  // during allocation.
  BarInfo& bar_info = bars_[bar_id];
  auto cleanup = fbl::MakeAutoCall([&bar_info] { bar_info.size = 0; });
  uint32_t bar_val = cfg_->Read(Config::kBar(bar_id));

  bar_info.bar_id = bar_id;
  bar_info.is_mmio = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
  bar_info.is_64bit =
      bar_info.is_mmio && ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
  bar_info.is_prefetchable = bar_info.is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);

  // Sanity check the read-only configuration of the BAR
  if (bar_info.is_64bit && (bar_info.bar_id == bar_count_ - 1)) {
    zxlogf(ERROR, "%s has a 64bit bar in invalid position %u!", cfg_->addr(), bar_info.bar_id);
    return ZX_ERR_BAD_STATE;
  }

  if (bar_info.is_64bit && !bar_info.is_mmio) {
    zxlogf(ERROR, "%s bar %u is 64bit but not mmio!", cfg_->addr(), bar_info.bar_id);
    return ZX_ERR_BAD_STATE;
  }

  // Disable MMIO & PIO access while we perform the probe. We don't want the
  // addresses written during probing to conflict with anything else on the
  // bus. Note: No drivers should have access to this device's registers
  // during the probe process as the device should not have been published
  // yet. That said, there could be other (special case) parts of the system
  // accessing a devices registers at this point in time, like an early init
  // debug console or serial port. Don't make any attempt to print or log
  // until the probe operation has been completed. Hopefully these special
  // systems are quiescent at this point in time, otherwise they might see
  // some minor glitching while access is disabled.
  bool enabled = MmioEnabled() || IoEnabled();
  uint16_t cmd_backup = ReadCmdLocked();
  ModifyCmdLocked(PCI_COMMAND_MEM_EN | PCI_COMMAND_IO_EN, cmd_backup);
  uint32_t addr_mask = (bar_info.is_mmio) ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;

  // For enabled devices save the original address in the BAR. If the device
  // is enabled then we should assume the bios configured it and we should
  // attempt to retain the BAR allocation.
  if (enabled) {
    bar_info.address = bar_val & addr_mask;
  }

  // Write ones to figure out the size of the BAR
  cfg_->Write(Config::kBar(bar_id), UINT32_MAX);
  bar_val = cfg_->Read(Config::kBar(bar_id));
  // BARs that are not wired up return all zeroes on read after writing 1s
  if (bar_val == 0) {
    return ZX_OK;
  }

  uint64_t size_mask = ~(bar_val & addr_mask);
  if (bar_info.is_mmio && bar_info.is_64bit) {
    // This next BAR should not be probed/allocated on its own, so set
    // its size to zero and make it clear it's owned by the previous
    // BAR. We already verified the bar_id is valid above.
    bars_[bar_id + 1].size = 0;
    bars_[bar_id + 1].bar_id = bar_id;

    // Retain the high 32bits of the  address if the device was enabled.
    if (enabled) {
      bar_info.address = static_cast<uint64_t>(cfg_->Read(Config::kBar(bar_id + 1))) << 32;
    }

    // Get the high 32 bits of size for the 64 bit BAR by repeating the
    // steps of writing 1s and then reading the value of the next BAR.
    cfg_->Write(Config::kBar(bar_id + 1), UINT32_MAX);
    size_mask |= static_cast<uint64_t>(~cfg_->Read(Config::kBar(bar_id + 1))) << 32;
  }

  // No matter what configuration we've found, |size_mask| should contain a
  // mask representing all the valid bits that can be set in the address.
  bar_info.size = size_mask + 1;

  // Restore the original bar address values cached above if enabled coming
  // into this probe.
  if (enabled) {
    cfg_->Write(Config::kBar(bar_id), static_cast<uint32_t>(bar_info.address));
    if (bar_info.is_64bit) {
      cfg_->Write(Config::kBar(bar_id + 1), static_cast<uint32_t>(bar_info.address >> 32));
    }
  }

  std::array<char, 8> pretty_size = {};
  zxlogf(TRACE, "%s Region %u: probed %s (%s%sprefetchable) [size=%s]", cfg_->addr(), bar_id,
         (bar_info.is_mmio) ? "Memory" : "I/O ports", (bar_info.is_64bit) ? "64-bit, " : "",
         (bar_info.is_prefetchable) ? "" : "non-",
         format_size(pretty_size.data(), pretty_size.max_size(), bar_info.size));

  // All done, re-enable IO/MMIO access that was disabled prior.
  AssignCmdLocked(cmd_backup);
  cleanup.cancel();
  return ZX_OK;
}

zx_status_t Device::AllocateBar(uint32_t bar_id) {
  ZX_DEBUG_ASSERT(upstream_);
  ZX_DEBUG_ASSERT(bar_id < bar_count_);

  zx_status_t status;
  PciAllocator* allocator;
  BarInfo& bar_info = bars_[bar_id];
  // TODO(cja): It's possible that we may have an unlikely configuration of a prefetchable
  // window that starts below 4GB, ends above 4GB and then has a prefetchable 32bit BAR. If
  // that BAR already had an address we would request it here and be fine, but if it didn't
  // then the below code could potentially fail because it received an address that didn't fit
  // in 32 bits.
  if (bar_info.is_mmio) {
    if (bar_info.is_64bit || bar_info.is_prefetchable) {
      allocator = &upstream_->pf_mmio_regions();
    } else {
      allocator = &upstream_->mmio_regions();
    }
  } else {
    allocator = &upstream_->pio_regions();
  }

  // If we have an address it was found earlier in the probe and we'll try to
  // preserve it.
  if (bar_info.address) {
    status = allocator->AllocateWindow(bar_info.address, bar_info.size, &bar_info.allocation);
    if (status == ZX_OK) {
      // If we successfully grabbed the allocation then we're finished because
      // our metadata already matches what we requested from the allocator.
      zxlogf(TRACE, "%s preserved BAR %u's existing allocation.", cfg_->addr(), bar_info.bar_id);
      return ZX_OK;
    } else {
      zxlogf(TRACE, "%s failed to preserve BAR %u address %lx, reallocating: %d", cfg_->addr(),
             bar_info.bar_id, bar_info.address, status);
      bar_info.address = 0;
    }
  }

  // If we had no address, or we failed to preseve the address, then it's time
  // to take any allocation window possible.
  if (!bar_info.address) {
    status = allocator->AllocateWindow(bar_info.size, &bar_info.allocation);
    // Request a base address of zero to signal we'll take any location in
    // the window.
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s couldn't allocate %#zx for bar %u: %d", cfg_->addr(), bar_info.size,
             bar_info.bar_id, status);
      return status;
    }
  }

  // Now write the allocated address space to the BAR.
  uint16_t cmd_backup = cfg_->Read(Config::kCommand);
  ModifyCmdLocked(PCI_COMMAND_MEM_EN | PCI_COMMAND_IO_EN, cmd_backup);
  cfg_->Write(Config::kBar(bar_id), static_cast<uint32_t>(bar_info.allocation->base()));
  if (bar_info.is_64bit) {
    uint32_t addr_hi = static_cast<uint32_t>(bar_info.allocation->base() >> 32);
    cfg_->Write(Config::kBar(bar_id + 1), addr_hi);
  }
  bar_info.address = bar_info.allocation->base();
  AssignCmdLocked(cmd_backup);

  return ZX_OK;
}

zx_status_t Device::ConfigureBars() {
  fbl::AutoLock dev_lock(&dev_lock_);
  ZX_DEBUG_ASSERT(plugged_in_);
  ZX_DEBUG_ASSERT(bar_count_ <= bars_.max_size());

  // Allocate BARs for the device
  zx_status_t status;
  // First pass, probe BARs to populate the table and grab backing allocations
  // for any BARs that have been allocated by system firmware.
  for (uint32_t bar_id = 0; bar_id < bar_count_; bar_id++) {
    status = ProbeBar(bar_id);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s error probing bar %u: %d. Skipping it.", cfg_->addr(), bar_id, status);
      continue;
    }

    // Allocate the BAR if it was successfully probed.
    if (bars_[bar_id].size) {
      status = AllocateBar(bar_id);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s failed to allocate bar %u: %d", cfg_->addr(), bar_id, status);
      }
    }

    // If the BAR was 64bit then we need to skip the next bar holding its
    // high address bits.
    if (bars_[bar_id].is_64bit) {
      bar_id++;
    }
  }

  return ZX_OK;
}

void Device::Unplug() {
  zxlogf(TRACE, "[%s]%s %s", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);
  // Begin by completely nerfing this device, and preventing an new API
  // operations on it.  We need to be inside the dev lock to do this.  Note:
  // it is assumed that we will not disappear during any of this function,
  // because our caller is holding a reference to us.
  fbl::AutoLock dev_lock(&dev_lock_);
  // Disable should have been called before Unplug and would have disabled
  // everything in the command register
  ZX_DEBUG_ASSERT(disabled_);
  upstream_->UnlinkDevice(this);
  bdi_->UnlinkDevice(this);
  plugged_in_ = false;
  zxlogf(TRACE, "device [%s] unplugged", cfg_->addr());
}

void Device::Dump() const {
  fbl::AutoLock dev_lock(&dev_lock_);
  fbl::StringBuffer<256> log;
  zxlogf(TRACE, "%s at %s vid:did %04x:%04x", (is_bridge()) ? "bridge" : "device", cfg_->addr(),
         vendor_id(), device_id());
  for (size_t i = 0; i < bar_count_; i++) {
    auto& bar = bars_[i];
    if (bar.size) {
      log.AppendPrintf("    bar %zu: %s, %s, addr %#lx, size %#zx [raw: ", i,
                       (bar.is_mmio) ? ((bar.is_64bit) ? "64bit mmio" : "32bit mmio") : "io",
                       (bar.is_prefetchable) ? "pf" : "no-pf", bar.address, bar.size);
      if (bar.is_64bit) {
        log.AppendPrintf("%08x ", cfg_->Read(Config::kBar(bar.bar_id + 1)));
      }
      log.AppendPrintf("%08x ]", cfg_->Read(Config::kBar(bar.bar_id)));
      zxlogf(TRACE, "%s", log.c_str());
      log.Clear();
    }
  }

  if (!caps_.list.is_empty()) {
    log.AppendPrintf("    capabilities: ");
    for (auto& cap : caps_.list) {
      auto id = static_cast<Capability::Id>(cap.id());
      bool end = &cap == &caps_.list.back();
      log.AppendPrintf("%s (%#x)%s", CapabilityIdToName(id), cap.id(), (!end) ? "," : " ");
    }
    zxlogf(TRACE, "%s", log.c_str());
    log.Clear();
  }

  if (!caps_.ext_list.is_empty()) {
    log.AppendPrintf("    extended capabilities: ");
    for (auto& cap : caps_.ext_list) {
      auto id = static_cast<ExtCapability::Id>(cap.id());
      bool end = &cap == &caps_.ext_list.back();
      log.AppendPrintf("%s (%#x)%s", ExtCapabilityIdToName(id), cap.id(), (!end) ? "," : " ");
    }
    zxlogf(TRACE, "%s", log.c_str());
  }
}

}  // namespace pci
