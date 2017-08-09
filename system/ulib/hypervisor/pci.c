// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/pci.h>

#include <stdio.h>
#include <string.h>

#include <hw/pci.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/decode.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/virtio_ids.h>

// PCI BAR register addresses.
#define PCI_REGISTER_BAR_0                      0x10
#define PCI_REGISTER_BAR_1                      0x14
#define PCI_REGISTER_BAR_2                      0x18
#define PCI_REGISTER_BAR_3                      0x1c
#define PCI_REGISTER_BAR_4                      0x20
#define PCI_REGISTER_BAR_5                      0x24

static const uint32_t kPioBase = 0x8000;
static const uint32_t kPioAddressMask = (uint32_t)~BIT_MASK(2);
static const uint32_t kMmioAddressMask = (uint32_t)~BIT_MASK(4);

typedef struct pci_device_attr {
    uint16_t device_id;
    uint16_t vendor_id;
    uint16_t subsystem_id;
    uint16_t subsystem_vendor_id;
    // Both class & subclass fields combined.
    uint16_t class_code;
    uint16_t bar_size;
} pci_device_attr_t;

static const pci_device_attr_t kDisabledDeviceAttributes = {
    .vendor_id = UINT16_MAX,
    .device_id = UINT16_MAX,
    .subsystem_vendor_id = UINT16_MAX,
    .subsystem_id = UINT16_MAX,
    .class_code = UINT16_MAX,
    .bar_size = UINT16_MAX,
};

