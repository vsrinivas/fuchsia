// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <hypervisor/guest.h>
#include <hypervisor/io.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>

// clang-format off

#define PCI_DEVICE_ROOT_COMPLEX     0u
#define PCI_DEVICE_VIRTIO_BALLOON   1u
#define PCI_DEVICE_VIRTIO_BLOCK     2u
#define PCI_DEVICE_VIRTIO_GPU       3u
#define PCI_DEVICE_VIRTIO_INPUT     4u
#define PCI_DEVICE_INVALID          UINT16_MAX
#define PCI_MAX_DEVICES             5u
#define PCI_MAX_BARS                2u

// PCI configuration constants.
#define PCI_BAR_ASPACE_MASK         0x0001u
#define PCI_BAR_ASPACE_PIO          0x0001u
#define PCI_BAR_ASPACE_MMIO         0x0000u
#define PCI_VENDOR_ID_INTEL         0x8086u
#define PCI_DEVICE_ID_INTEL_Q35     0x29c0u
#define PCI_CLASS_BRIDGE_HOST       0x0600u
#define PCI_CLASS_MASS_STORAGE      0x0100u

// PCI type 1 address manipulation.
#define PCI_TYPE1_BUS(addr)         (((addr) >> 16) & 0xff)
#define PCI_TYPE1_DEVICE(addr)      (((addr) >> 11) & 0x1f)
#define PCI_TYPE1_FUNCTION(addr)    (((addr) >> 8) & 0x7)
#define PCI_TYPE1_REGISTER_MASK     0xfc
#define PCI_TYPE1_REGISTER(addr)    ((addr)&PCI_TYPE1_REGISTER_MASK)

// PCI ECAM address manipulation.
#define PCI_ECAM_BUS(addr)          (((addr) >> 20) & 0xff)
#define PCI_ECAM_DEVICE(addr)       (((addr) >> 15) & 0x1f)
#define PCI_ECAM_FUNCTION(addr)     (((addr) >> 12) & 0x7)
#define PCI_ECAM_REGISTER(addr)     ((addr)&0xfff)

// clang-format on

class IoApic;
class PciBus;
class PciDevice;

/* PCI capability structure.
 *
 * The 1-byte next pointer will be computed dynamically while traversing the
 * capabilities list.
 */
typedef struct pci_cap {
    // PCI capability ID as defined in PCI LOCAL BUS SPECIFICATION, REV. 3.0
    // Appendix H.
    uint8_t id;
    // Data for this capability. Must be at least |len| bytes. The first
    // two bytes will be ignored (id and next) as these will be populated
    // dynamically. They're skipped over in the data pointer to allow common
    // structures to be used for read/write where the id/next pointers are
    // embedded in the structure.
    uint8_t* data;
    // Size of |data|.
    uint8_t len;
} pci_cap_t;

struct PciBar : public IoHandler {
    // Register value.
    uint32_t addr;
    // Size of this BAR.
    uint32_t size;
    // The type of trap to create for this region.
    TrapType trap_type;

    // Pointer to the owning device.
    PciDevice* device;
    // Bar number.
    uint8_t n;

    // IoHandler interface.
    zx_status_t Read(uint64_t addr, IoValue* value) override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

    uint32_t aspace() const;
    uint32_t base() const;
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
    virtual zx_status_t ReadBar(uint8_t bar, uint64_t addr, IoValue* value) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Write to a region mapped by a BAR register.
    virtual zx_status_t WriteBar(uint8_t bar, uint64_t addr, const IoValue& value) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    // Handle accesses to this devics config space.
    zx_status_t ReadConfig(uint64_t reg, IoValue* value);
    zx_status_t WriteConfig(uint64_t reg, const IoValue& value);

    // Send the configured interrupt for this device.
    zx_status_t Interrupt() const;

    // Determines if the given base address register is implemented for this
    // device.
    bool is_bar_implemented(size_t bar) const {
        return bar < PCI_MAX_BARS && bar_[bar].size > 0;
    }

