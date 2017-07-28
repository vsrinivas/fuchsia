// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <sys/types.h>

#define PCI_DEVICE_ROOT_COMPLEX                 0u
#define PCI_DEVICE_VIRTIO_BLOCK                 1u
#define PCI_DEVICE_INVALID                      UINT16_MAX
#define PCI_MAX_DEVICES                         2u
#define PCI_MAX_BARS                            1u

// PCI configuration constants.
#define PCI_BAR_IO_TYPE_MASK                    0x0001
#define PCI_BAR_IO_TYPE_PIO                     0x0001
#define PCI_VENDOR_ID_VIRTIO                    0x1af4
#define PCI_VENDOR_ID_INTEL                     0x8086
#define PCI_DEVICE_ID_VIRTIO_BLOCK_LEGACY       0x1001
#define PCI_DEVICE_ID_INTEL_Q35                 0x29c0
#define PCI_CLASS_BRIDGE_HOST                   0x0600
#define PCI_CLASS_MASS_STORAGE                  0x0100

// PCI type 1 address manipulation.
#define PCI_TYPE1_BUS(addr)                     (((addr) >> 16) & 0xff)
#define PCI_TYPE1_DEVICE(addr)                  (((addr) >> 11) & 0x1f)
#define PCI_TYPE1_FUNCTION(addr)                (((addr) >> 8) & 0x7)
#define PCI_TYPE1_REGISTER_MASK                 0xfc
#define PCI_TYPE1_REGISTER(addr)                ((addr) & PCI_TYPE1_REGISTER_MASK)

#define PCI_TYPE1_ADDR(bus, device, function, reg) \
    (0x80000000 | ((bus) << 16) | ((device) << 11) | ((function) << 8) \
     | ((reg) & PCI_TYPE1_REGISTER_MASK))

/* Stores the state of PCI devices across VM exists. */
typedef struct pci_device_state {
    // Command register.
    uint16_t command;
    // Base address registers.
    uint32_t bar[PCI_MAX_BARS];
} pci_device_state_t;


/* Read a value from PCI config space. */
mx_status_t pci_config_read(pci_device_state_t* pci_device_state,
                            uint8_t bus, uint8_t device,
                            uint8_t func, uint8_t reg,
                            size_t len, uint32_t* value);

/* Write a value to PCI config space. */
mx_status_t pci_config_write(pci_device_state_t* pci_device_state,
                             uint8_t bus, uint8_t device,
                             uint8_t func, uint8_t reg,
                             size_t len, uint32_t value);

/* Return the device number for the PCI device that has a BAR mapped to the
 * given IO port. Returns PCI_DEVICE_INVALID if no mapping exists.
 */
uint16_t pci_device(pci_device_state_t* pci_device_states, uint16_t port,
                    uint16_t* port_off);

/* Returns the bar size for the device. The device is the same value used to
 * index the device in PCI config space.
 */
uint16_t pci_bar_size(uint8_t device);
