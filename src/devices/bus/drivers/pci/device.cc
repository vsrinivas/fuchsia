// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bus/drivers/pci/device.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <lib/fit/defer.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/zx/interrupt.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <array>
#include <optional>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/string_buffer.h>
#include <pretty/sizes.h>

#include "src/devices/bus/drivers/pci/bus_device_interface.h"
#include "src/devices/bus/drivers/pci/capabilities/msi.h"
#include "src/devices/bus/drivers/pci/capabilities/msix.h"
#include "src/devices/bus/drivers/pci/common.h"
#include "src/devices/bus/drivers/pci/pci_bind.h"
#include "src/devices/bus/drivers/pci/ref_counted.h"
#include "src/devices/bus/drivers/pci/upstream_node.h"

namespace pci {

static const zx_bind_inst_t sysmem_match[] = {
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_SYSMEM),
};

static const device_fragment_part_t sysmem_fragment[] = {
    {countof(sysmem_match), sysmem_match},
};

namespace {  // anon namespace.  Externals do not need to know about DeviceImpl

class DeviceImpl : public Device {
 public:
  static zx_status_t Create(zx_device_t* parent, std::unique_ptr<Config>&& cfg,
                            UpstreamNode* upstream, BusDeviceInterface* bdi, inspect::Node node);

  // Implement ref counting, do not let derived classes override.
  PCI_IMPLEMENT_REFCOUNTED;

  // Disallow copying, assigning and moving.
  DISALLOW_COPY_ASSIGN_AND_MOVE(DeviceImpl);

 protected:
  DeviceImpl(zx_device_t* parent, std::unique_ptr<Config>&& cfg, UpstreamNode* upstream,
             BusDeviceInterface* bdi, inspect::Node node)
      : Device(parent, std::move(cfg), upstream, bdi, std::move(node), false) {}
};

zx_status_t DeviceImpl::Create(zx_device_t* parent, std::unique_ptr<Config>&& cfg,
                               UpstreamNode* upstream, BusDeviceInterface* bdi,
                               inspect::Node node) {
  fbl::AllocChecker ac;
  auto raw_dev = new (&ac) DeviceImpl(parent, std::move(cfg), upstream, bdi, std::move(node));
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

Device::Device(zx_device_t* parent, std::unique_ptr<Config>&& config, UpstreamNode* upstream,
               BusDeviceInterface* bdi, inspect::Node node, bool is_bridge)
    : PciDeviceType(parent),
      cfg_(std::move(config)),
      upstream_(upstream),
      bdi_(bdi),
      bar_count_(is_bridge ? PCI_BAR_REGS_PER_BRIDGE : PCI_BAR_REGS_PER_DEVICE),
      is_bridge_(is_bridge) {
  metrics_.node = std::move(node);
  metrics_.legacy.node = metrics_.node.CreateChild(kInspectLegacyInterrupt);
  metrics_.msi.node = metrics_.node.CreateChild(kInspectMsi);

  metrics_.irq_mode =
      metrics_.node.CreateString(kInspectIrqMode, kInspectIrqModes[PCI_IRQ_MODE_DISABLED]);
  uint8_t pin = cfg_->Read(Config::kInterruptPin);
  switch (pin) {
    case 1:
    case 2:
    case 3:
    case 4: {
      // register values 1-4 map to pins A-D
      char s[2] = {static_cast<char>('A' + (pin - 1)), '\0'};
      metrics_.legacy.pin = metrics_.legacy.node.CreateString(kInspectLegacyInterruptPin, s);
      break;
    }
  }
  // Line should always exist if a pin exists, unless there was no mapping in the _PRT.
  uint8_t line = cfg_->Read(Config::kInterruptLine);
  if (line != 0 && line != 0xFF) {
    metrics_.legacy.line = metrics_.legacy.node.CreateUint(kInspectLegacyInterruptLine, line);
  }
  metrics_.legacy.ack_count = metrics_.legacy.node.CreateUint(kInspectLegacyAckCount, 0);
  metrics_.legacy.signal_count = metrics_.legacy.node.CreateUint(kInspectLegacySignalCount, 0);
  metrics_.legacy.disabled = metrics_.legacy.node.CreateBool(kInspectLegacyDisabled, false);
  metrics_.msi.base_vector = metrics_.msi.node.CreateUint(kInspectMsiBaseVector, 0);
  metrics_.msi.allocated = metrics_.msi.node.CreateUint(kInspectMsiAllocated, 0);
}

Device::~Device() {
  // We should already be unlinked from the bus's device tree.
  ZX_DEBUG_ASSERT(disabled_);
  ZX_DEBUG_ASSERT(!plugged_in_);

  // Make certain that all bus access (MMIO, PIO, Bus mastering) has been
  // disabled and disable IRQs.
  DisableInterrupts();
  EnableBusMaster(false);
  ModifyCmd(/*clr_bits=*/PCI_CFG_COMMAND_IO_EN | PCI_CFG_COMMAND_MEM_EN, /*set_bits=*/0);
  // TODO(cja/fxbug.dev/32979): Remove this after porting is finished.
  zxlogf(TRACE, "%s [%s] dtor finished", is_bridge() ? "bridge" : "device", cfg_->addr());
}

zx_status_t Device::CreateProxy() {
  auto pci_bind_topo = static_cast<uint32_t>(BIND_PCI_TOPO_PACK(bus_id(), dev_id(), func_id()));
  // clang-format off
  zx_device_prop_t pci_device_props[] = {
      {BIND_PROTOCOL, 0, ZX_PROTOCOL_PCI},
      {BIND_PCI_VID, 0, vendor_id_},
      {BIND_PCI_DID, 0, device_id_},
      {BIND_PCI_CLASS, 0, class_id_},
      {BIND_PCI_SUBCLASS, 0, subclass_},
      {BIND_PCI_INTERFACE, 0, prog_if_},
      {BIND_PCI_REVISION, 0, rev_id_},
      {BIND_PCI_TOPO, 0, pci_bind_topo},
  };
  // clang-format on

  // Create an isolated devhost to load the proxy pci driver containing the PciProxy
  // instance which will talk to this device.
  zx_status_t status = DdkAdd(
      ddk::DeviceAddArgs(cfg_->addr()).set_props(pci_device_props).set_proto_id(ZX_PROTOCOL_PCI));
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to create pci fragment %s: %s", cfg_->addr(),
           zx_status_get_string(status));
  }

