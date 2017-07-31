// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/pci.h>

#include <hw/pci.h>
#include <hypervisor/bits.h>
#include <hypervisor/decode.h>
#include <stdio.h>
#include <virtio/virtio_ids.h>

// PCI BAR register addresses.
#define PCI_REGISTER_BAR_0                      0x10
#define PCI_REGISTER_BAR_1                      0x14
#define PCI_REGISTER_BAR_2                      0x18
#define PCI_REGISTER_BAR_3                      0x1c
#define PCI_REGISTER_BAR_4                      0x20
#define PCI_REGISTER_BAR_5                      0x24

typedef struct pci_attr {
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;
    // Both class & subclass fields combined.
    uint16_t class_code;
    uint16_t bar_size;
} pci_attr_t;

static const pci_attr_t kPciDeviceAttributes[] = {
    [PCI_DEVICE_ROOT_COMPLEX] = {
        .vendor_id = PCI_VENDOR_ID_INTEL,
        .device_id = PCI_DEVICE_ID_INTEL_Q35,
        .subsystem_vendor_id = 0,
        .subsystem_id = 0,
        .class_code = PCI_CLASS_BRIDGE_HOST,
        .bar_size = 0x10,
    },
    [PCI_DEVICE_VIRTIO_BLOCK] = {
        .vendor_id = PCI_VENDOR_ID_VIRTIO,
        .device_id = PCI_DEVICE_ID_VIRTIO_BLOCK_LEGACY,
        .subsystem_vendor_id = 0,
        .subsystem_id = VIRTIO_ID_BLOCK,
        .class_code = PCI_CLASS_MASS_STORAGE,
        .bar_size = 0x40,
    },
};

// TODO(abdulla): Introduce a syscall to associate a port range with a FIFO, so
// that we can directly communicate with the handler and remove this function.
uint16_t pci_device(pci_device_state_t* pci_device_states, uint16_t port,
                    uint16_t* port_off) {
    for (unsigned i = 0; i < PCI_MAX_DEVICES; i++) {
        uint16_t bar0 = pci_device_states[i].bar[0];
        if (!(bar0 & PCI_BAR_IO_TYPE_PIO))
            continue;
        uint16_t bar_base = bar0 & ~PCI_BAR_IO_TYPE_PIO;
        uint16_t bar_size = kPciDeviceAttributes[i].bar_size;
        if (port >= bar_base && port < bar_base + bar_size) {
            *port_off = port - bar_base;
            return i;
        }
    }
    return PCI_DEVICE_INVALID;
}

uint16_t pci_bar_size(uint8_t device) {
    return device < PCI_MAX_DEVICES ? kPciDeviceAttributes[device].bar_size : 0;
}

