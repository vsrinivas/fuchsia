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
#include <hypervisor/io_apic.h>
#include <hypervisor/vcpu.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/virtio_ids.h>

// PCI BAR register addresses.
#define PCI_REGISTER_BAR_0 0x10
#define PCI_REGISTER_BAR_1 0x14
#define PCI_REGISTER_BAR_2 0x18
#define PCI_REGISTER_BAR_3 0x1c
#define PCI_REGISTER_BAR_4 0x20
#define PCI_REGISTER_BAR_5 0x24

// PCI Capabilities registers.
#define PCI_REGISTER_CAP_BASE 0xa4
#define PCI_REGISTER_CAP_TOP UINT8_MAX

static const uint32_t kPioBase = 0x8000;
static const uint32_t kMaxBarSize = 1 << 8;
static const uint32_t kPioAddressMask = ~bit_mask<uint32_t>(2);
static const uint32_t kMmioAddressMask = ~bit_mask<uint32_t>(4);

// PCI capabilities register layout.
static const uint8_t kPciCapTypeOffset = 0;
static const uint8_t kPciCapNextOffset = 1;

/* Per-device IRQ assignments.
 *
 * These are provided to the guest via the _SB section in the DSDT ACPI table.
 */
static const uint32_t kPciGlobalIrqAssigments[PCI_MAX_DEVICES] = {32, 33, 34};

static mx_status_t pci_bar_read_unsupported(const pci_device_t* device, uint8_t bar, uint16_t port,
                                            uint8_t access_size, mx_vcpu_io_t* vcpu_io) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t pci_bar_write_unsupported(pci_device_t* device, uint8_t bar, uint16_t port,
                                             const mx_vcpu_io_t* io) {
    return MX_ERR_NOT_SUPPORTED;
}

static inline bool pci_bar_implemented(pci_device_t* device, uint8_t bar) {
    return bar < PCI_MAX_BARS && device->bar[bar].size > 0;
}

static pci_device_ops_t kRootComplexDeviceOps = {
    .read_bar = &pci_bar_read_unsupported,
    .write_bar = &pci_bar_write_unsupported,
};

static void pci_root_complex_init(pci_device_t* host_bridge) {
    host_bridge->vendor_id = PCI_VENDOR_ID_INTEL;
    host_bridge->device_id = PCI_DEVICE_ID_INTEL_Q35;
    host_bridge->subsystem_vendor_id = 0;
    host_bridge->subsystem_id = 0;
    host_bridge->class_code = PCI_CLASS_BRIDGE_HOST;
    host_bridge->bar[0].size = 0x10;
    host_bridge->ops = &kRootComplexDeviceOps;
}

mx_status_t pci_bus_init(pci_bus_t* bus, const io_apic_t* io_apic) {
    memset(bus, 0, sizeof(*bus));
    bus->io_apic = io_apic;
    bus->pio_base = kPioBase;
    pci_root_complex_init(&bus->root_complex);
    return pci_bus_connect(bus, &bus->root_complex, PCI_DEVICE_ROOT_COMPLEX);
}

mx_status_t pci_bus_connect(pci_bus_t* bus, pci_device_t* device, uint8_t slot) {
    if (slot >= PCI_MAX_DEVICES)
        return MX_ERR_OUT_OF_RANGE;
    if (bus->device[slot])
        return MX_ERR_ALREADY_EXISTS;

    // Initialize BAR registers.
    for (uint8_t bar_num = 0; bar_num < PCI_MAX_BARS; ++bar_num) {
        // Skip unimplemented bars.
        if (!pci_bar_implemented(device, bar_num))
            continue;

        // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.2.5.1
        //
        // This design implies that all address spaces used are a power of two in
        // size and are naturally aligned.
        uint32_t bar_size = round_up_pow2(device->bar[bar_num].size);
        if (bar_size > kMaxBarSize)
            return MX_ERR_NOT_SUPPORTED;

        device->bus = bus;
        device->bar[bar_num].size = static_cast<uint16_t>(bar_size);
        device->bar[bar_num].addr = bus->pio_base;
        device->bar[bar_num].io_type = PCI_BAR_IO_TYPE_PIO;
        bus->pio_base += kMaxBarSize;
    }
    device->command = PCI_COMMAND_IO_EN;
    device->global_irq = kPciGlobalIrqAssigments[slot];
    bus->device[slot] = device;
    return MX_OK;
}