  const zx_bind_inst_t pci_fragment_match[] = {
      BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
      BI_ABORT_IF(NE, BIND_PCI_VID, vendor_id_),
      BI_ABORT_IF(NE, BIND_PCI_DID, device_id_),
      BI_ABORT_IF(NE, BIND_PCI_CLASS, class_id_),
      BI_ABORT_IF(NE, BIND_PCI_SUBCLASS, subclass_),
      BI_ABORT_IF(NE, BIND_PCI_INTERFACE, prog_if_),
      BI_ABORT_IF(NE, BIND_PCI_REVISION, rev_id_),
      BI_ABORT_IF(EQ, BIND_COMPOSITE, 1),
      BI_MATCH_IF(EQ, BIND_PCI_TOPO, pci_bind_topo),

  };

  const device_fragment_part_t pci_fragment[] = {
      {countof(pci_fragment_match), pci_fragment_match},
  };

  const device_fragment_t fragments[] = {
      {"sysmem", countof(sysmem_fragment), sysmem_fragment},
      {"pci", countof(pci_fragment), pci_fragment},
  };

  composite_device_desc_t composite_desc = {
      .props = pci_device_props,
      .props_count = countof(pci_device_props),
      .fragments = fragments,
      .fragments_count = countof(fragments),
      .primary_fragment = "pci",
      .spawn_colocated = false,
  };

  char composite_name[ZX_DEVICE_NAME_MAX];
  snprintf(composite_name, sizeof(composite_name), "pci-%s", cfg_->addr());
  return DdkAddComposite(composite_name, &composite_desc);
}

