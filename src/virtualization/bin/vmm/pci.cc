// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/pci.h"

#include <endian.h>
#include <lib/stdcompat/bit.h>
#include <lib/trace/event.h>
#include <stdio.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <algorithm>

__BEGIN_CDECLS
#include <libfdt.h>
__END_CDECLS

namespace fpci = fuchsia_hardware_pci;

namespace {

// PCI ECAM address manipulation.
constexpr uint8_t pci_ecam_bus(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 27, 20));
}

constexpr uint8_t pci_ecam_device(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 19, 15));
}

constexpr uint8_t pci_ecam_function(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 14, 12));
}

constexpr uint16_t pci_ecam_register_etc(uint64_t addr) {
  return static_cast<uint16_t>(bits_shift(addr, 11, 0));
}

// The size of an ECAM region depends on values in the MCFG ACPI table. For
// each ECAM region there is a defined physical base address as well as a bus
// start/end value for that region.
//
// When creating an ECAM address for a PCI configuration register, the bus
// value must be relative to the starting bus number for that ECAM region.
inline constexpr uint64_t pci_ecam_size(uint64_t start_bus, uint64_t end_bus) {
  return (end_bus - start_bus) << 20;
}

// PCI command register bits.
constexpr uint16_t kPciCommandIoEnable = 1 << 0;
constexpr uint16_t kPciCommandMemEnable = 1 << 1;
constexpr uint16_t kPciCommandIntEnable = 1 << 10;

constexpr bool pci_irq_enabled(uint16_t command_register) {
  return (command_register & kPciCommandIntEnable) == 0;
}

// PCI config relative IO port addresses (typically at 0xcf8).
constexpr uint16_t kPciConfigAddrPortBase = 0;
constexpr uint16_t kPciConfigAddrPortTop = 3;
constexpr uint16_t kPciConfigDataPortBase = 4;
constexpr uint16_t kPciConfigDataPortTop = 7;

// PCI base address registers.
constexpr uint8_t kPciRegisterBar0 = 0x10;
constexpr uint8_t kPciRegisterBar1 = 0x14;
constexpr uint8_t kPciRegisterBar2 = 0x18;
constexpr uint8_t kPciRegisterBar3 = 0x1c;
constexpr uint8_t kPciRegisterBar4 = 0x20;
constexpr uint8_t kPciRegisterBar5 = 0x24;

// PCI capabilities registers.
constexpr uint8_t kPciRegisterCapBase = 0xa4;
constexpr uint8_t kPciRegisterCapTop = UINT8_MAX;

// Size of the PCI capability space in bytes.
constexpr uint8_t kPciRegisterCapMaxBytes = kPciRegisterCapTop - kPciRegisterCapBase + 1;

// PCI capabilities register layout.
constexpr uint8_t kPciCapNextOffset = 1;

// clang-format off

// PCI memory ranges.
#if __aarch64__
constexpr uint64_t kPciEcamPhysBase    = 0x808100000;
constexpr uint64_t kPciMmioBarPhysBase = 0x808200000;
#elif __x86_64__
constexpr uint64_t kPciEcamPhysBase    = 0xf8100000;
constexpr uint64_t kPciMmioBarPhysBase = 0xf8200000;
constexpr uint64_t kPciConfigPortBase  = 0xcf8;
constexpr uint64_t kPciConfigPortSize  = 0x8;
#else
#error Unknown architecture.
#endif

constexpr uint64_t kPciEcamSize        = pci_ecam_size(0, 1);
constexpr uint64_t kPciMmioBarSize     = 0x100000;

// clang-format on

// Per-device IRQ assignments.
//
// These are provided to the guest via the /pci@10000000 node within the device
// tree, and via the _SB section in the DSDT ACPI table.
//
// The device tree and DSDT define interrupts for 12 devices (IRQ 32-47).
// Adding  additional devices beyond that will require updates to both.
constexpr uint32_t kPciGlobalIrqAssigments[kPciMaxDevices] = {32, 33, 34, 35, 36, 37, 38, 39,
                                                              40, 41, 42, 43, 44, 45, 46, 47};

}  // namespace