static bool pci_addr_valid(const pci_bus_t* b, uint8_t bus, uint8_t device, uint8_t function) {
    return bus == 0 && device < PCI_MAX_DEVICES && function == 0 && b->device[device];
}

static void pci_addr_invalid_read(uint8_t len, uint32_t* value) {
    // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
    //
    // The host bus to PCI bridge must unambiguously report attempts to read the
    // Vendor ID of non-existent devices. Since 0 FFFFh is an invalid Vendor ID,
    // it is adequate for the host bus to PCI bridge to return a value of all
    // 1's on read accesses to Configuration Space registers of non-existent
    // devices.
    *value = bit_mask<uint32_t>(len * 8);
}

mx_status_t pci_ecam_read(pci_bus_t* bus, mx_vaddr_t addr, uint8_t access_size, mx_vcpu_io_t* io) {
    const uint8_t device = PCI_ECAM_DEVICE(addr);
    const uint16_t reg = PCI_ECAM_REGISTER(addr);
    const bool valid = pci_addr_valid(bus, PCI_ECAM_BUS(addr), device, PCI_ECAM_FUNCTION(addr));
    if (!valid) {
        pci_addr_invalid_read(access_size, &io->u32);
        return MX_OK;
    }

    return pci_device_read(bus->device[device], reg, access_size, &io->u32);
}

mx_status_t pci_ecam_write(pci_bus_t* bus, mx_vaddr_t addr, const mx_vcpu_io_t* io) {
    const uint8_t device = PCI_ECAM_DEVICE(addr);
    const uint16_t reg = PCI_ECAM_REGISTER(addr);
    const bool valid = pci_addr_valid(bus, PCI_ECAM_BUS(addr), device, PCI_ECAM_FUNCTION(addr));
    if (!valid) {
        return MX_ERR_OUT_OF_RANGE;
    }

    return pci_device_write(bus->device[device], reg, io->access_size, io->u32);
}

