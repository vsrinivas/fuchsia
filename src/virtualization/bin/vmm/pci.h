// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_PCI_H_
#define SRC_VIRTUALIZATION_BIN_VMM_PCI_H_

#include <fidl/fuchsia.hardware.pci/cpp/wire.h>
#include <lib/stdcompat/span.h>
#include <lib/zx/result.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <array>
#include <mutex>
#include <vector>

#include <fbl/array.h>

#include "src/virtualization/bin/vmm/bits.h"
#include "src/virtualization/bin/vmm/guest.h"
#include "src/virtualization/bin/vmm/interrupt_controller.h"
#include "src/virtualization/bin/vmm/io.h"
#include "src/virtualization/bin/vmm/platform_device.h"

// clang-format off

// PCI configuration constants.
#define PCI_VENDOR_ID_INTEL         0x8086u
#define PCI_DEVICE_ID_INTEL_Q35     0x29c0u
#define PCI_CLASS_BRIDGE_HOST       0x0600u

// clang-format on

class Guest;

static constexpr size_t kPciMaxDevices = 16;
static constexpr size_t kPciMaxBars = 2;
static constexpr uint8_t kPciCapMinSize = 2;  // Minimum size of a PCI capability, in bytes.

static constexpr uint64_t kPciBarMmioAccessSpace = 0;
static constexpr uint64_t kPciBarMmioType64Bit = 0b10 << 1;
static constexpr uint64_t kPciBarMmioAddrMask = ~bit_mask<uint64_t>(4);

// PCI type 1 address manipulation.
constexpr uint8_t pci_type1_bus(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 23, 16));
}

constexpr uint8_t pci_type1_device(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 15, 11));
}

constexpr uint8_t pci_type1_function(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 10, 8));
}

constexpr uint8_t pci_type1_register(uint64_t addr) {
  return static_cast<uint8_t>(bits_shift(addr, 7, 2) << 2);
}

class PciBus;
class PciDevice;

// 64-bit PCI Base Address Register (BAR)
//
// PCI BARs indicate a region in memory or (for x86) the IO Port space
// that is used to interact with the device.
//
// This class tracks the size/region/type of such a region and implements
// logic to call back into the device to handle reads and writes as
// necessary.
//
// Thread compatible.
class PciBar : public IoHandler {
 public:
  class Callback {
   public:
    virtual zx_status_t Read(uint64_t offset, IoValue* value) = 0;
    virtual zx_status_t Write(uint64_t offset, const IoValue& value) = 0;
  };

  // Construct a BAR of the given type, size, and ID.
  //
  // `size` will be rounded up to be a power of two, and at least PAGE_SIZE.
  PciBar(PciDevice* device, uint64_t size, TrapType trap_type, Callback* callback);

  // Get the size / type of the region.
  uint64_t size() const { return size_; }
  TrapType trap_type() const { return trap_type_; }

  // Get/set base address.
  //
  // Setting the address overwrites any guest-configured value of the register.
  uint64_t addr() const { return addr_; }
  void set_addr(uint64_t value);

  // Get/set the high/low 32-bits of the BAR registers in the PCI config space.
  //
  // Each 64-bit BAR occupies two 32-bit slots in the config space,
  // so `slot` must be 0 or 1.
  uint32_t pci_config_reg(size_t slot) const;
  void set_pci_config_reg(size_t slot, uint32_t value);

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override;

 private:
  // Number of 32-bit registers this BAR occupies.
  static constexpr size_t kNumBarSlots = 2;

  // Calculate the low bits of the BAR containing the type of address space
  // this BAR represents.
  uint32_t AspaceType() const;

  // Pointer to the owning device.
  PciDevice* device_;

  // Callback used for read/write accesses.
  //
  // Owned elsewhere.
  Callback* callback_;

  // Base address.
  //
  // This is the real base address of the BAR. The value in the PCI
  // configuration registers can be modified by the guest, but don't actually
  // cause the location of the BAR to change.
  uint64_t addr_;

  // Size of region, in bytes.
  uint64_t size_;

  // The type of trap to create for this region.
  TrapType trap_type_;

  // Raw registers exposed in the PCI config space.
  std::array<uint32_t, kNumBarSlots> pci_config_reg_;
};

/* Stores the state of PCI devices. */
class PciDevice {
 public:
  // Static attributes associated with a device.
  struct Attributes {
    std::string_view name;
    // Device attributes.
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;
    // class, subclass, prog_if, and revision id.
    uint32_t device_class;
  };

  // Handle accesses to this device config space.
  zx_status_t ReadConfig(uint64_t reg, IoValue* value) const;
  zx_status_t WriteConfig(uint64_t reg, const IoValue& value);

  // If interrupts are enabled and the device has one pending, send it to the
  // bus.
  zx_status_t Interrupt();

  // Return a human-readable name for this device, for debugging and logging.
  std::string_view name() { return attrs_.name; }

  // Returns a pointer to a base address register for this device.
  //
  // Returns nullptr if the register is not implemented.
  const PciBar* bar(size_t n) const { return n < bars_.size() ? &bars_[n] : nullptr; }

  // Return static device attributes.
  const Attributes& attrs() const { return attrs_; }

 protected:
  explicit PciDevice(const Attributes& attrs);
  virtual ~PciDevice() = default;