PciBar::PciBar(PciDevice* device, uint64_t size, TrapType trap_type, Callback* callback)
    : device_(device),
      callback_(callback),
      addr_(0),
      // BARs must have a power-of-two size. (PCI 3.0, Section 6.2.5.1)
      size_(std::max(cpp20::bit_ceil(size), static_cast<size_t>(PAGE_SIZE))),
      trap_type_(trap_type) {
  ZX_DEBUG_ASSERT(size > 0);
  set_addr(0);  // Initialise the `pci_config_reg_` registers.
}

uint32_t PciBar::AspaceType() const {
  switch (trap_type_) {
    case TrapType::MMIO_SYNC:
    case TrapType::MMIO_BELL:
      return kPciBarMmioType64Bit | kPciBarMmioAccessSpace;
    default:
      return 0;
  }
}

uint32_t PciBar::pci_config_reg(size_t slot) const {
  ZX_DEBUG_ASSERT(slot < kNumBarSlots);
  return pci_config_reg_[slot];
}

void PciBar::set_addr(uint64_t value) {
  addr_ = value;
  set_pci_config_reg(0, static_cast<uint32_t>(value >> 0));
  set_pci_config_reg(1, static_cast<uint32_t>(value >> 32));
}

void PciBar::set_pci_config_reg(size_t slot, uint32_t value) {
  ZX_DEBUG_ASSERT(slot < kNumBarSlots);

  // We zero bits in the BAR in order to set the size.
  if (slot == 0) {
    pci_config_reg_[0] = value;
    pci_config_reg_[0] &= ~(size_ - 1);
    pci_config_reg_[0] |= AspaceType();
  } else {
    pci_config_reg_[1] = value;
    pci_config_reg_[1] &= ~((size_ - 1) >> 32);
  }
}

zx_status_t PciBar::Read(uint64_t addr, IoValue* value) {
  TRACE_DURATION("machina", "pci_readbar", "vendor_id", TA_UINT32(device_->attrs().vendor_id),
                 "device_id", TA_UINT32(device_->attrs().device_id), "offset", TA_UINT64(addr),
                 "access_size", TA_UINT32(value->access_size));
  return callback_->Read(addr, value);
}

zx_status_t PciBar::Write(uint64_t addr, const IoValue& value) {
  TRACE_DURATION("machina", "pci_writebar", "vendor_id", TA_UINT32(device_->attrs().vendor_id),
                 "device_id", TA_UINT32(device_->attrs().device_id), "offset", TA_UINT64(addr),
                 "access_size", TA_UINT32(value.access_size));
  return callback_->Write(addr, value);
}

std::string_view PciBar::Name() const { return device_->name(); }

PciPortHandler::PciPortHandler(PciBus* bus) : bus_(bus) {}

zx_status_t PciPortHandler::Read(uint64_t addr, IoValue* value) {
  return bus_->ReadIoPort(addr, value);
}

zx_status_t PciPortHandler::Write(uint64_t addr, const IoValue& value) {
  return bus_->WriteIoPort(addr, value);
}

PciEcamHandler::PciEcamHandler(PciBus* bus) : bus_(bus) {}

zx_status_t PciEcamHandler::Read(uint64_t addr, IoValue* value) {
  return bus_->ReadEcam(addr, value);
}

zx_status_t PciEcamHandler::Write(uint64_t addr, const IoValue& value) {
  return bus_->WriteEcam(addr, value);
}

static constexpr PciDevice::Attributes kRootComplexAttributes = {
    .name = "Intel Q35",
    .device_id = PCI_DEVICE_ID_INTEL_Q35,
    .vendor_id = PCI_VENDOR_ID_INTEL,
    .subsystem_id = 0,
    .subsystem_vendor_id = 0,
    .device_class = (PCI_CLASS_BRIDGE_HOST << 16),
};

PciRootComplex::PciRootComplex(const Attributes& attrs) : PciDevice(attrs) {}

PciBus::PciBus(Guest* guest, InterruptController* interrupt_controller)
    : guest_(guest),
      ecam_handler_(this),
      port_handler_(this),
      interrupt_controller_(interrupt_controller),
      root_complex_(kRootComplexAttributes),
      mmio_base_(kPciMmioBarPhysBase) {}