zx_status_t Device::Create(zx_device_t* parent, std::unique_ptr<Config>&& config,
                           UpstreamNode* upstream, BusDeviceInterface* bdi, inspect::Node node) {
  return DeviceImpl::Create(parent, std::move(config), upstream, bdi, std::move(node));
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

zx_status_t Device::InitInterrupts() {
  zx_status_t status = zx::interrupt::create(*zx::unowned_resource(ZX_HANDLE_INVALID), 0,
                                             ZX_INTERRUPT_VIRTUAL, &irqs_.legacy);
  if (status != ZX_OK) {
    zxlogf(ERROR, "device %s could not create its legacy interrupt: %s", cfg_->addr(),
           zx_status_get_string(status));
    return status;
  }

  // Disable all interrupt modes until a driver enables the preferred method.
  // The legacy interrupt is disabled by hand because our Enable/Disable methods
  // for doing so need to interact with the Shared IRQ lists in Bus.
  ModifyCmdLocked(/*clr_bits=*/0, /*set_bits=*/PCIE_CFG_COMMAND_INT_DISABLE);
  irqs_.legacy_vector = 0;

  if (caps_.msi && (status = DisableMsi()) != ZX_OK) {
    zxlogf(ERROR, "failed to disable MSI: %s", zx_status_get_string(status));
    return status;
  }

  if (caps_.msix && (status = DisableMsix()) != ZX_OK) {
    zxlogf(ERROR, "failed to disable MSI-X: %s", zx_status_get_string(status));
    return status;
  }

  irqs_.mode = PCI_IRQ_MODE_DISABLED;
  return ZX_OK;
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
  auto disable = fit::defer([this]() __TA_NO_THREAD_SAFETY_ANALYSIS { DisableLocked(); });

  // Parse and sanity check the capabilities and extended capabilities lists
  // if they exist
  zx_status_t st = ProbeCapabilities();
  if (st != ZX_OK) {
    zxlogf(ERROR, "device %s encountered an error parsing capabilities: %d", cfg_->addr(), st);
    return st;
  }

  // Now that we know what our capabilities are, initialize our internal IRQ
  // bookkeeping and disable all interrupts until a driver requests them.
  st = InitInterrupts();
  if (st != ZX_OK) {
    return st;
  }

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
  zxlogf(TRACE, "[%s] %s %s", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);

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
  // Only allow bus mastering to be turned off if the device is disabled.
  if (enabled && disabled_) {
    return ZX_ERR_BAD_STATE;
  }

  ModifyCmdLocked(enabled ? /*clr_bits=*/0 : /*set_bits=*/PCI_CFG_COMMAND_BUS_MASTER_EN,
                  enabled ? /*clr_bits=*/PCI_CFG_COMMAND_BUS_MASTER_EN : /*set_bits=*/0);
  return upstream_->EnableBusMasterUpstream(enabled);
}

// Configures the BAR represented by |bar| by writing to its register and configuring
// IO and Memory access accordingly.
zx_status_t Device::WriteBarInformation(const Bar& bar) {
  // Now write the allocated address space to the BAR.
  uint16_t cmd_backup = cfg_->Read(Config::kCommand);
  // Figure out the IO type of the bar and disable that while we adjust the bar address.
  uint16_t mem_io_en_flag = (bar.is_mmio) ? PCI_CFG_COMMAND_MEM_EN : PCI_CFG_COMMAND_IO_EN;
  ModifyCmdLocked(mem_io_en_flag, cmd_backup);

  cfg_->Write(Config::kBar(bar.bar_id), static_cast<uint32_t>(bar.address));
  if (bar.is_64bit) {
    uint32_t addr_hi = static_cast<uint32_t>(bar.address >> 32);
    cfg_->Write(Config::kBar(bar.bar_id + 1), addr_hi);
  }
  // Flip the IO bit back on for this type of bar
  AssignCmdLocked(cmd_backup | mem_io_en_flag);
  return ZX_OK;
}

zx_status_t Device::ProbeBar(uint8_t bar_id) {
  if (bar_id >= bar_count_) {
    return ZX_ERR_INVALID_ARGS;
  }

  Bar& bar = bars_[bar_id];
  uint32_t bar_val = cfg_->Read(Config::kBar(bar_id));
  bar.bar_id = bar_id;
  bar.is_mmio = (bar_val & PCI_BAR_IO_TYPE_MASK) == PCI_BAR_IO_TYPE_MMIO;
  bar.is_64bit = bar.is_mmio && ((bar_val & PCI_BAR_MMIO_TYPE_MASK) == PCI_BAR_MMIO_TYPE_64BIT);
  bar.is_prefetchable = bar.is_mmio && (bar_val & PCI_BAR_MMIO_PREFETCH_MASK);
  bar.size = 0;  // Default to an unused BAR until probing is properly completed.

  // Sanity check the read-only configuration of the BAR
  if (bar.is_64bit && (bar.bar_id == bar_count_ - 1)) {
    zxlogf(ERROR, "[%s] has a 64bit bar in invalid position %u!", cfg_->addr(), bar.bar_id);
    return ZX_ERR_BAD_STATE;
  }

  if (bar.is_64bit && !bar.is_mmio) {
    zxlogf(ERROR, "[%s] bar %u is 64bit but not mmio!", cfg_->addr(), bar.bar_id);
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
  ModifyCmdLocked(/*clr_bits=*/PCI_CFG_COMMAND_MEM_EN | PCI_CFG_COMMAND_IO_EN,
                  /*set_bits=*/cmd_backup);
  uint32_t addr_mask = (bar.is_mmio) ? PCI_BAR_MMIO_ADDR_MASK : PCI_BAR_PIO_ADDR_MASK;

  // For enabled devices save the original address in the BAR. If the device
  // is enabled then we should assume the bios configured it and we should
  // attempt to retain the BAR allocation.
  if (enabled) {
    bar.address = bar_val & addr_mask;
  }

  // Write ones to figure out the size of the BAR
  cfg_->Write(Config::kBar(bar_id), UINT32_MAX);
  bar_val = cfg_->Read(Config::kBar(bar_id));
  // BARs that are not wired up return all zeroes on read after probing.
  if (bar_val == 0) {
    return ZX_OK;
  }

  uint64_t size_mask = ~(bar_val & addr_mask);
  if (bar.is_mmio && bar.is_64bit) {
    // This next BAR should not be probed/allocated on its own, so set
    // its size to zero and make it clear it's owned by the previous
    // BAR. We already verified the bar_id is valid above.
    bars_[bar_id + 1].size = 0;
    bars_[bar_id + 1].bar_id = bar_id;

    // Retain the high 32bits of the 64bit address address if the device was
    // enabled already.
    if (enabled) {
      bar.address |= static_cast<uint64_t>(cfg_->Read(Config::kBar(bar_id + 1))) << 32;
    }

    // Get the high 32 bits of size for the 64 bit BAR by repeating the
    // steps of writing 1s and then reading the value of the next BAR.
    cfg_->Write(Config::kBar(bar_id + 1), UINT32_MAX);
    size_mask |= static_cast<uint64_t>(~cfg_->Read(Config::kBar(bar_id + 1))) << 32;
  } else if (!bar.is_mmio && !(bar_val & (UINT16_MAX << 16))) {
    // Per spec, if the type is IO and the upper 16 bits were zero in the read
    // then they should be removed from the size mask before incrementing it.
    size_mask &= UINT16_MAX;
  }

  // No matter what configuration we've found, |size_mask| should contain a
  // mask representing all the valid bits that can be set in the address.
  bar.size = size_mask + 1;

  // Write the original address value we had before probing and re-enable its
  // access mode now that probing is complete.
  WriteBarInformation(bar);

  std::array<char, 8> pretty_size = {};
  zxlogf(DEBUG, "[%s] Region %u: probed %s (%s%sprefetchable) [size=%s]", cfg_->addr(), bar_id,
         (bar.is_mmio) ? "Memory" : "I/O ports", (bar.is_64bit) ? "64-bit, " : "",
         (bar.is_prefetchable) ? "" : "non-",
         format_size(pretty_size.data(), pretty_size.max_size(), bar.size));
  return ZX_OK;
}

// Allocates appropriate address space for BAR |bar| out of any suitable
// upstream allocators, using |base| as the base address if present.
zx::status<std::unique_ptr<PciAllocation>> Device::AllocateFromUpstream(
    const Bar& bar, std::optional<zx_paddr_t> base) {
  ZX_DEBUG_ASSERT(bar.size > 0);
  std::unique_ptr<PciAllocation> allocation;

  // On all platforms if a BAR is not marked in its register as MMIO then it
  // goes through the Root Host IO/PIO allocator, regardless of whether the
  // platform's IO is actually MMIO backed.
  if (!bar.is_mmio) {
    return upstream_->pio_regions().Allocate(base, bar.size);
  }

  // Prefetchable bars *must* come from a prefetchable region. However, Bridges
  // only allocate 64 bit space to the prefetchable window. This means if we
  // want to allocate a 64 bit BAR then it must also come from the prefetchable
  // window. At the Root Host level if no address base is provided it will
  // attempt to allocate from the 32 bit allocator if the platform does not
  // populate any space in the > 4GB region, but this does not matter at the
  // level of endpoints below a bridge since they will be assigning out of the
  // address windows provided to their upstream bridges.
  // TODO(fxb/32978): Do we need to worry about BARs that want to span the 4GB boundary?
  if (bar.is_prefetchable || bar.is_64bit) {
    if (auto result = upstream_->pf_mmio_regions().Allocate(base, bar.size); result.is_ok()) {
      return result;
    }
  }

  // If the BAR is 32 bit, or for some reason the 64 bit window wasn't populated
  // them fall back to the 32 bit allocator. 64 bit BARs are commonly allocated
  // out of the < 4GB range on Intel platforms.
  return upstream_->mmio_regions().Allocate(base, bar.size);
}

// Higher level method to allocate address space a previously probed BAR id
// |bar_id| and handle configuration space setup.
zx_status_t Device::AllocateBar(uint8_t bar_id) {
  ZX_DEBUG_ASSERT(upstream_);
  ZX_DEBUG_ASSERT(bar_id < bar_count_);
  Bar& bar = bars_[bar_id];
  ZX_DEBUG_ASSERT(bar.size);

  // The goal is to try to allocate the same window configured by the
  // bootloader/bios, but if unavailable then allocate an appropriately sized
  // window from anywhere in the upstream allocator.
  std::unique_ptr<PciAllocation> allocation = {};
  if (auto result = AllocateFromUpstream(bar, bar.address); result.is_ok()) {
    bar.allocation = std::move(result.value());
  } else if (auto result = AllocateFromUpstream(bar, std::nullopt); result.is_ok()) {
    bar.allocation = std::move(result.value());
  } else {
    return ZX_ERR_NOT_FOUND;
  }

  bar.address = bar.allocation->base();
  WriteBarInformation(bar);
  zxlogf(TRACE, "[%s] allocated [%#lx, %#lx) to BAR%u", cfg_->addr(), bar.allocation->base(),
         bar.allocation->base() + bar.allocation->size(), bar.bar_id);

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
      zxlogf(ERROR, "[%s] error probing bar %u: %d. Skipping it.", cfg_->addr(), bar_id, status);
      continue;
    }

    // Allocate the BAR if it was successfully probed.
    if (bars_[bar_id].size) {
      status = AllocateBar(bar_id);
      if (status != ZX_OK) {
        zxlogf(ERROR, "[%s] failed to allocate bar %u: %d", cfg_->addr(), bar_id, status);
        return status;
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
  zxlogf(TRACE, "[%s] %s %s", cfg_->addr(), (is_bridge()) ? " (b)" : "", __func__);
  fbl::AutoLock dev_lock(&dev_lock_);
  // Disable should have been called before Unplug and would have disabled
  // everything in the command register
  ZX_DEBUG_ASSERT(disabled_);
  upstream_->UnlinkDevice(this);
  // After unplugging from the Bus there should be no further references to this
  // device and the dtor will be called.
  bdi_->UnlinkDevice(this);
  plugged_in_ = false;
  zxlogf(TRACE, "device [%s] unplugged", cfg_->addr());
}

}  // namespace pci
