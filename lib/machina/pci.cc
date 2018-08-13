// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/pci.h"

#include <stdio.h>

#include <fbl/auto_lock.h>
#include <hw/pci.h>
#include <zircon/assert.h>

namespace machina {

// PCI ECAM address manipulation.
constexpr uint8_t pci_ecam_bus(uint64_t addr) {
  return bits_shift(addr, 27, 20);
}

constexpr uint8_t pci_ecam_device(uint64_t addr) {
  return bits_shift(addr, 19, 15);
}

constexpr uint8_t pci_ecam_function(uint64_t addr) {
  return bits_shift(addr, 14, 12);
}

constexpr uint16_t pci_ecam_register_etc(uint64_t addr) {
  return bits_shift(addr, 11, 0);
}

// PCI command register bits.
static constexpr uint16_t kPciCommandIoEnable = 1 << 0;
static constexpr uint16_t kPciCommandMemEnable = 1 << 1;
static constexpr uint16_t kPciCommandIntEnable = 1 << 10;

constexpr bool pci_irq_enabled(uint16_t command_register) {
  return (command_register & kPciCommandIntEnable) == 0;
}

// PCI config relative IO port addresses (typically at 0xcf8).
static constexpr uint16_t kPciConfigAddrPortBase = 0;
static constexpr uint16_t kPciConfigAddrPortTop = 3;
static constexpr uint16_t kPciConfigDataPortBase = 4;
static constexpr uint16_t kPciConfigDataPortTop = 7;

// PCI base address registers.
static constexpr uint8_t kPciRegisterBar0 = 0x10;
static constexpr uint8_t kPciRegisterBar1 = 0x14;
static constexpr uint8_t kPciRegisterBar2 = 0x18;
static constexpr uint8_t kPciRegisterBar3 = 0x1c;
static constexpr uint8_t kPciRegisterBar4 = 0x20;
static constexpr uint8_t kPciRegisterBar5 = 0x24;

// PCI capabilities registers.
static constexpr uint8_t kPciRegisterCapBase = 0xa4;
static constexpr uint8_t kPciRegisterCapTop = UINT8_MAX;

// PCI capabilities register layout.
constexpr uint8_t kPciCapTypeOffset = 0;
constexpr uint8_t kPciCapNextOffset = 1;

// Per-device IRQ assignments.
//
// These are provided to the guest via the /pci@10000000 node within the device
// tree, and via the _SB section in the DSDT ACPI table.
//
// The device tree and DSDT define interrupts for 12 devices (IRQ 32-47).
// Adding  additional devices beyond that will require updates to both.
static constexpr uint32_t kPciGlobalIrqAssigments[kPciMaxDevices] = {
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47};

uint64_t PciBar::aspace() const {
  switch (trap_type) {
    case TrapType::MMIO_SYNC:
    case TrapType::MMIO_BELL:
      return kPciBarMmioType64Bit | kPciBarMmioAccessSpace;
    default:
      return 0;
  }
}

uint64_t PciBar::base() const {
  switch (trap_type) {
    case TrapType::MMIO_SYNC:
    case TrapType::MMIO_BELL:
      return addr & kPciBarMmioAddrMask;
    default:
      return 0;
  }
}

zx_status_t PciBar::Read(uint64_t addr, IoValue* value) const {
  if (device == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return device->ReadBar(n, addr, value);
}

zx_status_t PciBar::Write(uint64_t addr, const IoValue& value) {
  if (device == nullptr) {
    return ZX_ERR_BAD_STATE;
  }
  return device->WriteBar(n, addr, value);
}

PciPortHandler::PciPortHandler(PciBus* bus) : bus_(bus) {}

zx_status_t PciPortHandler::Read(uint64_t addr, IoValue* value) const {
  return bus_->ReadIoPort(addr, value);
}

zx_status_t PciPortHandler::Write(uint64_t addr, const IoValue& value) {
  return bus_->WriteIoPort(addr, value);
}

PciEcamHandler::PciEcamHandler(PciBus* bus) : bus_(bus) {}

zx_status_t PciEcamHandler::Read(uint64_t addr, IoValue* value) const {
  return bus_->ReadEcam(addr, value);
}

zx_status_t PciEcamHandler::Write(uint64_t addr, const IoValue& value) {
  return bus_->WriteEcam(addr, value);
}

static constexpr PciDevice::Attributes kRootComplexAttributes = {
    .device_id = PCI_DEVICE_ID_INTEL_Q35,
    .vendor_id = PCI_VENDOR_ID_INTEL,
    .subsystem_id = 0,
    .subsystem_vendor_id = 0,
    .device_class = (PCI_CLASS_BRIDGE_HOST << 16),
};

PciBus::PciBus(Guest* guest, InterruptController* interrupt_controller)
    : guest_(guest),
      ecam_handler_(this),
      port_handler_(this),
      interrupt_controller_(interrupt_controller),
      root_complex_(kRootComplexAttributes) {}

zx_status_t PciBus::Init() {
  root_complex_.bar_[0].size = 0x10;
  root_complex_.bar_[0].trap_type = TrapType::MMIO_SYNC;
  zx_status_t status = Connect(&root_complex_);
  if (status != ZX_OK) {
    return status;
  }

  // Setup ECAM trap for a single bus.
  status = guest_->CreateMapping(TrapType::MMIO_SYNC, kPciEcamPhysBase,
                                 pci_ecam_size(0, 1), 0, &ecam_handler_);
  if (status != ZX_OK) {
    return status;
  }

#if __x86_64__
  // Setup PIO trap.
  status = guest_->CreateMapping(TrapType::PIO_SYNC, kPciConfigPortBase,
                                 kPciConfigPortSize, 0, &port_handler_);
  if (status != ZX_OK) {
    return status;
  }
#endif

  return ZX_OK;
}

uint32_t PciBus::config_addr() {
  fbl::AutoLock lock(&mutex_);
  return config_addr_;
}

void PciBus::set_config_addr(uint32_t addr) {
  fbl::AutoLock lock(&mutex_);
  config_addr_ = addr;
}

zx_status_t PciBus::Connect(PciDevice* device) {
  if (next_open_slot_ >= kPciMaxDevices) {
    FXL_LOG(ERROR) << "No PCI device slots available";
    return ZX_ERR_OUT_OF_RANGE;
  }
  ZX_DEBUG_ASSERT(device_[next_open_slot_] == nullptr);
  size_t slot = next_open_slot_++;

  // Initialize BAR registers.
  for (uint8_t bar_num = 0; bar_num < kPciMaxBars; ++bar_num) {
    // Skip unimplemented bars.
    if (!device->is_bar_implemented(bar_num)) {
      break;
    }

    device->bus_ = this;
    PciBar* bar = &device->bar_[bar_num];
    bar->size = static_cast<uint16_t>(align(bar->size, PAGE_SIZE));
    bar->addr = mmio_base_;
    mmio_base_ += bar->size;
  }
  if (mmio_base_ >= machina::kPciMmioBarPhysBase + machina::kPciMmioBarSize) {
    FXL_LOG(ERROR) << "No PCI MMIO address space available";
    return ZX_ERR_NO_RESOURCES;
  }

  device->command_ = kPciCommandIoEnable | kPciCommandMemEnable;
  device->global_irq_ = kPciGlobalIrqAssigments[slot];
  device_[slot] = device;

  zx_status_t status = device->SetupBarTraps(guest_);
  if (status == ZX_OK) {
    FXL_LOG(INFO) << "PCI bus connected device " << slot << " to device ID 0x"
                  << std::hex << device->attrs_.device_id;
  }
  return status;
}

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1: All PCI devices must
// treat Configuration Space write operations to reserved registers as no-ops;
// that is, the access must be completed normally on the bus and the data
// discarded.
static inline zx_status_t pci_write_unimplemented_register() { return ZX_OK; }

static inline zx_status_t pci_write_unimplemented_device() { return ZX_OK; }

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1: Read accesses to reserved
// or unimplemented registers must be completed normally and a data value of 0
// returned.
static inline zx_status_t pci_read_unimplemented_register(uint32_t* value) {
  *value = 0;
  return ZX_OK;
}

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1: The host bus to PCI bridge
// must unambiguously report attempts to read the Vendor ID of non-existent
// devices. Since 0 FFFFh is an invalid Vendor ID, it is adequate for the host
// bus to PCI bridge to return a value of all 1's on read accesses to
// Configuration Space registers of non-existent devices.
static inline zx_status_t pci_read_unimplemented_device(IoValue* value) {
  value->u32 = bit_mask<uint32_t>(value->access_size * 8);
  return ZX_OK;
}

zx_status_t PciBus::ReadEcam(uint64_t addr, IoValue* value) const {
  const uint8_t device = pci_ecam_device(addr);
  const uint16_t reg = pci_ecam_register_etc(addr);
  const bool valid =
      is_addr_valid(pci_ecam_bus(addr), device, pci_ecam_function(addr));
  if (!valid) {
    return pci_read_unimplemented_device(value);
  }

  return device_[device]->ReadConfig(reg, value);
}

zx_status_t PciBus::WriteEcam(uint64_t addr, const IoValue& value) {
  const uint8_t device = pci_ecam_device(addr);
  const uint16_t reg = pci_ecam_register_etc(addr);
  const bool valid =
      is_addr_valid(pci_ecam_bus(addr), device, pci_ecam_function(addr));
  if (!valid) {
    return pci_write_unimplemented_device();
  }

  return device_[device]->WriteConfig(reg, value);
}

zx_status_t PciBus::ReadIoPort(uint64_t port, IoValue* value) const {
  switch (port) {
    case kPciConfigAddrPortBase ... kPciConfigAddrPortTop: {
      uint64_t bit_offset = (port - kPciConfigAddrPortBase) * 8;
      uint32_t mask = bit_mask<uint32_t>(value->access_size * 8);

      fbl::AutoLock lock(&mutex_);
      uint32_t addr = config_addr_ >> bit_offset;
      value->u32 = addr & mask;
      return ZX_OK;
    }
    case kPciConfigDataPortBase ... kPciConfigDataPortTop: {
      uint32_t addr;
      uint64_t reg;
      {
        fbl::AutoLock lock(&mutex_);
        addr = config_addr_;
        if (!is_addr_valid(pci_type1_bus(addr), pci_type1_device(addr),
                           pci_type1_function(addr))) {
          return pci_read_unimplemented_device(value);
        }
      }

      PciDevice* device = device_[pci_type1_device(addr)];
      reg = pci_type1_register(addr) + port - kPciConfigDataPortBase;
      return device->ReadConfig(reg, value);
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t PciBus::WriteIoPort(uint64_t port, const IoValue& value) {
  switch (port) {
    case kPciConfigAddrPortBase ... kPciConfigAddrPortTop: {
      // Software can (and Linux does) perform partial word accesses to the
      // PCI address register. This means we need to take care to read/write
      // portions of the 32bit register without trampling the other bits.
      uint64_t bit_offset = (port - kPciConfigAddrPortBase) * 8;
      uint32_t bit_size = value.access_size * 8;
      uint32_t mask = bit_mask<uint32_t>(bit_size);

      fbl::AutoLock lock(&mutex_);
      // Clear out the bits we'll be modifying.
      config_addr_ = clear_bits(config_addr_, bit_size, bit_offset);
      // Set the bits of the address.
      config_addr_ |= (value.u32 & mask) << bit_offset;
      return ZX_OK;
    }
    case kPciConfigDataPortBase ... kPciConfigDataPortTop: {
      uint32_t addr;
      uint64_t reg;
      {
        fbl::AutoLock lock(&mutex_);
        addr = config_addr_;

        if (!is_addr_valid(pci_type1_bus(addr), pci_type1_device(addr),
                           pci_type1_function(addr))) {
          return pci_write_unimplemented_device();
        }

        reg = pci_type1_register(addr) + port - kPciConfigDataPortBase;
      }
      PciDevice* device = device_[pci_type1_device(addr)];
      return device->WriteConfig(reg, value);
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t PciBus::Interrupt(PciDevice& device) {
  {
    fbl::AutoLock lock(&device.mutex_);
    if (!pci_irq_enabled(device.command_)) {
      device.pending_irq_ = true;
      return ZX_OK;
    }
    device.pending_irq_ = false;
  }
  return interrupt_controller_->Interrupt(device.global_irq_);
}

// PCI Local Bus Spec v3.0 Section 6.7: Each capability must be DWORD aligned.
static inline uint8_t pci_cap_len(const pci_cap_t* cap) {
  return align(cap->len, 4);
}

PciDevice::PciDevice(const Attributes attrs) : attrs_(attrs) {}

const pci_cap_t* PciDevice::FindCapability(uint8_t addr, uint8_t* cap_index,
                                           uint32_t* cap_base) const {
  uint32_t base = kPciRegisterCapBase;
  for (uint8_t i = 0; i < num_capabilities_; ++i) {
    const pci_cap_t* cap = &capabilities_[i];
    uint8_t cap_len = pci_cap_len(cap);
    if (addr >= base + cap_len) {
      base += cap_len;
      continue;
    }
    *cap_index = i;
    *cap_base = base;
    return cap;
  }

  // Given address doesn't lie within the range of addresses occupied by
  // capabilities.
  return nullptr;
}

zx_status_t PciDevice::ReadCapability(uint8_t addr, uint32_t* out) const {
  uint8_t cap_index;
  uint32_t cap_base;
  const pci_cap_t* cap = FindCapability(addr, &cap_index, &cap_base);
  if (cap == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  uint32_t word = 0;
  uint32_t cap_offset = addr - cap_base;
  for (uint8_t byte = 0; byte < sizeof(word); ++byte, ++cap_offset) {
    // In the case of padding bytes, return 0.
    if (cap_offset >= cap->len) {
      break;
    }

    // PCI Local Bus Spec v3.0 Section 6.7:
    // Each capability in the list consists of an 8-bit ID field assigned
    // by the PCI SIG, an 8 bit pointer in configuration space to the next
    // capability, and some number of additional registers immediately
    // following the pointer to implement that capability.
    uint32_t val = 0;
    switch (cap_offset) {
      case kPciCapTypeOffset:
        val = cap->id;
        break;
      case kPciCapNextOffset:
        // PCI Local Bus Spec v3.0 Section 6.7: A pointer value of 00h is
        // used to indicate the last capability in the list.
        if (cap_index + 1u < num_capabilities_) {
          val = cap_base + pci_cap_len(cap);
        }
        break;
      default:
        val = cap->data[cap_offset];
        break;
    }
    word |= val << (byte * 8);
  }

  *out = word;
  return ZX_OK;
}

// Read a 4 byte aligned value from PCI config space.
zx_status_t PciDevice::ReadConfigWord(uint8_t reg, uint32_t* value) const {
  switch (reg) {
    //  ---------------------------------
    // |   (31..16)     |    (15..0)     |
    // |   device_id    |   vendor_id    |
    //  ---------------------------------
    case PCI_CONFIG_VENDOR_ID:
      *value = attrs_.vendor_id;
      *value |= attrs_.device_id << 16;
      return ZX_OK;
    //  ----------------------------
    // |   (31..16)  |   (15..0)    |
    // |   status    |    command   |
    //  ----------------------------
    case PCI_CONFIG_COMMAND: {
      fbl::AutoLock lock(&mutex_);
      *value = command_;

      uint16_t status = PCI_STATUS_INTERRUPT;
      if (capabilities_ != nullptr) {
        status |= PCI_STATUS_NEW_CAPS;
      }
      *value |= status << 16;
      return ZX_OK;
    }
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case PCI_CONFIG_REVISION_ID:
      *value = attrs_.device_class;
      return ZX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case PCI_CONFIG_CACHE_LINE_SIZE:
      *value = PCI_HEADER_TYPE_STANDARD << 16;
      return ZX_OK;
    case kPciRegisterBar0:
    case kPciRegisterBar1:
    case kPciRegisterBar2:
    case kPciRegisterBar3:
    case kPciRegisterBar4:
    case kPciRegisterBar5: {
      const uint64_t pci_reg = (reg - kPciRegisterBar0) / 4;
      const uint64_t bar_num = pci_reg / 2;
      const bool high_word = pci_reg % 2;
      if (bar_num >= kPciMaxBars) {
        return pci_read_unimplemented_register(value);
      }

      fbl::AutoLock lock(&mutex_);
      const PciBar* bar = &bar_[bar_num];
      if (!high_word) {
        *value = bar->addr | bar->aspace();
      } else {
        *value = bar->addr >> 32;
      }
      return ZX_OK;
    }
    //  -------------------------------------------------------------
    // |   (31..24)  |  (23..16)   |    (15..8)     |    (7..0)      |
    // | max_latency |  min_grant  | interrupt_pin  | interrupt_line |
    //  -------------------------------------------------------------
    case PCI_CONFIG_INTERRUPT_LINE: {
      const uint8_t interrupt_pin = 1;
      *value = interrupt_pin << 8;
      return ZX_OK;
    }
    //  -------------------------------------------
    // |   (31..16)        |         (15..0)       |
    // |   subsystem_id    |  subsystem_vendor_id  |
    //  -------------------------------------------
    case PCI_CONFIG_SUBSYS_VENDOR_ID:
      *value = attrs_.subsystem_vendor_id;
      *value |= attrs_.subsystem_id << 16;
      return ZX_OK;
    //  ------------------------------------------
    // |     (31..8)     |         (7..0)         |
    // |     Reserved    |  capabilities_pointer  |
    //  ------------------------------------------
    case PCI_CONFIG_CAPABILITIES:
      *value = 0;
      if (capabilities_ != nullptr) {
        *value |= kPciRegisterCapBase;
      }
      return ZX_OK;
    case kPciRegisterCapBase ... kPciRegisterCapTop:
      if (ReadCapability(reg, value) != ZX_ERR_NOT_FOUND) {
        return ZX_OK;
      }
    // Fall-through if the capability is not-implemented.
    default:
      return pci_read_unimplemented_register(value);
  }
}

zx_status_t PciDevice::ReadConfig(uint64_t reg, IoValue* value) const {
  // Perform 4-byte aligned read and then shift + mask the result to get the
  // expected value.
  uint32_t word = 0;
  const uint8_t reg_mask = bit_mask<uint8_t>(2);
  uint8_t word_aligend_reg = static_cast<uint8_t>(reg & ~reg_mask);
  uint8_t bit_offset = static_cast<uint8_t>((reg & reg_mask) * 8);
  zx_status_t status = ReadConfigWord(word_aligend_reg, &word);
  if (status != ZX_OK) {
    return status;
  }

  word >>= bit_offset;
  word &= bit_mask<uint32_t>(value->access_size * 8);
  value->u32 = word;
  return ZX_OK;
}

zx_status_t PciDevice::WriteConfig(uint64_t reg, const IoValue& value) {
  switch (reg) {
    case PCI_CONFIG_COMMAND: {
      if (value.access_size != 2) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      bool fire_pending_irq = false;
      {
        fbl::AutoLock lock(&mutex_);
        command_ = value.u16;
        // If we have a pending IRQ and this write will enable interrupts for
        // this device, we'll inject that pending IRQ now.
        fire_pending_irq = pending_irq_ && pci_irq_enabled(command_);
      }
      if (fire_pending_irq) {
        return Interrupt();
      }
      return ZX_OK;
    }
    case kPciRegisterBar0:
    case kPciRegisterBar1:
    case kPciRegisterBar2:
    case kPciRegisterBar3:
    case kPciRegisterBar4:
    case kPciRegisterBar5: {
      if (value.access_size != 4) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      const uint64_t pci_reg = (reg - kPciRegisterBar0) / 4;
      const uint64_t bar_num = pci_reg / 2;
      const bool high_word = pci_reg % 2;
      if (bar_num >= kPciMaxBars) {
        return pci_write_unimplemented_register();
      }

      fbl::AutoLock lock(&mutex_);
      PciBar* bar = &bar_[bar_num];
      auto addr = reinterpret_cast<uint32_t*>(&bar->addr);
      // We zero bits in the BAR in order to set the size.
      if (!high_word) {
        addr[0] = value.u32;
        addr[0] &= ~(bar->size - 1);
      } else {
        addr[1] = value.u32;
        addr[1] &= ~((bar->size - 1) >> 32);
      }
      return ZX_OK;
    }
    default:
      return pci_write_unimplemented_register();
  }
}

zx_status_t PciDevice::SetupBarTraps(Guest* guest) {
  for (uint8_t i = 0; i < kPciMaxBars; ++i) {
    PciBar* bar = &bar_[i];
    if (!is_bar_implemented(i)) {
      break;
    }

    bar->n = i;
    bar->device = this;
    zx_status_t status =
        guest->CreateMapping(bar->trap_type, bar->base(), bar->size, 0, bar);
    if (status != ZX_OK) {
      return status;
    }
  }

  return ZX_OK;
}

zx_status_t PciDevice::Interrupt() {
  if (!bus_) {
    return ZX_ERR_BAD_STATE;
  }
  return bus_->Interrupt(*this);
}

}  // namespace machina