zx_status_t PciBus::Init(async_dispatcher_t* dispatcher) {
  zx_status_t status = Connect(&root_complex_, dispatcher);
  if (status != ZX_OK) {
    return status;
  }

  // Setup ECAM trap for a single bus.
  status =
      guest_->CreateMapping(TrapType::MMIO_SYNC, kPciEcamPhysBase, kPciEcamSize, 0, &ecam_handler_);
  if (status != ZX_OK) {
    return status;
  }

#if __x86_64__
  // Setup PIO trap.
  status = guest_->CreateMapping(TrapType::PIO_SYNC, kPciConfigPortBase, kPciConfigPortSize, 0,
                                 &port_handler_);
  if (status != ZX_OK) {
    return status;
  }
#endif

  return ZX_OK;
}

uint32_t PciBus::config_addr() {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_addr_;
}

void PciBus::set_config_addr(uint32_t addr) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_addr_ = addr;
}

zx_status_t PciBus::Connect(PciDevice* device, async_dispatcher_t* dispatcher) {
  if (next_open_slot_ >= kPciMaxDevices) {
    FX_LOGS(ERROR) << "No PCI device slots available";
    return ZX_ERR_OUT_OF_RANGE;
  }
  ZX_DEBUG_ASSERT(device_[next_open_slot_] == nullptr);
  size_t slot = next_open_slot_++;

  // Initialize BAR registers.
  //
  // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.2.5.1: "[A]ll
  // address spaces used are a power of two in size and are naturally
  // aligned."
  for (PciBar& bar : device->bars_) {
    // Naturally align the base of this BAR (i.e., align to its size),
    // and also ensure it is on its own page.
    mmio_base_ = align(mmio_base_, std::max(static_cast<uint64_t>(PAGE_SIZE), bar.size()));

    // Assign an address.
    bar.set_addr(mmio_base_);
    mmio_base_ += bar.size();
  }
  if (mmio_base_ >= kPciMmioBarPhysBase + kPciMmioBarSize) {
    FX_LOGS(ERROR) << "No PCI MMIO address space available";
    return ZX_ERR_NO_RESOURCES;
  }

  device->bus_ = this;
  device->command_ = kPciCommandIoEnable | kPciCommandMemEnable;
  device->global_irq_ = kPciGlobalIrqAssigments[slot];
  device_[slot] = device;
  return device->SetupBarTraps(guest_, dispatcher);
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
  const bool valid = is_addr_valid(pci_ecam_bus(addr), device, pci_ecam_function(addr));
  if (!valid) {
    return pci_read_unimplemented_device(value);
  }

  return device_[device]->ReadConfig(reg, value);
}