static const pci_device_attr_t kDeviceAttributes[] = {
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

void pci_bus_init(pci_bus_t* bus) {
    memset(bus, 0, sizeof(*bus));
    for (uint8_t i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_device_t* device = &bus->device[i];
        device->command = PCI_COMMAND_IO_EN;
        device->bar[0] = kPioBase + (i << 8);
        device->attr = &kDeviceAttributes[i];
    }
}

static bool pci_addr_valid(uint8_t bus, uint8_t device, uint8_t function) {
    return bus == 0 && device < PCI_MAX_DEVICES && function == 0;
}

static void pci_addr_invalid_read(uint8_t len, uint32_t* value) {
    // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
    //
    // The host bus to PCI bridge must unambiguously report attempts to read the
    // Vendor ID of non-existent devices. Since 0 FFFFh is an invalid Vendor ID,
    // it is adequate for the host bus to PCI bridge to return a value of all
    // 1's on read accesses to Configuration Space registers of non-existent
    // devices.
    *value = (uint32_t) BIT_MASK(len * 8);
}

mx_status_t pci_bus_handler(pci_bus_t* bus, const mx_guest_memory_t* memory,
                            const instruction_t* inst) {
    const mx_vaddr_t addr = memory->addr;
    const uint8_t device = PCI_ECAM_DEVICE(addr);
    const uint16_t reg = PCI_ECAM_REGISTER(addr);
    const bool valid = pci_addr_valid(PCI_ECAM_BUS(addr), device, PCI_ECAM_FUNCTION(addr));

    uint32_t value = 0;
    mx_status_t status;
    switch (inst->type) {
    case INST_TEST:
        if (!valid) {
            pci_addr_invalid_read(inst->mem, &value);
        } else {
            status = pci_device_read(&bus->device[device], reg, inst->mem, &value);
            if (status != MX_OK)
                return status;
        }

        switch (inst->mem) {
        case 1:
            return inst_test8(inst, inst->imm, value);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    case INST_MOV_READ:
        if (!valid) {
            pci_addr_invalid_read(inst->mem, &value);
        } else {
            status = pci_device_read(&bus->device[device], reg, inst->mem, &value);
            if (status != MX_OK)
                return status;
        }

        switch (inst->mem) {
        case 1:
            return inst_read8(inst, value);
        case 2:
            return inst_read16(inst, value);
        case 4:
            return inst_read32(inst, value);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    case INST_MOV_WRITE:
        if (!valid)
            return MX_ERR_OUT_OF_RANGE;
        switch (inst->mem) {
        case 2:
            status = inst_write16(inst, (uint16_t*) &value);
            break;
        case 4:
            status = inst_write32(inst, &value);
            break;
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
        if (status != MX_OK)
            return status;
        return pci_device_write(&bus->device[device], reg, inst->mem, value);
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t pci_bus_read(const pci_bus_t* bus, uint16_t port, uint8_t access_size,
                         mx_vcpu_io_t* vcpu_io) {
    switch (port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        uint32_t bit_offset = (port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t addr = bus->config_addr >> bit_offset;
        uint32_t bit_mask = (uint32_t) BIT_MASK(access_size * 8);
        vcpu_io->access_size = access_size;
        vcpu_io->u32 = (vcpu_io->u32 & ~bit_mask) | (addr & bit_mask);
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        vcpu_io->access_size = access_size;
        uint32_t addr = bus->config_addr;
        if (!pci_addr_valid(PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr),
                            PCI_TYPE1_FUNCTION(addr))) {
            pci_addr_invalid_read(access_size, &vcpu_io->u32);
            return MX_OK;
        }

        const pci_device_t* device = &bus->device[PCI_TYPE1_DEVICE(addr)];
        uint16_t reg = PCI_TYPE1_REGISTER(addr) + port - PCI_CONFIG_DATA_PORT_BASE;
        return pci_device_read(device, reg, access_size, &vcpu_io->u32);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t pci_bus_write(pci_bus_t* bus, const mx_guest_io_t* io) {
    switch (io->port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        // Software can (and Linux does) perform partial word accesses to the
        // PCI address register. This means we need to take care to read/write
        // portions of the 32bit register without trampling the other bits.
        uint32_t bit_offset = (io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t bit_size = io->access_size * 8;
        uint32_t bit_mask = (uint32_t) BIT_MASK(bit_size);

        // Clear out the bits we'll be modifying.
        bus->config_addr = CLEAR_BITS(bus->config_addr, bit_size, bit_offset);
        // Set the bits of the address.
        bus->config_addr |= (io->u32 & bit_mask) << bit_offset;
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        uint32_t addr = bus->config_addr;
        if (!pci_addr_valid(PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr), PCI_TYPE1_FUNCTION(addr)))
            return MX_ERR_OUT_OF_RANGE;

        pci_device_t* device = &bus->device[PCI_TYPE1_DEVICE(addr)];
        uint16_t reg = PCI_TYPE1_REGISTER(addr) + io->port - PCI_CONFIG_DATA_PORT_BASE;
        return pci_device_write(device, reg, io->access_size, io->u32);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

/* Read a 4 byte aligned value from PCI config space. */
static mx_status_t pci_device_read_word(const pci_device_t* device, uint8_t reg, uint32_t* value) {
    const pci_device_attr_t* attr = device->attr;
    switch (reg) {
    //  ---------------------------------
    // |   (31..16)     |    (15..0)     |
    // |   device_id    |   vendor_id    |
    //  ---------------------------------
    case PCI_CONFIG_VENDOR_ID:
        *value = attr->vendor_id;
        *value |= attr->device_id << 16;
        return MX_OK;
    //  ----------------------------
    // |   (31..16)  |   (15..0)    |
    // |   status    |    command   |
    //  ----------------------------
    case PCI_CONFIG_COMMAND:
        *value = device->command;
        *value |= PCI_STATUS_INTERRUPT << 16;
        return MX_OK;
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case PCI_CONFIG_REVISION_ID:
        *value = attr->class_code << 16;
        return MX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case PCI_CONFIG_CACHE_LINE_SIZE:
        *value = PCI_HEADER_TYPE_STANDARD << 16;
        return MX_OK;
    case PCI_REGISTER_BAR_0: {
        *value = device->bar[0] | PCI_BAR_IO_TYPE_PIO;
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
        *value = attr->subsystem_vendor_id;
        *value |= attr->subsystem_id << 16;
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

    fprintf(stderr, "Unhandled PCI device read %#x\n", reg);
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t pci_device_read(const pci_device_t* device, uint16_t reg, uint8_t len, uint32_t* value) {
    // Perform 4-byte aligned read and then shift + mask the result to get the
    // expected value.
    uint32_t word = 0;
    const uint32_t reg_mask = BIT_MASK(2);
    uint8_t word_aligend_reg = reg & ~reg_mask;
    uint8_t bit_offset = (reg & reg_mask) * 8;
    mx_status_t status = pci_device_read_word(device, word_aligend_reg, &word);
    if (status != MX_OK)
        return status;

    word >>= bit_offset;
    word &= BIT_MASK(len * 8);
    *value = word;
    return MX_OK;
}

mx_status_t pci_device_write(pci_device_t* device, uint16_t reg, uint8_t len, uint32_t value) {
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
        device->command = value;
        return MX_OK;
    case PCI_REGISTER_BAR_0: {
        if (len != 4) {
            return MX_ERR_NOT_SUPPORTED;
        }
        uint32_t* bar = &device->bar[0];
        *bar = value;
        // We zero bits in the BAR in order to set the size.
        *bar &= ~(device->attr->bar_size - 1);
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
}

void pci_device_disable(pci_device_t* device) {
    device->attr = &kDisabledDeviceAttributes;
}

static bool pci_device_io_enabled(uint8_t io_type, uint16_t command) {
    switch (io_type) {
    case PCI_BAR_IO_TYPE_PIO:
        return command & PCI_COMMAND_IO_EN;
    case PCI_BAR_IO_TYPE_MMIO:
        return command & PCI_COMMAND_MEM_EN;
    }
    return false;
}

static uint32_t pci_bar_address(uint8_t io_type, uint32_t bar) {
    switch (io_type) {
    case PCI_BAR_IO_TYPE_PIO:
        return bar & kPioAddressMask;
    case PCI_BAR_IO_TYPE_MMIO:
        return bar & kMmioAddressMask;
    }
    return 0;
}

uint16_t pci_device_num(pci_bus_t* bus, uint8_t io_type, uint16_t addr, uint16_t* off) {
    for (uint8_t i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_device_t* device = &bus->device[i];

        // Check if the BAR is implemented and configured for the requested
        // io type.
        uint16_t bar0 = device->bar[0];
        if (!bar0 || (bar0 & PCI_BAR_IO_TYPE_MASK) != io_type)
            continue;

        // Ensure IO operations are enabled for this device.
        if (!pci_device_io_enabled(io_type, device->command))
            continue;

        uint16_t bar_base = pci_bar_address(io_type, bar0);
        uint16_t bar_size = device->attr->bar_size;
        if (addr >= bar_base && addr < bar_base + bar_size) {
            *off = addr - bar_base;
            return i;
        }
    }
    return PCI_DEVICE_INVALID;
}

uint16_t pci_bar_size(pci_device_t* device) {
    return device->attr->bar_size;
}
