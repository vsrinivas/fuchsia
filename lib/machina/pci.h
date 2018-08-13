// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_PCI_H_
#define GARNET_LIB_MACHINA_PCI_H_

#include <fbl/mutex.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "garnet/lib/machina/address.h"
#include "garnet/lib/machina/bits.h"
#include "garnet/lib/machina/guest.h"
#include "garnet/lib/machina/interrupt_controller.h"
#include "garnet/lib/machina/io.h"

// clang-format off

// PCI configuration constants.
#define PCI_VENDOR_ID_INTEL         0x8086u
#define PCI_DEVICE_ID_INTEL_Q35     0x29c0u
#define PCI_CLASS_BRIDGE_HOST       0x0600u

// clang-format on

class Guest;

namespace machina {

static constexpr size_t kPciMaxDevices = 16;
static constexpr size_t kPciMaxBars = 2;

static constexpr uint64_t kPciBarMmioAccessSpace = 0;
static constexpr uint64_t kPciBarMmioType64Bit = 0b10 << 1;
static constexpr uint64_t kPciBarMmioAddrMask = ~bit_mask<uint64_t>(4);

// PCI type 1 address manipulation.
constexpr uint8_t pci_type1_bus(uint64_t addr) {
  return bits_shift(addr, 23, 16);
}

constexpr uint8_t pci_type1_device(uint64_t addr) {
  return bits_shift(addr, 15, 11);
}

constexpr uint8_t pci_type1_function(uint64_t addr) {
  return bits_shift(addr, 10, 8);
}

constexpr uint8_t pci_type1_register(uint64_t addr) {
  return bits_shift(addr, 7, 2) << 2;
}

class PciBus;
class PciDevice;

// PCI capability structure.
//
// The 1-byte next pointer will be computed dynamically while traversing the
// capabilities list.
typedef struct pci_cap {
  // PCI capability ID as defined in PCI LOCAL BUS SPECIFICATION, REV. 3.0
  // Appendix H.
  uint8_t id;
  // Data for this capability. Must be at least |len| bytes. The first two bytes
  // will be ignored (id and next) as these will be populated dynamically.
  // They're skipped over in the data pointer to allow common structures to be
  // used for read/write where the id/next pointers are embedded in the
  // structure.
  uint8_t* data;
  // Size of |data|.
  uint8_t len;
} pci_cap_t;

struct PciBar : public IoHandler {
  // Register value.
  uint64_t addr;
  // Size of this BAR.
  uint64_t size;
  // The type of trap to create for this region.
  TrapType trap_type;

  // Pointer to the owning device.
  PciDevice* device;
  // Bar number.
  uint8_t n;

  // IoHandler interface.
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

  uint64_t aspace() const;
  uint64_t base() const;
};

/* Stores the state of PCI devices. */
class PciDevice {
 public:
  // Static attributes associated with a device.
  struct Attributes {
    // Device attributes.
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;
    // class, subclass, prog_if, and revision id.
    uint32_t device_class;
  };

  // Read from a region mapped by a BAR register.
  virtual zx_status_t ReadBar(uint8_t bar, uint64_t addr,
                              IoValue* value) const {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Write to a region mapped by a BAR register.
  virtual zx_status_t WriteBar(uint8_t bar, uint64_t addr,
                               const IoValue& value) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Handle accesses to this device config space.
  zx_status_t ReadConfig(uint64_t reg, IoValue* value) const;
  zx_status_t WriteConfig(uint64_t reg, const IoValue& value);

  // Send the configured interrupt for this device.
  zx_status_t Interrupt();

  // Determines if the given base address register is implemented for this
  // device.
  bool is_bar_implemented(size_t bar) const {
    return bar < kPciMaxBars && bar_[bar].size > 0;
  }

  // Returns a pointer to a base address register for this device.
  //
  // Returns nullptr if the register is not implemented.
  const PciBar* bar(size_t n) const {
    return is_bar_implemented(n) ? &bar_[n] : nullptr;
  }

  // Install a capability list.
  void set_capabilities(const pci_cap_t* caps, size_t num_caps) {
    capabilities_ = caps;
    num_capabilities_ = num_caps;
  }

 protected:
  PciDevice(const Attributes attrs);

  // Base address registers.
  PciBar bar_[kPciMaxBars] = {};

 private:
  friend class PciBus;

  // Setup traps and handlers for accesses to BAR regions.
  zx_status_t SetupBarTraps(Guest* guest);

  zx_status_t ReadConfigWord(uint8_t reg, uint32_t* value) const;

  zx_status_t ReadCapability(uint8_t addr, uint32_t* out) const;

  const pci_cap_t* FindCapability(uint8_t addr, uint8_t* cap_index,
                                  uint32_t* cap_base) const;

  mutable fbl::Mutex mutex_;

  // Static attributes for this device.
  const Attributes attrs_;
  // Command register.
  uint16_t command_ __TA_GUARDED(mutex_) = 0;
  // An IRQ was asserted while INT signalling is suppressed.
  bool pending_irq_ __TA_GUARDED(mutex_) = false;
  // Array of capabilities for this device.
  const pci_cap_t* capabilities_ = nullptr;
  // Size of |capabilities|.
  size_t num_capabilities_ = 0;
  // PCI bus this device is connected to.
  PciBus* bus_ = nullptr;
  // IRQ vector assigned by the bus.
  uint32_t global_irq_ = 0;
};

class PciPortHandler : public IoHandler {
 public:
  PciPortHandler(PciBus* bus);
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  PciBus* bus_;
};

class PciEcamHandler : public IoHandler {
 public:
  PciEcamHandler(PciBus* bus);
  zx_status_t Read(uint64_t addr, IoValue* value) const override;
  zx_status_t Write(uint64_t addr, const IoValue& value) override;

 private:
  PciBus* bus_;
};

class PciBus {
 public:
  PciBus(Guest* guest, InterruptController* interrupt_controller);

  zx_status_t Init();

  // Connect a PCI device to the bus.
  //
  // |slot| must be between 1 and kPciMaxDevices (slot 0 is reserved for the
  // root complex).
  //
  // This method is *not* thread-safe and must only be called during
  // initialization.
  zx_status_t Connect(PciDevice* device) __TA_NO_THREAD_SAFETY_ANALYSIS;

  // Access devices via the ECAM region.
  //
  // |addr| is the offset from the start of the ECAM region for this bus.
  zx_status_t ReadEcam(uint64_t addr, IoValue* value) const;
  zx_status_t WriteEcam(uint64_t addr, const IoValue& value);

  // Handle access to the PC IO ports (0xcf8 - 0xcff).
  zx_status_t ReadIoPort(uint64_t port, IoValue* value) const;
  zx_status_t WriteIoPort(uint64_t port, const IoValue& value);

  // Raise an interrupt for the given device.
  zx_status_t Interrupt(PciDevice& device);

  // Returns true if |bus|, |device|, |function| corresponds to a valid
  // device address.
  bool is_addr_valid(uint8_t bus, uint8_t device, uint8_t function) const {
    return bus == 0 && device < kPciMaxDevices && function == 0 &&
           device_[device];
  }

  // Current config address selected by the 0xcf8 IO port.
  uint32_t config_addr();
  void set_config_addr(uint32_t addr);

  PciDevice& root_complex() { return root_complex_; }

 private:
  mutable fbl::Mutex mutex_;

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
  PciDevice root_complex_;
  // Next mmio window to be allocated to connected devices.
  uint64_t mmio_base_ = kPciMmioBarPhysBase;
  // Pointer to the next open PCI slot.
  size_t next_open_slot_ = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_PCI_H_