zx_status_t PciBus::WriteEcam(uint64_t addr, const IoValue& value) {
  const uint8_t device = pci_ecam_device(addr);
  const uint16_t reg = pci_ecam_register_etc(addr);
  const bool valid = is_addr_valid(pci_ecam_bus(addr), device, pci_ecam_function(addr));
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

      std::lock_guard<std::mutex> lock(mutex_);
      uint32_t addr = config_addr_ >> bit_offset;
      value->u32 = addr & mask;
      return ZX_OK;
    }
    case kPciConfigDataPortBase ... kPciConfigDataPortTop: {
      uint32_t addr;
      uint64_t reg;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        addr = config_addr_;
        if (!is_addr_valid(pci_type1_bus(addr), pci_type1_device(addr), pci_type1_function(addr))) {
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

      std::lock_guard<std::mutex> lock(mutex_);
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
        std::lock_guard<std::mutex> lock(mutex_);
        addr = config_addr_;

        if (!is_addr_valid(pci_type1_bus(addr), pci_type1_device(addr), pci_type1_function(addr))) {
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

zx_status_t PciBus::Interrupt(PciDevice& device) const {
  return interrupt_controller_->Interrupt(device.global_irq_);
}

zx_status_t PciBus::ConfigureDtb(void* dtb) const {
  uint64_t reg_val[2] = {htobe64(kPciEcamPhysBase), htobe64(kPciEcamSize)};
  int node_off = fdt_node_offset_by_prop_value(dtb, -1, "reg", reg_val, sizeof(reg_val));
  if (node_off < 0) {
    FX_LOGS(ERROR) << "Failed to find PCI in DTB";
    return ZX_ERR_INTERNAL;
  }
  int ret = fdt_node_check_compatible(dtb, node_off, "pci-host-ecam-generic");
  if (ret != 0) {
    FX_LOGS(ERROR) << "Device with PCI registers is not PCI compatible";
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

PciDevice::PciDevice(const Attributes& attrs) : attrs_(attrs) {}

zx_status_t PciDevice::AddCapability(cpp20::span<const uint8_t> payload) {
  std::lock_guard<std::mutex> lock(mutex_);

  // PCI Local Bus Spec v3.0 Section 6.7: Each capability must be DWORD aligned.
  //
  // Invariant: We keep capabilities_.size() DWORD (4 byte) aligned by adding
  // padding if neccessary. This means any new data appened to the end of the
  // buffer will be aligned by default.
  ZX_DEBUG_ASSERT(is_aligned(capabilities_.size(), sizeof(uint32_t)));
  size_t padding = align(payload.size(), sizeof(uint32_t)) - payload.size();

  // Ensure we won't exceeded the capability space.
  if (capabilities_.size() + payload.size() + padding > kPciRegisterCapMaxBytes) {
    return ZX_ERR_NO_RESOURCES;
  }

  // Copy the payload and padding into the buffer.
  auto cap_start = static_cast<uint8_t>(capabilities_.size());
  capabilities_.insert(capabilities_.end(), payload.begin(), payload.end());
  capabilities_.insert(capabilities_.end(), padding, 0);

  // Set the "next" pointer of this cap to 0, indicating this is the last cap.
  //
  //   PCI Local Bus Spec v3.0 Section 6.7: A pointer value of 00h is
  //   used to indicate the last capability in the list.
  capabilities_.at(cap_start + kPciCapNextOffset) = 0;

  // If we have a previous capability, patch its next pointer to point to
  // this capability.
  //
  //   PCI Local Bus Spec v3.0 Section 6.7: Each capability in the list
  //   consists of an 8-bit ID field assigned by the PCI SIG, an 8 bit
  //   pointer in configuration space to the next capability, and some
  //   number of additional registers immediately following the pointer
  //   to implement that capability.
  if (last_cap_offset_.has_value()) {
    capabilities_[*last_cap_offset_ + kPciCapNextOffset] = cap_start + kPciRegisterCapBase;
  }

  // Track the beginning of this cap.
  last_cap_offset_ = cap_start;

  return ZX_OK;
}

zx::result<size_t> PciDevice::AddBar(PciBar bar) {
  if (bars_.size() >= kPciMaxBars) {
    return zx::error(ZX_ERR_NO_RESOURCES);
  }
  bars_.emplace_back(std::move(bar));
  return zx::ok(bars_.size() - 1);
}

zx_status_t PciDevice::ReadCapability(size_t offset, uint32_t* out) const {
  // Ensure our read is aligned.
  if (!is_aligned(offset, sizeof(uint32_t))) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Ensure we are not reading beyond the capability size.
  if (static_cast<size_t>(offset) + sizeof(uint32_t) > capabilities_.size()) {
    return ZX_ERR_NOT_FOUND;
  }

  // Read the given word.
  memcpy(out, capabilities_.data() + offset, sizeof(uint32_t));
  return ZX_OK;
}

// Read a 4 byte aligned value from PCI config space.
zx_status_t PciDevice::ReadConfigWord(uint8_t reg, uint32_t* value) const {
  switch (reg) {
    //  ---------------------------------
    // |   (31..16)     |    (15..0)     |
    // |   device_id    |   vendor_id    |
    //  ---------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kVendorId):
      *value = attrs_.vendor_id;
      *value |= attrs_.device_id << 16;
      return ZX_OK;
    //  ----------------------------
    // |   (31..16)  |   (15..0)    |
    // |   status    |    command   |
    //  ----------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kCommand): {
      std::lock_guard<std::mutex> lock(mutex_);
      *value = command_;

      uint16_t status = static_cast<uint16_t>(fpci::wire::Status::kInterrupt);
      if (!capabilities_.empty()) {
        status |= static_cast<uint16_t>(fpci::wire::Status::kNewCaps);
      }
      *value |= status << 16;
      return ZX_OK;
    }
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kRevisionId):
      *value = attrs_.device_class;
      return ZX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kCacheLineSize):
      *value = static_cast<uint16_t>(static_cast<uint8_t>(fpci::wire::HeaderType::kStandard)) << 16;
      return ZX_OK;
    case kPciRegisterBar0:
    case kPciRegisterBar1:
    case kPciRegisterBar2:
    case kPciRegisterBar3:
    case kPciRegisterBar4:
    case kPciRegisterBar5: {
      const uint64_t pci_reg = (reg - kPciRegisterBar0) / 4;
      const uint64_t bar_num = pci_reg / 2;
      const size_t index = pci_reg % 2;

      std::lock_guard<std::mutex> lock(mutex_);
      if (bar_num >= bars_.size()) {
        return pci_read_unimplemented_register(value);
      }
      *value = bars_[bar_num].pci_config_reg(index);
      return ZX_OK;
    }
    //  -------------------------------------------------------------
    // |   (31..24)  |  (23..16)   |    (15..8)     |    (7..0)      |
    // | max_latency |  min_grant  | interrupt_pin  | interrupt_line |
    //  -------------------------------------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kInterruptLine): {
      std::lock_guard<std::mutex> lock(mutex_);
      const uint8_t interrupt_pin = 1;
      *value = (interrupt_pin << 8) | reg_interrupt_line_;
      return ZX_OK;
    }
    //  -------------------------------------------
    // |   (31..16)        |         (15..0)       |
    // |   subsystem_id    |  subsystem_vendor_id  |
    //  -------------------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kSubsystemVendorId):
      *value = attrs_.subsystem_vendor_id;
      *value |= attrs_.subsystem_id << 16;
      return ZX_OK;
    //  ------------------------------------------
    // |     (31..8)     |         (7..0)         |
    // |     Reserved    |  capabilities_pointer  |
    //  ------------------------------------------
    case fidl::ToUnderlying(fpci::wire::Config::kCapabilitiesPtr): {
      *value = 0;
      std::lock_guard<std::mutex> lock(mutex_);
      if (!capabilities_.empty()) {
        *value |= kPciRegisterCapBase;
      }
      return ZX_OK;
    }
    case kPciRegisterCapBase ... kPciRegisterCapTop: {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ReadCapability(/*offset=*/(reg - kPciRegisterCapBase), value) != ZX_ERR_NOT_FOUND) {
        return ZX_OK;
      }
    }
    // Fall-through if the capability is not-implemented.
    default:
      return pci_read_unimplemented_register(value);
  }
}

