// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <sys/types.h>

#define PCI_DEVICE_ROOT_COMPLEX             0u
#define PCI_DEVICE_VIRTIO_BLOCK             1u
#define PCI_DEVICE_INVALID                  UINT16_MAX
#define PCI_MAX_DEVICES                     2u
#define PCI_MAX_BARS                        1u

// PCI configuration constants.
#define PCI_BAR_IO_TYPE_MASK                0x0001u
#define PCI_BAR_IO_TYPE_PIO                 0x0001u
#define PCI_BAR_IO_TYPE_MMIO                0x0000u
#define PCI_VENDOR_ID_VIRTIO                0x1af4u
#define PCI_VENDOR_ID_INTEL                 0x8086u
#define PCI_DEVICE_ID_VIRTIO_BLOCK_LEGACY   0x1001u
#define PCI_DEVICE_ID_INTEL_Q35             0x29c0u
#define PCI_CLASS_BRIDGE_HOST               0x0600u
#define PCI_CLASS_MASS_STORAGE              0x0100u

// PCI type 1 address manipulation.
#define PCI_TYPE1_BUS(addr)                 (((addr) >> 16) & 0xff)
#define PCI_TYPE1_DEVICE(addr)              (((addr) >> 11) & 0x1f)
#define PCI_TYPE1_FUNCTION(addr)            (((addr) >> 8) & 0x7)
#define PCI_TYPE1_REGISTER_MASK             0xfc
#define PCI_TYPE1_REGISTER(addr)            ((addr) & PCI_TYPE1_REGISTER_MASK)

// PCI ECAM address manipulation.
#define PCI_ECAM_BUS(addr)                  (((addr) >> 20) & 0xff)
#define PCI_ECAM_DEVICE(addr)               (((addr) >> 15) & 0x1f)
#define PCI_ECAM_FUNCTION(addr)             (((addr) >> 12) & 0x7)
#define PCI_ECAM_REGISTER(addr)             ((addr) & 0xfff)

typedef struct instruction instruction_t;
typedef struct mx_guest_io mx_guest_io_t;
typedef struct mx_guest_memory mx_guest_memory_t;
typedef struct mx_vcpu_io mx_vcpu_io_t;
typedef struct pci_device_attr pci_device_attr_t;

/* Stores the state of PCI devices. */
typedef struct pci_device {
    // Command register.
    uint16_t command;
    // Base address registers.
    uint32_t bar[PCI_MAX_BARS];
    // Device attributes.
    const pci_device_attr_t* attr;
} pci_device_t;

typedef struct pci_bus {
    // Selected address in PCI config space.
    uint32_t config_addr;
    // Devices on the virtual PCI bus.
    pci_device_t device[PCI_MAX_DEVICES];
} pci_bus_t;

void pci_bus_init(pci_bus_t* bus);

/* Handle MMIO access to the PCI config space. */
mx_status_t pci_bus_handler(pci_bus_t* bus, const mx_guest_memory_t* memory,
                            const instruction_t* inst);

/* Handle PIO reads to the PCI config space. */
mx_status_t pci_bus_read(const pci_bus_t* bus, uint16_t port, uint8_t access_size,
                         mx_vcpu_io_t* vcpu_io);

/* Handle PIO writes to the PCI config space. */
mx_status_t pci_bus_write(pci_bus_t* bus, const mx_guest_io_t* io);

mx_status_t pci_device_read(const pci_device_t* device, uint16_t reg, uint8_t len, uint32_t* value);
mx_status_t pci_device_write(pci_device_t* device, uint16_t reg, uint8_t len, uint32_t value);

/* Disable the given PCI device. By default, all devices on the bus are enabled
 * by pci_bus_init.
 */
void pci_device_disable(pci_device_t* device);

/* Return the device number for the PCI device that has a BAR mapped to the
 * given address with the specified IO type. Returns PCI_DEVICE_INVALID if no
 * mapping exists or IO is disabled for the mapping.
 */
uint16_t pci_device_num(pci_bus_t* bus, uint8_t io_type, uint16_t addr, uint16_t* off);

/* Returns the bar size for the device. The device is the same value used to
 * index the device in PCI config space.
 */
uint16_t pci_bar_size(pci_device_t* device);
