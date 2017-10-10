// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/pci.h>

#include <stdio.h>
#include <string.h>

#include <fbl/auto_lock.h>
#include <hw/pci.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/decode.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/vcpu.h>
#include <virtio/virtio_ids.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

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

static const uint32_t kPioAddressMask = ~bit_mask<uint32_t>(2);
static const uint32_t kMmioAddressMask = ~bit_mask<uint32_t>(4);

// PCI capabilities register layout.
static const uint8_t kPciCapTypeOffset = 0;
static const uint8_t kPciCapNextOffset = 1;

/* Per-device IRQ assignments.
 *
 * These are provided to the guest via the _SB section in the DSDT ACPI table.
 *
 * The DSDT defines interrupts for 5 devices (IRQ 32-36). Adding
 * additional devices beyond that will require updates to the DSDT.
 */
static const uint32_t kPciGlobalIrqAssigments[PCI_MAX_DEVICES] = {32, 33, 34, 35, 36};

static uint32_t pci_bar_base(PciBar* bar) {
    switch (bar->aspace) {
    case PCI_BAR_ASPACE_PIO:
        return bar->addr & kPioAddressMask;
    case PCI_BAR_ASPACE_MMIO:
        return bar->addr & kMmioAddressMask;
    default:
        return 0;
    }
}

static uint16_t pci_bar_size(PciBar* bar) {
    return static_cast<uint16_t>(bar->size);
}

static const PciDevice::Attributes kRootComplexAttributes = {
    .device_id = PCI_DEVICE_ID_INTEL_Q35,
    .vendor_id = PCI_VENDOR_ID_INTEL,
    .subsystem_id = 0,
    .subsystem_vendor_id = 0,
    .class_code = PCI_CLASS_BRIDGE_HOST,
    .revision_id = 0,
};

PciDevice::PciDevice(const Attributes attrs)
    : attrs_(attrs) {}

PciBus::PciBus(zx_handle_t guest, const IoApic* io_apic)
    : guest_(guest), io_apic_(io_apic), root_complex_(kRootComplexAttributes) {}

zx_status_t PciBus::Init() {
    root_complex_.bar_[0].size = 0x10;
    root_complex_.bar_[0].aspace = PCI_BAR_ASPACE_PIO;
    root_complex_.bar_[0].memory_type = PciMemoryType::STRONG;
    zx_status_t status = Connect(&root_complex_, PCI_DEVICE_ROOT_COMPLEX);
    if (status != ZX_OK)
        return status;
    status = zx_guest_set_trap(guest_, ZX_GUEST_TRAP_MEM, PCI_ECAM_PHYS_BASE,
                               PCI_ECAM_PHYS_TOP - PCI_ECAM_PHYS_BASE + 1, ZX_HANDLE_INVALID, 0);
    if (status != ZX_OK)
        return status;
    status = zx_guest_set_trap(guest_, ZX_GUEST_TRAP_IO, PCI_CONFIG_ADDRESS_PORT_BASE,
                               PCI_CONFIG_ADDRESS_PORT_TOP - PCI_CONFIG_ADDRESS_PORT_BASE + 1,
                               ZX_HANDLE_INVALID, 0);
    if (status != ZX_OK)
        return status;
    return zx_guest_set_trap(guest_, ZX_GUEST_TRAP_IO, PCI_CONFIG_DATA_PORT_BASE,
                             PCI_CONFIG_DATA_PORT_TOP - PCI_CONFIG_DATA_PORT_BASE + 1,
                             ZX_HANDLE_INVALID, 0);
}

uint32_t PciBus::config_addr() {
    fbl::AutoLock lock(&mutex_);
    return config_addr_;
}

void PciBus::set_config_addr(uint32_t addr) {
    fbl::AutoLock lock(&mutex_);
    config_addr_ = addr;
}