zx_status_t PciDevice::ReadConfig(uint64_t reg, IoValue* value) const {
  // Ensure address / size are naturally aligned.
  if (reg % value->access_size != 0) {
    FX_LOGS(WARNING) << "Guest attempted unaligned read from PCI configuration space. Device: \""
                     << attrs_.name << "\", config register: " << std::hex << reg
                     << ", access size: " << static_cast<uint32_t>(value->access_size);
    return ZX_ERR_IO;
  }

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
  // Ensure address / size are naturally aligned.
  if (reg % value.access_size != 0) {
    FX_LOGS(ERROR) << "Guest attempted unaligned write to PCI configuration space. Device: \""
                   << attrs_.name << "\", config register: " << std::hex << reg
                   << ", access size: " << static_cast<uint32_t>(value.access_size);
    return ZX_ERR_IO;
  }

  switch (reg) {
    case fidl::ToUnderlying(fpci::wire::Config::kCommand): {
      if (value.access_size != 2) {
        return ZX_ERR_NOT_SUPPORTED;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        command_ = value.u16;
      }
      // If this write enables interrupts, send any pending interrupts to the
      // bus.
      return Interrupt();
    }
    case fidl::ToUnderlying(fpci::wire::Config::kInterruptLine): {
      // The 8-byte `interrupt_line` register is R/W, while the other registers
      // are read-only. (PCI 3.0, Section 6.2.4)
      std::lock_guard<std::mutex> lock(mutex_);
      reg_interrupt_line_ = value.u8;
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
      const size_t slot = pci_reg % 2;

      std::lock_guard<std::mutex> lock(mutex_);
      if (bar_num >= bars_.size()) {
        return pci_write_unimplemented_register();
      }
      bars_[bar_num].set_pci_config_reg(slot, value.u32);
      return ZX_OK;
    }
    default:
      return pci_write_unimplemented_register();
  }
}

zx_status_t PciDevice::SetupBarTraps(Guest* guest, async_dispatcher_t* dispatcher) {
  for (PciBar& bar : bars_) {
    if (bar.trap_type() == TrapType::MMIO_BELL) {
      continue;
    }

    zx_status_t status =
        guest->CreateMapping(bar.trap_type(), bar.addr(), bar.size(), 0, &bar, dispatcher);
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
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!pci_irq_enabled(command_) || !HasPendingInterrupt()) {
      return ZX_OK;
    }
  }
  return bus_->Interrupt(*this);
}