/* Read a 4 byte aligned value from PCI config space. */
static mx_status_t pci_config_read_word(pci_device_state_t* pci_device_state,
                                        uint8_t bus, uint8_t device,
                                        uint8_t func, uint8_t reg,
                                        uint32_t* value) {
    if (device >= PCI_MAX_DEVICES)
        return MX_ERR_OUT_OF_RANGE;

    const pci_attr_t* device_attributes = &kPciDeviceAttributes[device];
    switch (reg) {
    //  ---------------------------------
    // |   (31..16)     |    (15..0)     |
    // |   device_id    |   vendor_id    |
    //  ---------------------------------
    case PCI_CONFIG_VENDOR_ID:
        *value = device_attributes->vendor_id;
        *value |= device_attributes->device_id << 16;
        return MX_OK;
    //  ----------------------------
    // |   (31..16)  |   (15..0)    |
    // |   status    |    command   |
    //  ----------------------------
    case PCI_CONFIG_COMMAND:
        *value = pci_device_state->command;
        *value |= PCI_STATUS_INTERRUPT << 16;
        return MX_OK;
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case PCI_CONFIG_REVISION_ID:
        *value = device_attributes->class_code << 16;
        return MX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case PCI_CONFIG_CACHE_LINE_SIZE:
        *value = PCI_HEADER_TYPE_STANDARD << 16;
        return MX_OK;
    case PCI_REGISTER_BAR_0: {
        uint32_t* bar = &pci_device_state->bar[0];
        *value = (pci_device_state->command & PCI_COMMAND_IO_EN) ?
                               *bar | PCI_BAR_IO_TYPE_PIO : UINT32_MAX;
        return MX_OK;
    }
    //  -------------------------------------------------------------
    // |   (31..24)  |  (23..16)   |    (15..8)     |    (7..0)      |
    // | max_latency |  min_grant  | interrupt_pin  | interrupt_line |
    //  -------------------------------------------------------------
    case PCI_CONFIG_INTERRUPT_LINE: {
        const uint8_t interrupt_pin = 1;
        *value = interrupt_pin << 8;
        return MX_OK;
    }
    //  -------------------------------------------
    // |   (31..16)        |         (15..0)       |
    // |   subsystem_id    |  subsystem_vendor_id  |
    //  -------------------------------------------
    case PCI_CONFIG_SUBSYS_VENDOR_ID:
        *value = device_attributes->subsystem_vendor_id;
        *value |= device_attributes->subsystem_id << 16;
        return MX_OK;
    //  ------------------------------------------
    // |     (31..8)     |         (7..0)         |
    // |     Reserved    |  capabilities_pointer  |
    //  ------------------------------------------
    case PCI_CONFIG_CAPABILITIES:
    // These are all 32-bit registers.
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5:
    case PCI_CONFIG_CARDBUS_CIS_PTR:
    case PCI_CONFIG_EXP_ROM_ADDRESS:
        // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
        //
        // Read accesses to reserved or unimplemented registers must be
        // completed normally and a data value of 0 returned.
        *value = 0;
        return MX_OK;
    }
    fprintf(stderr, "Unhandled PCI device read %d %#x\n", device, reg);
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t pci_config_read(pci_device_state_t* pci_device_state,
                            uint8_t bus, uint8_t device, uint8_t func,
                            uint8_t reg, size_t len, uint32_t* value) {
    // Perform 4-byte aligned read and then shift + mask the result to get the
    // expected value.
    uint32_t word = 0;
    const uint32_t reg_mask = BIT_MASK(2);
    uint8_t word_aligend_reg = reg & ~reg_mask;
    uint8_t bit_offset = (reg & reg_mask) * 8;
    mx_status_t status = pci_config_read_word(pci_device_state, bus, device,
                                              func, word_aligend_reg, &word);
    if (status != MX_OK)
        return status;

    word >>= bit_offset;
    word &= BIT_MASK(len * 8);
    *value = word;
    return MX_OK;
}

mx_status_t pci_config_write(pci_device_state_t* pci_device_state,
                             uint8_t bus, uint8_t device,
                             uint8_t func, uint8_t reg,
                             size_t len, uint32_t value) {
    if (device >= PCI_MAX_DEVICES)
        return MX_ERR_OUT_OF_RANGE;

    switch (reg) {
    case PCI_CONFIG_VENDOR_ID:
    case PCI_CONFIG_DEVICE_ID:
    case PCI_CONFIG_REVISION_ID:
    case PCI_CONFIG_HEADER_TYPE:
    case PCI_CONFIG_CLASS_CODE:
    case PCI_CONFIG_CLASS_CODE_SUB:
    case PCI_CONFIG_CLASS_CODE_BASE:
        // Read-only registers.
        return MX_ERR_NOT_SUPPORTED;
    case PCI_CONFIG_COMMAND:
        if (len != 2) {
            return MX_ERR_NOT_SUPPORTED;
        }
        pci_device_state->command = value;
        return MX_OK;
    case PCI_REGISTER_BAR_0: {
        if (len != 4) {
            return MX_ERR_NOT_SUPPORTED;
        }
        uint32_t* bar = &pci_device_state->bar[0];
        *bar = value;
        // We zero bits in the BAR in order to set the size.
        *bar &= ~(kPciDeviceAttributes[device].bar_size - 1);
        *bar |= PCI_BAR_IO_TYPE_PIO;
        return MX_OK;
    }
    default:
        // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
        //
        // All PCI devices must treat Configuration Space write operations to
        // reserved registers as no-ops; that is, the access must be completed
        // normally on the bus and the data discarded.
        return MX_OK;
    }

    fprintf(stderr, "Unhandled PCI device write %d %#x 0x%x\n", device, reg, value);
    return MX_ERR_NOT_SUPPORTED;
}