zx_status_t PciBus::Connect(PciDevice* device, uint8_t slot) {
    if (slot >= PCI_MAX_DEVICES)
        return ZX_ERR_OUT_OF_RANGE;
    if (device_[slot])
        return ZX_ERR_ALREADY_EXISTS;

    // Initialize BAR registers.
    for (uint8_t bar_num = 0; bar_num < PCI_MAX_BARS; ++bar_num) {
        PciBar* bar = &device->bar_[bar_num];

        // Skip unimplemented bars.
        if (!device->is_bar_implemented(bar_num))
            break;

        device->bus_ = this;
        if (device->bar_[bar_num].aspace == PCI_BAR_ASPACE_PIO) {
            // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.2.5.1
            //
            // This design implies that all address spaces used are a power of two in
            // size and are naturally aligned.
            bar->size = round_up_pow2(bar->size);
            bar->addr = align(pio_base_, bar->size);
            pio_base_ = bar->addr + bar->size;
        } else {
            bar->size = static_cast<uint16_t>(align(bar->size, PAGE_SIZE));
            bar->addr = mmio_base_;
            mmio_base_ += bar->size;
        }
    }

    device->command_ = PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN;
    device->global_irq_ = kPciGlobalIrqAssigments[slot];
    device_[slot] = device;

    return device->SetupBarTraps(guest_);
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

zx_status_t PciBus::ReadEcam(zx_vaddr_t addr, uint8_t access_size, zx_vcpu_io_t* io) {
    const uint8_t device = PCI_ECAM_DEVICE(addr);
    const uint16_t reg = PCI_ECAM_REGISTER(addr);
    const bool valid = is_addr_valid(PCI_ECAM_BUS(addr), device, PCI_ECAM_FUNCTION(addr));
    if (!valid) {
        pci_addr_invalid_read(access_size, &io->u32);
        return ZX_OK;
    }

    return device_[device]->ReadConfig(reg, access_size, &io->u32);
}

zx_status_t PciBus::WriteEcam(zx_vaddr_t addr, const zx_vcpu_io_t* io) {
    const uint8_t device = PCI_ECAM_DEVICE(addr);
    const uint16_t reg = PCI_ECAM_REGISTER(addr);
    const bool valid = is_addr_valid(PCI_ECAM_BUS(addr), device, PCI_ECAM_FUNCTION(addr));
    if (!valid) {
        return ZX_ERR_OUT_OF_RANGE;
    }

    return device_[device]->WriteConfig(reg, io->access_size, io->u32);
}

zx_status_t PciBus::ReadIoPort(uint16_t port, uint8_t access_size, zx_vcpu_io_t* vcpu_io) {
    switch (port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        uint32_t bit_offset = (port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t mask = bit_mask<uint32_t>(access_size * 8);

        fbl::AutoLock lock(&mutex_);
        uint32_t addr = config_addr_ >> bit_offset;
        vcpu_io->access_size = access_size;
        vcpu_io->u32 = (vcpu_io->u32 & ~mask) | (addr & mask);
        return ZX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        uint32_t addr;
        uint32_t reg;
        {
            fbl::AutoLock lock(&mutex_);
            addr = config_addr_;
            vcpu_io->access_size = access_size;
            if (!is_addr_valid(PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr),
                               PCI_TYPE1_FUNCTION(addr))) {
                pci_addr_invalid_read(access_size, &vcpu_io->u32);
                return ZX_OK;
            }
        }

        PciDevice* device = device_[PCI_TYPE1_DEVICE(addr)];
        reg = PCI_TYPE1_REGISTER(addr) + port - PCI_CONFIG_DATA_PORT_BASE;
        return device->ReadConfig(static_cast<uint16_t>(reg), access_size, &vcpu_io->u32);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t PciBus::WriteIoPort(const zx_packet_guest_io_t* io) {
    switch (io->port) {
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        // Software can (and Linux does) perform partial word accesses to the
        // PCI address register. This means we need to take care to read/write
        // portions of the 32bit register without trampling the other bits.
        uint32_t bit_offset = (io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8;
        uint32_t bit_size = io->access_size * 8;
        uint32_t mask = bit_mask<uint32_t>(bit_size);

        fbl::AutoLock lock(&mutex_);
        // Clear out the bits we'll be modifying.
        config_addr_ = clear_bits(config_addr_, bit_size, bit_offset);
        // Set the bits of the address.
        config_addr_ |= (io->u32 & mask) << bit_offset;
        return ZX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        uint32_t addr;
        uint32_t reg;
        {
            fbl::AutoLock lock(&mutex_);
            addr = config_addr_;

            if (!is_addr_valid(PCI_TYPE1_BUS(addr), PCI_TYPE1_DEVICE(addr), PCI_TYPE1_FUNCTION(addr)))
                return ZX_ERR_OUT_OF_RANGE;

            reg = PCI_TYPE1_REGISTER(addr) + io->port - PCI_CONFIG_DATA_PORT_BASE;
        }
        PciDevice* device = device_[PCI_TYPE1_DEVICE(addr)];
        return device->WriteConfig(static_cast<uint16_t>(reg), io->access_size, io->u32);
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

// PCI Local Bus Spec v3.0 Section 6.7: Each capability must be DWORD aligned.
static inline uint8_t pci_cap_len(const pci_cap_t* cap) {
    return align(cap->len, 4);
}

const pci_cap_t* PciDevice::FindCapability(uint8_t addr, uint8_t* cap_index,
                                           uint32_t* cap_base) const {
    uint32_t base = PCI_REGISTER_CAP_BASE;
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
    if (cap == nullptr)
        return ZX_ERR_NOT_FOUND;

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
            if (cap_index + 1u < num_capabilities_)
                val = cap_base + pci_cap_len(cap);
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

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
//
// Read accesses to reserved or unimplemented registers must be completed
// normally and a data value of 0 returned.
static zx_status_t pci_device_read_unimplemented(uint32_t* value) {
    *value = 0;
    return ZX_OK;
}

/* Read a 4 byte aligned value from PCI config space. */
zx_status_t PciDevice::ReadConfigWord(uint8_t reg, uint32_t* value) {
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
        if (capabilities_ != nullptr)
            status |= PCI_STATUS_NEW_CAPS;
        *value |= status << 16;
        return ZX_OK;
    }
    //  -------------------------------------------------
    // |    (31..16)    |    (15..8)   |      (7..0)     |
    // |   class_code   |    prog_if   |    revision_id  |
    //  -------------------------------------------------
    case PCI_CONFIG_REVISION_ID:
        *value = attrs_.class_code << 16 | attrs_.revision_id;
        return ZX_OK;
    //  ---------------------------------------------------------------
    // |   (31..24)  |   (23..16)    |    (15..8)    |      (7..0)     |
    // |     BIST    |  header_type  | latency_timer | cache_line_size |
    //  ---------------------------------------------------------------
    case PCI_CONFIG_CACHE_LINE_SIZE:
        *value = PCI_HEADER_TYPE_STANDARD << 16;
        return ZX_OK;
    case PCI_REGISTER_BAR_0:
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5: {
        uint32_t bar_num = (reg - PCI_REGISTER_BAR_0) / 4;
        if (bar_num >= PCI_MAX_BARS)
            return pci_device_read_unimplemented(value);

        fbl::AutoLock lock(&mutex_);
        const PciBar* bar = &bar_[bar_num];
        *value = bar->addr | bar->aspace;
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
        if (capabilities_ != nullptr)
            *value |= PCI_REGISTER_CAP_BASE;
        return ZX_OK;
    case PCI_REGISTER_CAP_BASE... PCI_REGISTER_CAP_TOP:
        if (ReadCapability(reg, value) != ZX_ERR_NOT_FOUND)
            return ZX_OK;
    // Fall-through if the capability is not-implemented.
    // These are all 32-bit registers.
    case PCI_CONFIG_CARDBUS_CIS_PTR:
    case PCI_CONFIG_EXP_ROM_ADDRESS:
        return pci_device_read_unimplemented(value);
    }

    fprintf(stderr, "Unhandled PCI device read %#x\n", reg);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t PciDevice::ReadConfig(uint16_t reg, uint8_t len, uint32_t* value) {
    // Perform 4-byte aligned read and then shift + mask the result to get the
    // expected value.
    uint32_t word = 0;
    const uint8_t reg_mask = bit_mask<uint8_t>(2);
    uint8_t word_aligend_reg = static_cast<uint8_t>(reg & ~reg_mask);
    uint8_t bit_offset = static_cast<uint8_t>((reg & reg_mask) * 8);
    zx_status_t status = ReadConfigWord(word_aligend_reg, &word);
    if (status != ZX_OK)
        return status;

    word >>= bit_offset;
    word &= bit_mask<uint32_t>(len * 8);
    *value = word;
    return ZX_OK;
}

// PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
//
// All PCI devices must treat Configuration Space write operations to reserved
// registers as no-ops; that is, the access must be completed  normally on the
// bus and the data discarded.
static inline zx_status_t pci_device_write_unimplemented() {
    return ZX_OK;
}

zx_status_t PciDevice::WriteConfig(uint16_t reg, uint8_t len, uint32_t value) {
    switch (reg) {
    case PCI_CONFIG_VENDOR_ID:
    case PCI_CONFIG_DEVICE_ID:
    case PCI_CONFIG_REVISION_ID:
    case PCI_CONFIG_HEADER_TYPE:
    case PCI_CONFIG_CLASS_CODE:
    case PCI_CONFIG_CLASS_CODE_SUB:
    case PCI_CONFIG_CLASS_CODE_BASE:
        // Read-only registers.
        return ZX_ERR_NOT_SUPPORTED;
    case PCI_CONFIG_COMMAND: {
        if (len != 2)
            return ZX_ERR_NOT_SUPPORTED;
        fbl::AutoLock lock(&mutex_);
        command_ = static_cast<uint16_t>(value);
        return ZX_OK;
    }
    case PCI_REGISTER_BAR_0:
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5: {
        if (len != 4)
            return ZX_ERR_NOT_SUPPORTED;

        uint32_t bar_num = (reg - PCI_REGISTER_BAR_0) / 4;
        if (bar_num >= PCI_MAX_BARS)
            return pci_device_write_unimplemented();

        fbl::AutoLock lock(&mutex_);
        PciBar* bar = &bar_[bar_num];
        bar->addr = value;
        // We zero bits in the BAR in order to set the size.
        bar->addr &= ~(bar->size - 1);
        return ZX_OK;
    }
    default:
        return pci_device_write_unimplemented();
    }
}

static bool pci_device_aspace_enabled(uint8_t aspace, uint16_t command) {
    switch (aspace) {
    case PCI_BAR_ASPACE_PIO:
        return command & PCI_COMMAND_IO_EN;
    case PCI_BAR_ASPACE_MMIO:
        return command & PCI_COMMAND_MEM_EN;
    default:
        return false;
    }
}

zx_status_t PciBus::MappedDevice(uint8_t aspace, uintptr_t addr,
                                 PciDevice** device_out, uint8_t* bar_out,
                                 uint16_t* off) {
    for (uint8_t i = 0; i < PCI_MAX_DEVICES; i++) {
        PciDevice* device = device_[i];
        if (device == nullptr)
            continue;

        for (uint8_t bar_num = 0; bar_num < PCI_MAX_BARS; ++bar_num) {
            uint16_t command;
            PciBar* bar;
            {
                fbl::AutoLock lock(&device->mutex_);

                command = device->command_;
                bar = &device->bar_[bar_num];
            }

            // Ensure IO operations are enabled for this device.
            if (!pci_device_aspace_enabled(aspace, command))
                continue;

            // Check if the BAR is implemented and configured for the requested
            // IO type.
            uintptr_t bar_base = pci_bar_base(bar);
            uint16_t bar_size = pci_bar_size(bar);
            if (!bar_size || bar->aspace != aspace)
                continue;

            if (addr >= bar_base && addr < bar_base + bar_size) {
                *bar_out = bar_num;
                *off = static_cast<uint16_t>(addr - bar_base);
                *device_out = device_[i];
                return ZX_OK;
            }
        }
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t PciDevice::Handler(zx_port_packet_t* packet, void* ctx) {
    auto pci_device = static_cast<PciDevice*>(ctx);

    // We provide the bar number as the trap key.
    if (packet->key > UINT8_MAX)
        return ZX_ERR_OUT_OF_RANGE;
    uint8_t bar = static_cast<uint8_t>(packet->key);
    uint32_t bar_base = pci_bar_base(&pci_device->bar_[bar]);

    uint16_t device_port;
    zx_vcpu_io_t io;
    io.access_size = packet->guest_io.access_size;
    switch (packet->type) {
    case ZX_PKT_TYPE_GUEST_BELL:
        // Translate all BELLs into a write-0. We're intentionally using the
        // zx_vcpu_io_t structure to conform to the PciDevice interface.
        //
        // TODO(tjdetwiler): Introduce a new structure for the purpose of
        // handling traps to decouple from zx_vcpu_io_t.
        device_port = static_cast<uint16_t>(packet->guest_bell.addr - bar_base);
        io.u32 = 0;
        break;
    case ZX_PKT_TYPE_GUEST_IO:
        device_port = static_cast<uint16_t>(packet->guest_io.port - bar_base);
        io.u32 = packet->guest_io.u32;
        break;
    // Async mem traps are not supported.
    case ZX_PKT_TYPE_GUEST_MEM:
    default:
        return ZX_ERR_INVALID_ARGS;
    }
    return pci_device->WriteBar(bar, device_port, &io);
}

zx_status_t PciDevice::SetupBarTraps(zx_handle_t guest) {
    size_t num_traps = 0;
    trap_args_t traps[PCI_MAX_BARS];
    for (uint8_t i = 0; i < PCI_MAX_BARS; ++i) {
        PciBar* bar = &bar_[i];
        if (!is_bar_implemented(i))
            break;

        trap_args_t* trap = &traps[num_traps++];
        trap->key = i;
        trap->addr = pci_bar_base(bar);
        trap->len = pci_bar_size(bar);
        if (bar->aspace == PCI_BAR_ASPACE_PIO) {
            if (bar->memory_type == PciMemoryType::BELL)
                return ZX_ERR_INVALID_ARGS;
            trap->kind = ZX_GUEST_TRAP_IO;
        } else {
            trap->kind = bar->memory_type == PciMemoryType::BELL
                ? ZX_GUEST_TRAP_BELL : ZX_GUEST_TRAP_MEM;
        }
        trap->use_port = bar->memory_type != PciMemoryType::STRONG;
    }

    if (num_traps == 0)
        return ZX_OK;
    return device_trap(guest, traps, num_traps, PciDevice::Handler, this);
}

zx_status_t PciDevice::Interrupt() const {
    if (!bus_)
        return ZX_ERR_BAD_STATE;
    return bus_->Interrupt(*this);
}

zx_status_t PciBus::Interrupt(const PciDevice& device) const {
    return io_apic_->Interrupt(device.global_irq_);
}