  // Install the given POD type as a PCI capability.
  //
  // Capabilities types must have a size aligned to 32-bits.
  //
  // The "next" pointer in cap header (byte 2) will be overwritten by
  // the function, and need not contain any particular value.
  template <typename T>
  zx_status_t AddCapability(const T& capability) {
    static_assert(sizeof(T) >= kPciCapMinSize, "Caps must be at least kPciCapMinSize bytes.");
    static_assert(std::is_pod<T>::value, "Type T should be POD.");
    static_assert(std::has_unique_object_representations<T>::value,
                  "Type T should not contain implicit padding.");
    return AddCapability(cpp20::span(reinterpret_cast<const uint8_t*>(&capability), sizeof(T)));
  }

  // Install the given PciBar in the next available slot, returning the index
  // the PciBar was installed at.
  //
  // Returns ZX_ERR_NO_RESOURCES if all BARs have already been used.
  zx::result<size_t> AddBar(PciBar bar);

 private:
  friend class PciBus;

  // Install a capability from the given payload.
  zx_status_t AddCapability(cpp20::span<const uint8_t> payload);

  // Setup traps and handlers for accesses to BAR regions.
  zx_status_t SetupBarTraps(Guest* guest, async_dispatcher_t* dispatcher);

  zx_status_t ReadConfigWord(uint8_t reg, uint32_t* value) const;

  // Read 32-bit from the capability area of the device's config space.
  zx_status_t ReadCapability(size_t offset, uint32_t* out) const __TA_REQUIRES(mutex_);

  // Returns true when an interrupt is active.
  virtual bool HasPendingInterrupt() const = 0;

  mutable std::mutex mutex_;

  // Base address registers.
  std::vector<PciBar> bars_;

  // Static attributes for this device.
  const Attributes attrs_;

  // PCI bus this device is connected to.
  PciBus* bus_ = nullptr;

  // IRQ vector assigned by the bus.
  uint32_t global_irq_ = 0;

  // PCI config register "command".
  uint16_t command_ __TA_GUARDED(mutex_) = 0;

  // PCI config register "interrupt line".
  //
  // The value written here is not used by us for anything, but
  // software relies on storing arbitrary values here.
  uint8_t reg_interrupt_line_ __TA_GUARDED(mutex_) = 0;

  // Capability section of the config space.
  std::vector<uint8_t> capabilities_ __TA_GUARDED(mutex_);

  // Offset to the beginning of the final capability in the config space.
  std::optional<uint8_t> last_cap_offset_ __TA_GUARDED(mutex_);
};

class PciPortHandler : public IoHandler {
 public:
  explicit PciPortHandler(PciBus* bus);
  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "PCI Bus"; }

 private:
  PciBus* bus_;
};

class PciEcamHandler : public IoHandler {
 public:
  explicit PciEcamHandler(PciBus* bus);
  zx_status_t Read(uint64_t addr, IoValue* value) override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;
  std::string_view Name() const override { return "PCI Bus"; }

 private:
  PciBus* bus_;
};

class PciRootComplex : public PciDevice {
 public:
  explicit PciRootComplex(const Attributes& attrs);

 private:
  bool HasPendingInterrupt() const override { return false; }
};

class PciBus : public PlatformDevice {
 public:
  PciBus(Guest* guest, InterruptController* interrupt_controller);

  zx_status_t Init(async_dispatcher_t* dispatcher);

  // Connect a PCI device to the bus.
  //
  // |slot| must be between 1 and kPciMaxDevices (slot 0 is reserved for the
  // root complex).
  //
  // This method is *not* thread-safe and must only be called during
  // initialization.
  zx_status_t Connect(PciDevice* device,
                      async_dispatcher_t* dispatcher) __TA_NO_THREAD_SAFETY_ANALYSIS;

  // Access devices via the ECAM region.
  //
  // |addr| is the offset from the start of the ECAM region for this bus.
  zx_status_t ReadEcam(uint64_t addr, IoValue* value) const;
  zx_status_t WriteEcam(uint64_t addr, const IoValue& value);

  // Handle access to the PC IO ports (0xcf8 - 0xcff).
  zx_status_t ReadIoPort(uint64_t port, IoValue* value) const;
  zx_status_t WriteIoPort(uint64_t port, const IoValue& value);

  // Raise an interrupt for the given device.
  zx_status_t Interrupt(PciDevice& device) const;

  // Returns true if |bus|, |device|, |function| corresponds to a valid
  // device address.
  bool is_addr_valid(uint8_t bus, uint8_t device, uint8_t function) const {
    return bus == 0 && device < kPciMaxDevices && function == 0 && device_[device];
  }

  // Current config address selected by the 0xcf8 IO port.
  uint32_t config_addr();
  void set_config_addr(uint32_t addr);

  PciDevice* root_complex() { return &root_complex_; }

  zx_status_t ConfigureDtb(void* dtb) const override;

 private:
  mutable std::mutex mutex_;

  Guest* guest_;
  PciEcamHandler ecam_handler_;
  PciPortHandler port_handler_;

  // Selected address in PCI config space.
  uint32_t config_addr_ __TA_GUARDED(mutex_) = 0;

  // Devices on the virtual PCI bus.
  PciDevice* device_[kPciMaxDevices] = {};
  // IO APIC for use with interrupt redirects.
  InterruptController* interrupt_controller_ = nullptr;
  // Embedded root complex device.
  PciRootComplex root_complex_;
  // Next mmio window to be allocated to connected devices.
  uint64_t mmio_base_;
  // Pointer to the next open PCI slot.
  size_t next_open_slot_ = 0;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_PCI_H_