mx_status_t pci_bus_read(const pci_bus_t* bus, uint16_t port, uint8_t access_size,
                         mx_vcpu_io_t* vcpu_io) {
    switch (port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE ... PCI_CONFIG_ADDRESS_PORT_TOP: {
        uint32_t bit_offset = (port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t mask = bit_mask<uint32_t>(access_size * 8);
        mtx_lock((mtx_t*)&bus->mutex);
        uint32_t addr = bus->config_addr >> bit_offset;
        mtx_unlock((mtx_t*)&bus->mutex);
        vcpu_io->access_size = access_size;
        vcpu_io->u32 = (vcpu_io->u32 & ~mask) | (addr & mask);
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE ... PCI_CONFIG_DATA_PORT_TOP: {
        mtx_lock((mtx_t*)&bus->mutex);
        uint32_t addr = bus->config_addr;
        mtx_unlock((mtx_t*)&bus->mutex);
        vcpu_io->access_size = access_size;
        if (!pci_addr_valid(bus, PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr),
                            PCI_TYPE1_FUNCTION(addr))) {
            pci_addr_invalid_read(access_size, &vcpu_io->u32);
            return MX_OK;
        }

        const pci_device_t* device = bus->device[PCI_TYPE1_DEVICE(addr)];
        uint32_t reg = PCI_TYPE1_REGISTER(addr) + port - PCI_CONFIG_DATA_PORT_BASE;
        return pci_device_read(device, static_cast<uint16_t>(reg), access_size, &vcpu_io->u32);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t pci_bus_write(pci_bus_t* bus, const mx_packet_guest_io_t* io) {
    switch (io->port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE ... PCI_CONFIG_ADDRESS_PORT_TOP: {
        // Software can (and Linux does) perform partial word accesses to the
        // PCI address register. This means we need to take care to read/write
        // portions of the 32bit register without trampling the other bits.
        uint32_t bit_offset = (io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t bit_size = io->access_size * 8;
        uint32_t mask = bit_mask<uint32_t>(bit_size);

        mtx_lock(&bus->mutex);
        // Clear out the bits we'll be modifying.
        bus->config_addr = clear_bits(bus->config_addr, bit_size, bit_offset);
        // Set the bits of the address.
        bus->config_addr |= (io->u32 & mask) << bit_offset;
        mtx_unlock(&bus->mutex);
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE ... PCI_CONFIG_DATA_PORT_TOP: {
        mtx_lock(&bus->mutex);
        uint32_t addr = bus->config_addr;
        mtx_unlock(&bus->mutex);
        if (!pci_addr_valid(bus, PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr), PCI_TYPE1_FUNCTION(addr)))
            return MX_ERR_OUT_OF_RANGE;

        pci_device_t* device = bus->device[PCI_TYPE1_DEVICE(addr)];
        uint32_t reg = PCI_TYPE1_REGISTER(addr) + io->port - PCI_CONFIG_DATA_PORT_BASE;
        return pci_device_write(device, static_cast<uint16_t>(reg), io->access_size, io->u32);
    }
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}

// PCI Local Bus Spec v3.0 Section 6.7: Each capability must be DWORD aligned.
static inline uint8_t pci_cap_len(const pci_cap_t* cap) {
    return align(cap->len, 4);
}

static const pci_cap_t* pci_find_cap(const pci_device_t* device, uint8_t addr, uint8_t* cap_index,
                                     uint32_t* cap_base) {
    uint32_t base = PCI_REGISTER_CAP_BASE;
    for (uint8_t i = 0; i < device->num_capabilities; ++i) {
        const pci_cap_t* cap = &device->capabilities[i];
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

static mx_status_t pci_read_cap(const pci_device_t* device, uint8_t addr, uint32_t* out) {
    uint8_t cap_index;
    uint32_t cap_base;
    const pci_cap_t* cap = pci_find_cap(device, addr, &cap_index, &cap_base);
    if (cap == nullptr)
        return MX_ERR_NOT_FOUND;

    uint32_t word = 0;
    uint32_t cap_offset = addr - cap_base;
    for (uint8_t byte = 0; byte < sizeof(word); ++byte, ++cap_offset) {

        // In the case of padding bytes, return 0.
        if (cap_offset >= cap->len)
            break;

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
            if (cap_index + 1u < device->num_capabilities)
                val = cap_base + pci_cap_len(cap);
            break;
        default:
            val = cap->data[cap_offset];
            break;
        }
        word |= val << (byte * 8);
    }

    *out = word;
    return MX_OK;
}

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
//
// Read accesses to reserved or unimplemented registers must be completed
// normally and a data value of 0 returned.
static mx_status_t pci_device_read_unimplemented(uint32_t* value) {
    *value = 0;
    return MX_OK;
}

/* Read a 4 byte aligned value from PCI config space. */
static mx_status_t pci_device_read_word(const pci_device_t* device, uint8_t reg, uint32_t* value) {
    switch (reg) {
    //  ---------------------------------
    // |   (31..16)     |    (15..0)     |
    // |   device_id    |   vendor_id    |
    //  ---------------------------------
    case PCI_CONFIG_VENDOR_ID:
        *value = device->vendor_id;
        *value |= device->device_id << 16;
        return MX_OK;
    //  ----------------------------
    // |   (31..16)  |   (15..0)    |
    // |   status    |    command   |
    //  ----------------------------
    case PCI_CONFIG_COMMAND: {
        mtx_lock((mtx_t*)&device->mutex);
        *value = device->command;
        mtx_unlock((mtx_t*)&device->mutex);

        uint16_t status = PCI_STATUS_INTERRUPT;
        if (device->capabilities != nullptr)
            status |= PCI_STATUS_NEW_CAPS;
        *value |= status << 16;
        return MX_OK;
    }
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case PCI_CONFIG_REVISION_ID:
        *value = device->class_code << 16 | device->revision_id;
        return MX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case PCI_CONFIG_CACHE_LINE_SIZE:
        *value = PCI_HEADER_TYPE_STANDARD << 16;
        return MX_OK;
    case PCI_REGISTER_BAR_0:
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5: {
        uint32_t bar_num = (reg - PCI_REGISTER_BAR_0) / 4;
        if (bar_num >= PCI_MAX_BARS)
            return pci_device_read_unimplemented(value);

        mtx_lock((mtx_t*)&device->mutex);
        const pci_bar_t* bar = &device->bar[bar_num];
        *value = bar->addr | bar->io_type;
        mtx_unlock((mtx_t*)&device->mutex);
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
        *value = device->subsystem_vendor_id;
        *value |= device->subsystem_id << 16;
        return MX_OK;
    //  ------------------------------------------
    // |     (31..8)     |         (7..0)         |
    // |     Reserved    |  capabilities_pointer  |
    //  ------------------------------------------
    case PCI_CONFIG_CAPABILITIES:
        *value = 0;
        if (device->capabilities != nullptr)
            *value |= PCI_REGISTER_CAP_BASE;
        return MX_OK;
    case PCI_REGISTER_CAP_BASE... PCI_REGISTER_CAP_TOP:
        if (pci_read_cap(device, reg, value) != MX_ERR_NOT_FOUND)
            return MX_OK;
        // Fall-through if the capability is not-implemented.
    // These are all 32-bit registers.
    case PCI_CONFIG_CARDBUS_CIS_PTR:
    case PCI_CONFIG_EXP_ROM_ADDRESS:
        return pci_device_read_unimplemented(value);
    }

    fprintf(stderr, "Unhandled PCI device read %#x\n", reg);
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t pci_device_read(const pci_device_t* device, uint16_t reg, uint8_t len, uint32_t* value) {
    // Perform 4-byte aligned read and then shift + mask the result to get the
    // expected value.
    uint32_t word = 0;
    const uint8_t reg_mask = bit_mask<uint8_t>(2);
    uint8_t word_aligend_reg = static_cast<uint8_t>(reg & ~reg_mask);
    uint8_t bit_offset = static_cast<uint8_t>((reg & reg_mask) * 8);
    mx_status_t status = pci_device_read_word(device, word_aligend_reg, &word);
    if (status != MX_OK)
        return status;

    word >>= bit_offset;
    word &= bit_mask<uint32_t>(len * 8);
    *value = word;
    return MX_OK;
}

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
//
// All PCI devices must treat Configuration Space write operations to reserved
// registers as no-ops; that is, the access must be completed  normally on the
// bus and the data discarded.
static inline mx_status_t pci_device_write_unimplemented() {
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
        if (len != 2)
            return MX_ERR_NOT_SUPPORTED;
        mtx_lock(&device->mutex);
        device->command = static_cast<uint16_t>(value);
        mtx_unlock(&device->mutex);
        return MX_OK;
    case PCI_REGISTER_BAR_0:
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5: {
        if (len != 4)
            return MX_ERR_NOT_SUPPORTED;

        uint32_t bar_num = (reg - PCI_REGISTER_BAR_0) / 4;
        if (bar_num >= PCI_MAX_BARS)
            return pci_device_write_unimplemented();

        mtx_lock(&device->mutex);
        pci_bar_t* bar = &device->bar[bar_num];
        bar->addr = value;
        // We zero bits in the BAR in order to set the size.
        bar->addr &= ~(bar->size - 1);
        mtx_unlock(&device->mutex);
        return MX_OK;
    }
    default:
        return pci_device_write_unimplemented();
    }
}

static bool pci_device_io_enabled(uint8_t io_type, uint16_t command) {
    switch (io_type) {
    case PCI_BAR_IO_TYPE_PIO:
        return command & PCI_COMMAND_IO_EN;
    case PCI_BAR_IO_TYPE_MMIO:
        return command & PCI_COMMAND_MEM_EN;
    default:
        return false;
    }
}

pci_device_t* pci_mapped_device(pci_bus_t* bus, uint8_t io_type, uint16_t addr, uint8_t* bar_out,
                                uint16_t* off) {
    for (uint8_t i = 0; i < PCI_MAX_DEVICES; i++) {
        pci_device_t* device = bus->device[i];
        if (!device)
            continue;

        for (uint8_t bar_num = 0; bar_num < PCI_MAX_BARS; ++bar_num) {
            mtx_lock(&device->mutex);
            uint16_t command = device->command;
            pci_bar_t* bar = &device->bar[bar_num];
            uint32_t bar_base = pci_bar_base(bar);
            uint16_t bar_size = pci_bar_size(bar);
            mtx_unlock(&device->mutex);

            // Ensure IO operations are enabled for this device.
            if (!pci_device_io_enabled(io_type, command))
                continue;

            // Check if the BAR is implemented and configured for the requested
            // IO type.
            if (!bar_size || bar->io_type != io_type)
                continue;

            if (addr >= bar_base && addr < bar_base + bar_size) {
                *bar_out = bar_num;
                *off = static_cast<uint16_t>(addr - bar_base);
                return bus->device[i];
            }
        }
    }
    return NULL;
}

uint32_t pci_bar_base(pci_bar_t* bar) {
    switch (bar->io_type) {
    case PCI_BAR_IO_TYPE_PIO:
        return bar->addr & kPioAddressMask;
    case PCI_BAR_IO_TYPE_MMIO:
        return bar->addr & kMmioAddressMask;
    default:
        return 0;
    }
}

uint16_t pci_bar_size(pci_bar_t* bar) {
    return static_cast<uint16_t>(bar->size);
}

static mx_status_t pci_handler(mx_port_packet_t* packet, void* ctx) {
    pci_device_t* pci_device = static_cast<pci_device_t*>(ctx);

    // We provide the bar number as the trap key.
    if (packet->key > UINT8_MAX)
        return MX_ERR_OUT_OF_RANGE;
    uint8_t bar = static_cast<uint8_t>(packet->key);

    mx_vcpu_io_t io;
    io.access_size = packet->guest_io.access_size;
    io.u32 = packet->guest_io.u32;
    uint32_t bar_base = pci_bar_base(&pci_device->bar[bar]);
    uint16_t device_port = static_cast<uint16_t>(packet->guest_io.port - bar_base);
    return pci_device->ops->write_bar(pci_device, bar, device_port, &io);
}

mx_status_t pci_device_async(pci_device_t* device, mx_handle_t guest) {
    size_t num_traps = 0;
    trap_args_t traps[PCI_MAX_BARS];
    for (uint8_t i = 0; i < PCI_MAX_BARS; ++i) {
        pci_bar_t* bar = &device->bar[i];
        if (!pci_bar_implemented(device, i))
            continue;

        trap_args_t* trap = &traps[num_traps++];
        trap->key = i;
        trap->addr = pci_bar_base(bar);
        trap->len = pci_bar_size(bar);
        trap->kind = bar->io_type == PCI_BAR_IO_TYPE_PIO ? MX_GUEST_TRAP_IO : MX_GUEST_TRAP_MEM;
    }
    return device_async(guest, traps, num_traps, pci_handler, device);
}

mx_status_t pci_interrupt(pci_device_t* pci_device) {
    pci_bus_t* bus = pci_device->bus;
    if (!bus)
        return MX_ERR_BAD_STATE;

    return io_apic_interrupt(bus->io_apic, pci_device->global_irq);
}