    // Returns a pointer to a base address register for this device.
    //
    // Returns nullptr if the register is not implmeneted.
    const PciBar* bar(size_t n) const { return is_bar_implemented(n) ? &bar_[n] : nullptr; }

    // Install a capability list.
    void set_capabilities(const pci_cap_t* caps, size_t num_caps) {
        capabilities_ = caps;
        num_capabilities_ = num_caps;
    }

protected:
    PciDevice(const Attributes attrs);

    // Base address registers.
    PciBar bar_[PCI_MAX_BARS] = {};

private:
    friend class PciBus;

    // Setup traps and handlers for accesses to BAR regions.
    zx_status_t SetupBarTraps(Guest* guest);

    zx_status_t ReadConfigWord(uint8_t reg, uint32_t* value);

    zx_status_t ReadCapability(uint8_t addr, uint32_t* out) const;

    const pci_cap_t* FindCapability(uint8_t addr, uint8_t* cap_index, uint32_t* cap_base) const;

    fbl::Mutex mutex_;

    // Static attributes for this device.
    const Attributes attrs_;
    // Command register.
    uint16_t command_ TA_GUARDED(mutex_) = 0;
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
    zx_status_t Read(uint64_t addr, IoValue* value) override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

private:
    PciBus* bus_;
};

class PciEcamHandler : public IoHandler {
public:
    PciEcamHandler(PciBus* bus);
    zx_status_t Read(uint64_t addr, IoValue* value) override;
    zx_status_t Write(uint64_t addr, const IoValue& value) override;

private:
    PciBus* bus_;
};

class PciBus {
public:
    // Base address in PIO space to map device BAR registers.
    static const uint32_t kPioBarBase = 0x8000;

    // Base address in MMIO space to map device BAR registers.
    static const uint32_t kMmioBarBase = 0xf0000000;

    PciBus(Guest* guest, const IoApic* io_apic);

    zx_status_t Init();

    // Connect a PCI device to the bus.
    //
    // |slot| must be between 1 and PCI_MAX_DEVICES (slot 0 is reserved for
    // the root complex).
    //
    // This method is *not* thread-safe and must only be called during
    // initialization.
    zx_status_t Connect(PciDevice* device, uint8_t slot) TA_NO_THREAD_SAFETY_ANALYSIS;

    // Access devices via the ECAM region.
    //
    // |addr| is the offset from the start of the ECAM region for this bus.
    zx_status_t ReadEcam(uint64_t addr, IoValue* value);
    zx_status_t WriteEcam(uint64_t addr, const IoValue& value);

    // Handle access to the PC IO ports (0xcf8 - 0xcff).
    zx_status_t ReadIoPort(uint64_t port, IoValue* value);
    zx_status_t WriteIoPort(uint64_t port, const IoValue& value);

    // Raise an interrupt for the given device.
    zx_status_t Interrupt(const PciDevice& device) const;

    // Returns true if |bus|, |device|, |function| corresponds to a valid
    // device address.
    bool is_addr_valid(uint8_t bus, uint8_t device, uint8_t function) const {
        return bus == 0 && device < PCI_MAX_DEVICES && function == 0 && device_[device];
    }

    // Current config address seleceted by the 0xcf8 IO port.
    uint32_t config_addr();
    void set_config_addr(uint32_t addr);

    PciDevice& root_complex() { return root_complex_; }

private:
    fbl::Mutex mutex_;

    Guest* guest_;
    PciEcamHandler ecam_handler_;
    PciPortHandler port_handler_;

    // Selected address in PCI config space.
    uint32_t config_addr_ TA_GUARDED(mutex_) = 0;

    // Devices on the virtual PCI bus.
    PciDevice* device_[PCI_MAX_DEVICES] = {};
    // IO APIC for use with interrupt redirects.
    const IoApic* io_apic_ = nullptr;
    // Embedded root complex device.
    PciDevice root_complex_;
    // Next pio window to be allocated to connected devices.
    uint32_t pio_base_ = kPioBarBase;
    // Next mmio window to be allocated to connected devices.
    uint32_t mmio_base_ = kMmioBarBase;
};
