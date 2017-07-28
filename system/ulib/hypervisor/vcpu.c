// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <hypervisor/bits.h>
#include <hypervisor/block.h>
#include <hypervisor/decode.h>
#include <hypervisor/ports.h>
#include <hypervisor/vcpu.h>
#include <hw/pci.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

#if __x86_64__
#include <acpica/acpi.h>
#include <acpica/actypes.h>
#endif // __x86_64__

/* Memory-mapped device physical addresses. */
#define LOCAL_APIC_PHYS_BASE                    0xfee00000
#define LOCAL_APIC_PHYS_TOP                     (LOCAL_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define IO_APIC_PHYS_BASE                       0xfec00000
#define IO_APIC_PHYS_TOP                        (IO_APIC_PHYS_BASE + PAGE_SIZE - 1)

/* PCI ECAM memory layout. These are provided to the guest via the MCFG ACPI
 * table.
 */
#define PCI_ECAM_START_BUS                      0
#define PCI_ECAM_END_BUS                        0
#define PCI_ECAM_PHYS_BASE                      0xd0000000
#define PCI_ECAM_PHYS_TOP \
    (PCI_ECAM_PHYS_BASE + PCI_ECAM_SIZE(PCI_ECAM_START_BUS, PCI_ECAM_END_BUS) - 1)

/* Local APIC register addresses. */
#define LOCAL_APIC_REGISTER_ID                  0x0020
#define LOCAL_APIC_REGISTER_VERSION             0x0030
#define LOCAL_APIC_REGISTER_LDR                 0x00d0
#define LOCAL_APIC_REGISTER_DFR                 0x00e0
#define LOCAL_APIC_REGISTER_SVR                 0x00f0
#define LOCAL_APIC_REGISTER_ISR_31_0            0x0100
#define LOCAL_APIC_REGISTER_ISR_255_224         0x0170
#define LOCAL_APIC_REGISTER_IRR_31_0            0x0200
#define LOCAL_APIC_REGISTER_IRR_255_224         0x0270
#define LOCAL_APIC_REGISTER_ESR                 0x0280
#define LOCAL_APIC_REGISTER_LVT_TIMER           0x0320
#define LOCAL_APIC_REGISTER_LVT_THERMAL         0x0330
#define LOCAL_APIC_REGISTER_LVT_PERFMON         0x0340
#define LOCAL_APIC_REGISTER_LVT_LINT0           0x0350
#define LOCAL_APIC_REGISTER_LVT_LINT1           0x0360
#define LOCAL_APIC_REGISTER_LVT_ERROR           0x0370
#define LOCAL_APIC_REGISTER_INITIAL_COUNT       0x0380

/* IO APIC register addresses. */
#define IO_APIC_IOREGSEL                        0x00
#define IO_APIC_IOWIN                           0x10

/* IO APIC register addresses. */
#define IO_APIC_REGISTER_ID                     0x00
#define IO_APIC_REGISTER_VER                    0x01
#define IO_APIC_REGISTER_ARBITRATION            0x02

/* IO APIC configuration constants. */
#define IO_APIC_VERSION                         0x11
#define FIRST_REDIRECT_OFFSET                   0x10
#define LAST_REDIRECT_OFFSET                    (FIRST_REDIRECT_OFFSET + IO_APIC_REDIRECT_OFFSETS - 1)

/* PIC configuration constants. */
#define PIC_INVALID                             UINT8_MAX

/* UART configuration constants. */
#define UART_BUFFER_SIZE                        512u

/* UART configuration flags. */
#define UART_INTERRUPT_ENABLE_THR_EMPTY         (1u << 1)
#define UART_INTERRUPT_ID_NONE                  (1u << 0)
#define UART_INTERRUPT_ID_THR_EMPTY             (1u << 1)
#define UART_INTERRUPT_ID_NO_FIFO_MASK          0x0f
#define UART_STATUS_EMPTY                       (1u << 5)
#define UART_STATUS_IDLE                        (1u << 6)

/* RTC register addresses. */
#define RTC_REGISTER_SECONDS                    0u
#define RTC_REGISTER_MINUTES                    2u
#define RTC_REGISTER_HOURS                      4u
#define RTC_REGISTER_DAY_OF_MONTH               7u
#define RTC_REGISTER_MONTH                      8u
#define RTC_REGISTER_YEAR                       9u
#define RTC_REGISTER_A                          10u
#define RTC_REGISTER_B                          11u

/* RTC register B flags. */
#define RTC_REGISTER_B_DAYLIGHT_SAVINGS         (1u << 0)
#define RTC_REGISTER_B_HOUR_FORMAT              (1u << 1)

/* I8042 status flags. */
#define I8042_STATUS_OUTPUT_FULL                (1u << 0)
#define I8042_STATUS_INPUT_FULL                 (1u << 1)

/* I8042 test constants. */
#define I8042_COMMAND_TEST                      0xaa
#define I8042_DATA_TEST_RESPONSE                0x55

/* PM register addresses. */
#define PM1A_REGISTER_STATUS                    0x0
#define PM1A_REGISTER_ENABLE                    (ACPI_PM1_REGISTER_WIDTH / 8)

/* Interrupt vectors. */
#define X86_INT_UART                            0x4
#define X86_INT_GP_FAULT                        0xd

static mx_status_t handle_local_apic(local_apic_state_t* local_apic_state,
                                     const mx_guest_memory_t* memory, instruction_t* inst) {
    MX_ASSERT(memory->addr >= LOCAL_APIC_PHYS_BASE);
    mx_vaddr_t offset = memory->addr - LOCAL_APIC_PHYS_BASE;
    switch (offset) {
    case LOCAL_APIC_REGISTER_VERSION: {
        // From Intel Volume 3, Section 10.4.8.
        //
        // We choose 15H as it causes us to be seen as a modern APIC by Linux,
        // and is the highest non-reserved value.
        const uint32_t version = 0x15;
        const uint32_t max_lvt_entry = 0x6; // LVT entries minus 1.
        const uint32_t eoi_suppression = 0; // Disable support for EOI-broadcast suppression.
        return inst_read32(inst, version | (max_lvt_entry << 16) | (eoi_suppression << 24));
    }
    case LOCAL_APIC_REGISTER_ESR:
        // From Intel Volume 3, Section 10.5.3: Before attempt to read from the
        // ESR, software should first write to it.
        //
        // Therefore, we ignore writes to the ESR.
        if (inst->type == INST_MOV_WRITE)
            return MX_OK;
    case LOCAL_APIC_REGISTER_ISR_31_0 ... LOCAL_APIC_REGISTER_ISR_255_224:
    case LOCAL_APIC_REGISTER_IRR_31_0 ... LOCAL_APIC_REGISTER_IRR_255_224:
        return inst_read32(inst, 0);
    case LOCAL_APIC_REGISTER_DFR:
    case LOCAL_APIC_REGISTER_ID:
    case LOCAL_APIC_REGISTER_LDR:
    case LOCAL_APIC_REGISTER_LVT_ERROR:
    case LOCAL_APIC_REGISTER_LVT_LINT0:
    case LOCAL_APIC_REGISTER_LVT_LINT1:
    case LOCAL_APIC_REGISTER_LVT_PERFMON:
    case LOCAL_APIC_REGISTER_LVT_THERMAL:
    case LOCAL_APIC_REGISTER_LVT_TIMER:
    case LOCAL_APIC_REGISTER_SVR:
        return inst_rw32(inst, local_apic_state->apic_addr + offset);
    case LOCAL_APIC_REGISTER_INITIAL_COUNT: {
        uint32_t initial_count;
        mx_status_t status = inst_write32(inst, &initial_count);
        if (status != MX_OK)
            return status;
        return initial_count > 0 ? MX_ERR_NOT_SUPPORTED : MX_OK;
    }}

    fprintf(stderr, "Unhandled local APIC %#lx\n", offset);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_io_apic(io_apic_state_t* io_apic_state,
                                  const mx_guest_memory_t* memory, instruction_t* inst) {
    MX_ASSERT(memory->addr >= IO_APIC_PHYS_BASE);
    mx_vaddr_t offset = memory->addr - IO_APIC_PHYS_BASE;
    switch (offset) {
    case IO_APIC_IOREGSEL: {
        mx_status_t status = inst_write32(inst, &io_apic_state->select);
        if (status != MX_OK)
            return status;
        return io_apic_state->select > UINT8_MAX ? MX_ERR_INVALID_ARGS : MX_OK;
    }
    case IO_APIC_IOWIN:
        switch (io_apic_state->select) {
        case IO_APIC_REGISTER_ID:
            return inst_rw32(inst, &io_apic_state->id);
        case IO_APIC_REGISTER_VER:
            // There are two redirect offsets per redirection entry. We return
            // the maximum redirection entry index.
            //
            // From Intel 82093AA, Section 3.2.2.
            return inst_read32(inst, (IO_APIC_REDIRECT_OFFSETS / 2 - 1) << 16 | IO_APIC_VERSION);
        case IO_APIC_REGISTER_ARBITRATION:
            // Since we have a single I/O APIC, it is always the winner
            // of arbitration and its arbitration register is always 0.
            return inst_read32(inst, 0);
        case FIRST_REDIRECT_OFFSET ... LAST_REDIRECT_OFFSET: {
            uint32_t i = io_apic_state->select - FIRST_REDIRECT_OFFSET;
            return inst_rw32(inst, io_apic_state->redirect + i);
        }}
    }

    fprintf(stderr, "Unhandled IO APIC %#lx\n", offset);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_pci_config_read(guest_state_t* guest_state,
                                          uint8_t bus, uint8_t device,
                                          uint8_t function, uint16_t reg,
                                          size_t len, uint32_t* value) {
    // PCI LOCAL BUS SPECIFICATION, REV. 3.0 Section 6.1
    //
    // The host bus to PCI bridge must unambiguously report attempts to read the
    // Vendor ID of non-existent devices. Since 0 FFFFh is an invalid Vendor ID,
    // it is adequate for the host bus to PCI bridge to return a value of all
    // 1's on read accesses to Configuration Space registers of non-existent
    // devices.
    if (device >= PCI_MAX_DEVICES) {
        *value = (uint32_t) BIT_MASK(len * 8);
        return MX_OK;
    }

    pci_device_state_t* device_state = &guest_state->pci_device_state[device];
    return pci_config_read(device_state, bus, device, function, reg,
                           len, value);
}

static mx_status_t handle_pci_config_write(guest_state_t* guest_state,
                                           uint8_t bus, uint8_t device,
                                           uint8_t function, uint16_t reg,
                                           size_t len, uint32_t value) {
    // We don't expect software to ever write to an unimplemented device.
    if (device >= PCI_MAX_DEVICES)
        return MX_ERR_OUT_OF_RANGE;

    pci_device_state_t* device_state = &guest_state->pci_device_state[device];
    return pci_config_write(device_state, bus, device, function, reg,
                            len, value);
}

static mx_status_t handle_pci_mmio_access(guest_state_t* guest_state,
                                          uint8_t bus, uint8_t device,
                                          uint8_t function, uint16_t reg,
                                          instruction_t* inst,
                                          const mx_guest_memory_t* memory) {
    mx_status_t status;
    switch (inst->type) {
    case INST_TEST: {
        uint32_t result = 0;
        status = handle_pci_config_read(guest_state, bus, device, function, reg,
                                        inst->mem, &result);
        if (status != MX_OK)
            return status;

        switch (inst->mem) {
        case 1:
            return inst_test8(inst, inst->imm, result);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }
    case INST_MOV_READ: {
        uint32_t result = 0;
        status = handle_pci_config_read(guest_state, bus, device, function, reg,
                                        inst->mem, &result);
        if (status != MX_OK)
            return status;

        switch (inst->mem) {
        case 1:
            return inst_read8(inst, result);
        case 2:
            return inst_read16(inst, result);
        case 4:
            return inst_read32(inst, result);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }
    case INST_MOV_WRITE: {
        uint32_t value = 0;
        switch (inst->mem) {
        case 2: {
            status = inst_write16(inst, (uint16_t*) &value);
            break;
        }
        case 4:
            status = inst_write32(inst, &value);
            break;
        default:
            status = MX_ERR_NOT_SUPPORTED;
            break;
        }
        if (status != MX_OK)
            return status;

        return handle_pci_config_write(guest_state, bus, device, function, reg,
                                       inst->mem, value);
    }}
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t unhandled_memory(const mx_guest_memory_t* memory, instruction_t* inst) {
    fprintf(stderr, "Unhandled address %#lx\n", memory->addr);
    if (inst->type == INST_MOV_READ)
        *inst->reg = UINT64_MAX;
    return MX_OK;
}

static mx_status_t handle_memory(vcpu_context_t* vcpu_context, const mx_guest_memory_t* memory) {
    mx_vcpu_state_t vcpu_state;
    mx_status_t status = vcpu_context->read_state(vcpu_context, MX_VCPU_STATE, &vcpu_state,
                                                  sizeof(vcpu_state));
    if (status != MX_OK)
        return status;

    instruction_t inst;
#if __aarch64__
    status = MX_ERR_NOT_SUPPORTED;
#elif __x86_64__
    status = inst_decode(memory->inst_buf, memory->inst_len, &vcpu_state, &inst);
#else
#error Unsupported architecture
#endif

    if (status != MX_OK) {
        fprintf(stderr, "Unsupported instruction:");
#if __x86_64__
        for (uint8_t i = 0; i < memory->inst_len; i++)
            fprintf(stderr, " %x", memory->inst_buf[i]);
#endif // __x86_64__
        fprintf(stderr, "\n");
    } else {
        guest_state_t* guest_state = vcpu_context->guest_state;
        switch (memory->addr) {
        case LOCAL_APIC_PHYS_BASE ... LOCAL_APIC_PHYS_TOP:
            status = handle_local_apic(&vcpu_context->local_apic_state, memory, &inst);
            break;
        case IO_APIC_PHYS_BASE ... IO_APIC_PHYS_TOP:
            mtx_lock(&guest_state->mutex);
            status = handle_io_apic(&guest_state->io_apic_state, memory, &inst);
            mtx_unlock(&guest_state->mutex);
            break;
        case PCI_ECAM_PHYS_BASE ... PCI_ECAM_PHYS_TOP: {
            mtx_lock(&guest_state->mutex);
            status = handle_pci_mmio_access(vcpu_context->guest_state,
                                            PCI_ECAM_BUS(memory->addr) + PCI_ECAM_START_BUS,
                                            PCI_ECAM_DEVICE(memory->addr),
                                            PCI_ECAM_FUNCTION(memory->addr),
                                            PCI_ECAM_REGISTER(memory->addr),
                                            &inst, memory);
            mtx_unlock(&guest_state->mutex);
            break;
        }
        default:
            status = unhandled_memory(memory, &inst);
            break;
        }
    }

    if (status != MX_OK) {
        uint32_t vector = X86_INT_GP_FAULT;
        return mx_vcpu_interrupt(vcpu_context->vcpu, vector);
    } else if (inst.type == INST_MOV_READ || inst.type == INST_TEST) {
        // If there was an attempt to read or test memory, update the GPRs.
        return vcpu_context->write_state(vcpu_context, MX_VCPU_STATE, &vcpu_state,
                                         sizeof(vcpu_state));
    }
    return status;
}

static uint8_t to_bcd(uint8_t binary) {
    return ((binary / 10) << 4) | (binary % 10);
}

static mx_status_t handle_rtc(uint8_t rtc_index, uint8_t* value) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL)
        return MX_ERR_INTERNAL;
    switch (rtc_index) {
    case RTC_REGISTER_SECONDS:
        *value = to_bcd(tm.tm_sec);
        break;
    case RTC_REGISTER_MINUTES:
        *value = to_bcd(tm.tm_min);
        break;
    case RTC_REGISTER_HOURS:
        *value = to_bcd(tm.tm_hour);
        break;
    case RTC_REGISTER_DAY_OF_MONTH:
        *value = to_bcd(tm.tm_mday);
        break;
    case RTC_REGISTER_MONTH:
        *value = to_bcd(tm.tm_mon);
        break;
    case RTC_REGISTER_YEAR:
        // RTC expects the number of years since 2000.
        *value = to_bcd(tm.tm_year - 100);
        break;
     case RTC_REGISTER_A:
        // Ensure that UIP is 0. Other values (clock frequency) are obsolete.
        *value = 0;
        break;
    case RTC_REGISTER_B:
        *value = RTC_REGISTER_B_HOUR_FORMAT;
        if (tm.tm_isdst)
            *value |= RTC_REGISTER_B_DAYLIGHT_SAVINGS;
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
    return MX_OK;
}

uint8_t irq_redirect(const io_apic_state_t* io_apic_state, uint8_t global_irq) {
    return io_apic_state->redirect[global_irq * 2] & UINT8_MAX;
}

static mx_status_t handle_input(vcpu_context_t* vcpu_context, const mx_guest_io_t* io) {
#if __x86_64__
    mx_vcpu_io_t vcpu_io;
    memset(&vcpu_io, 0, sizeof(vcpu_io));
    io_port_state_t* io_port_state = &vcpu_context->guest_state->io_port_state;
    switch (io->port) {
    case UART_RECEIVE_PORT:
    case UART_MODEM_CONTROL_PORT:
    case UART_MODEM_STATUS_PORT:
    case UART_SCR_SCRATCH_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = 0;
        break;
    case UART_INTERRUPT_ENABLE_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = io_port_state->uart_interrupt_enable;
        break;
    case UART_INTERRUPT_ID_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = UART_INTERRUPT_ID_NO_FIFO_MASK & io_port_state->uart_interrupt_id;
        // Technically, we should always reset the interrupt id register to UART_INTERRUPT_ID_NONE
        // after a read, but this requires us to take a lock on every THR output (to set
        // interrupt_id to UART_INTERRUPT_ID_THR_EMPTY before we fire the interrupt).
        // We aren't too fussed about being perfect here, so instead we will reset it when
        // UART_INTERRUPT_ENABLE_THR_EMPTY is disabled below.
        break;
    case UART_LINE_CONTROL_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = io_port_state->uart_line_control;
        break;
    case UART_LINE_STATUS_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = UART_STATUS_IDLE | UART_STATUS_EMPTY;
        break;
    case RTC_DATA_PORT:
        vcpu_io.access_size = 1;
        mx_status_t status = handle_rtc(io_port_state->rtc_index, &vcpu_io.u8);
        if (status != MX_OK)
            return status;
        break;
    case I8042_DATA_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = io_port_state->i8042_command == I8042_COMMAND_TEST ?
                       I8042_DATA_TEST_RESPONSE : 0;
        break;
    case I8042_COMMAND_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = I8042_STATUS_OUTPUT_FULL;
        break;
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        uint32_t bit_offset = ((io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8);
        uint32_t addr = io_port_state->pci_config_address >> bit_offset;
        uint32_t bit_mask = (uint32_t) BIT_MASK(io->access_size * 8);
        vcpu_io.access_size = io->access_size;
        vcpu_io.u32 = (vcpu_io.u32 & ~bit_mask) | (addr & bit_mask);
        break;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        size_t offset = io->port - PCI_CONFIG_DATA_PORT_BASE;
        uint32_t addr = io_port_state->pci_config_address;
        uint32_t register_value = 0;
        status = handle_pci_config_read(vcpu_context->guest_state,
                                        PCI_TYPE1_BUS(addr),
                                        PCI_TYPE1_DEVICE(addr),
                                        PCI_TYPE1_FUNCTION(addr),
                                        PCI_TYPE1_REGISTER(addr) + offset,
                                        io->access_size, &register_value);
        if (status != MX_OK) {
            return status;
        }

        switch (io->access_size) {
        case 1:
            vcpu_io.u8 = register_value;
            break;
        case 2:
            vcpu_io.u16 = register_value;
            break;
        case 4:
            vcpu_io.u32 = register_value;
            break;
        default:
            fprintf(stderr, "Unhandled port in %#x\n", io->port);
            return MX_ERR_NOT_SUPPORTED;
        }
        vcpu_io.access_size = io->access_size;
        break;
    }
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
        vcpu_io.access_size = 2;
        vcpu_io.u16 = 0;
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        vcpu_io.access_size = 2;
        vcpu_io.u16 = io_port_state->pm1_enable;
        break;
    case PIC1_DATA_PORT:
        vcpu_io.access_size = 1;
        vcpu_io.u8 = PIC_INVALID;
        break;
    default: {
        uint32_t port_off;
        pci_device_state_t* devices = vcpu_context->guest_state->pci_device_state;
        switch (pci_device(devices, PCI_BAR_IO_TYPE_PIO, io->port, &port_off)) {
        case PCI_DEVICE_VIRTIO_BLOCK: {
            mx_status_t status = handle_virtio_block_read(vcpu_context->guest_state, port_off,
                                                          &vcpu_io);
            if (status != MX_OK)
                return status;
            break;
        }
        default:
            fprintf(stderr, "Unhandled port in %#x\n", io->port);
            return MX_ERR_NOT_SUPPORTED;
        }
    }}

    if (vcpu_io.access_size != io->access_size)
        return MX_ERR_IO_DATA_INTEGRITY;
    return vcpu_context->write_state(vcpu_context, MX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t handle_output(vcpu_context_t* vcpu_context, const mx_guest_io_t* io) {
#if __x86_64__
    io_port_state_t* io_port_state = &vcpu_context->guest_state->io_port_state;
    switch (io->port) {
    case I8042_DATA_PORT:
    case I8253_CHANNEL_0:
    case I8253_CONTROL_PORT:
    case PIC1_COMMAND_PORT ... PIC1_DATA_PORT:
    case PIC2_COMMAND_PORT ... PIC2_DATA_PORT:
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
        return MX_OK;
    case RTC_INDEX_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->rtc_index = io->u8;
        return MX_OK;
    case I8042_COMMAND_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->i8042_command = io->u8;
        return MX_OK;
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        // Software can (and Linux does) perform partial word accesses to the
        // PCI address register. This means we need to take care to read/write
        // portions of the 32bit register without trampling the other bits.
        uint32_t bit_offset = ((io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8);
        uint32_t bit_size = io->access_size * 8;
        uint32_t bit_mask = (uint32_t) BIT_MASK(bit_size);

        // Clear out the bits we'll be modifying.
        io_port_state->pci_config_address = CLEAR_BITS(io_port_state->pci_config_address,
                                                       bit_size, bit_offset);

        // Write config address.
        io_port_state->pci_config_address |= ((io->u32 & bit_mask) << bit_offset);
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP: {
        size_t offset = io->port - PCI_CONFIG_DATA_PORT_BASE;
        uint32_t addr = io_port_state->pci_config_address;
        return handle_pci_config_write(vcpu_context->guest_state,
                                       PCI_TYPE1_BUS(addr),
                                       PCI_TYPE1_DEVICE(addr),
                                       PCI_TYPE1_FUNCTION(addr),
                                       PCI_TYPE1_REGISTER(addr) + offset,
                                       io->access_size, io->u32);
    }

    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->pm1_enable = io->u16;
        return MX_OK;
    }

    uint32_t port_off;
    pci_device_state_t* devices = vcpu_context->guest_state->pci_device_state;
    switch (pci_device(devices, PCI_BAR_IO_TYPE_PIO, io->port, &port_off)) {
    case PCI_DEVICE_VIRTIO_BLOCK: {
        return handle_virtio_block_write(vcpu_context, port_off, io);
    }}

    fprintf(stderr, "Unhandled port out %#x\n", io->port);
    return MX_ERR_NOT_SUPPORTED;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t handle_io(vcpu_context_t* vcpu_context, mx_guest_io_t* io) {
    mtx_lock(&vcpu_context->guest_state->mutex);
    mx_status_t status = io->input ?
                         handle_input(vcpu_context, io) :
                         handle_output(vcpu_context, io);
    mtx_unlock(&vcpu_context->guest_state->mutex);
    return status;
}

static mx_status_t fifo_create(mx_handle_t* out0, mx_handle_t* out1) {
    const uint32_t count = PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE;
    const uint32_t size = sizeof(mx_guest_packet_t);
    return mx_fifo_create(count, size, 0, out0, out1);
}

static mx_status_t fifo_wait(mx_handle_t fifo, mx_signals_t signals) {
    mx_signals_t observed = 0;
    while (!(observed & signals)) {
        mx_status_t status = mx_object_wait_one(fifo, signals | MX_FIFO_PEER_CLOSED,
                                                MX_TIME_INFINITE, &observed);
        if (status != MX_OK)
            return status;
        if (observed & MX_FIFO_PEER_CLOSED)
            return MX_ERR_PEER_CLOSED;
    }
    return MX_OK;
}

static mx_status_t raise_thr_empty_interrupt(mx_handle_t vcpu, io_apic_state_t* io_apic_state) {
    uint32_t interrupt = irq_redirect(io_apic_state, X86_INT_UART);
    // UART IRQs overlap with CPU exception handlers, so they need to be remapped.
    // If that hasn't happened yet, don't fire the interrupt - it would be bad.
    if (interrupt == 0) {
        return MX_OK;
    }
    return mx_vcpu_interrupt(vcpu, interrupt);
}

mx_status_t vcpu_handle_uart(mx_guest_io_t* io, guest_state_t* guest_state, mx_handle_t vcpu) {
    static uint8_t buffer[UART_BUFFER_SIZE] = {};
    static uint16_t offset = 0;
    static bool interrupt_on_thr_empty = false;

    mtx_t* mutex = &guest_state->mutex;
    io_port_state_t* io_port_state = &guest_state->io_port_state;
    io_apic_state_t* io_apic_state = &guest_state->io_apic_state;

    switch (io->port) {
    case UART_RECEIVE_PORT:
        for (int i = 0; i < io->access_size; i++) {
            buffer[offset++] = io->data[i];
            if (offset == UART_BUFFER_SIZE || io->data[i] == '\r') {
                printf("%.*s", offset, buffer);
                offset = 0;
            }
        }
        if (interrupt_on_thr_empty) {
            return raise_thr_empty_interrupt(vcpu, io_apic_state);
        }
        break;
    case UART_INTERRUPT_ENABLE_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        interrupt_on_thr_empty = io->u8 & UART_INTERRUPT_ENABLE_THR_EMPTY;
        mtx_lock(mutex);
        io_port_state->uart_interrupt_enable = io->u8;
        io_port_state->uart_interrupt_id =
                interrupt_on_thr_empty ? UART_INTERRUPT_ID_THR_EMPTY : UART_INTERRUPT_ID_NONE;
        mtx_unlock(mutex);

        if (interrupt_on_thr_empty) {
            return raise_thr_empty_interrupt(vcpu, io_apic_state);
        }
        break;
    case UART_LINE_CONTROL_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        mtx_lock(mutex);
        io_port_state->uart_line_control = io->u8;
        mtx_unlock(mutex);
        break;
    case UART_INTERRUPT_ID_PORT:
    case UART_MODEM_CONTROL_PORT ... UART_SCR_SCRATCH_PORT:
        break;
    default:
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
}

static int uart_loop(void* arg) {
    vcpu_context_t* vcpu_context = arg;
    guest_state_t* guest_state = vcpu_context->guest_state;
    mx_handle_t user_fifo;
    mx_handle_t kernel_fifo;
    mx_status_t status = fifo_create(&user_fifo, &kernel_fifo);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create UART FIFO %d\n", status);
        return MX_ERR_INTERNAL;
    }

    const mx_vaddr_t addr = UART_RECEIVE_PORT;
    const size_t len = (UART_SCR_SCRATCH_PORT - addr) + 1;
    status = mx_guest_set_trap(guest_state->guest, MX_GUEST_TRAP_IO, addr, len, kernel_fifo);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set trap for UART FIFO %d\n", status);
        goto cleanup;
    }

    mx_guest_packet_t packets[PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE];
    while (true) {
        status = fifo_wait(user_fifo, MX_FIFO_READABLE);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to wait for UART FIFO %d\n", status);
            goto cleanup;
        }

        uint32_t num_packets;
        status = mx_fifo_read(user_fifo, packets, sizeof(packets), &num_packets);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to read from UART FIFO %d\n", status);
            goto cleanup;
        }

        for (uint32_t i = 0; i < num_packets; i++) {
            if (packets[i].type != MX_GUEST_PKT_IO) {
                fprintf(stderr, "Invalid packet type for UART %d\n", packets[i].type);
                goto cleanup;
            }
            status = vcpu_handle_uart(&packets[i].io, guest_state, vcpu_context->vcpu);
            if (status != MX_OK) {
                fprintf(stderr, "Unable to handle packet for UART %d\n", status);
                goto cleanup;
            }
        }
    }

cleanup:
    mx_handle_close(user_fifo);
    mx_handle_close(kernel_fifo);
    return MX_ERR_INTERNAL;
}

static mx_status_t vcpu_state_read(vcpu_context_t* vcpu_context, uint32_t kind, void* buffer,
                                   uint32_t len) {
    return mx_vcpu_read_state(vcpu_context->vcpu, kind, buffer, len);
}

static mx_status_t vcpu_state_write(vcpu_context_t* vcpu_context, uint32_t kind, const void* buffer,
                                    uint32_t len) {
    return mx_vcpu_write_state(vcpu_context->vcpu, kind, buffer, len);
}

void vcpu_init(vcpu_context_t* vcpu_context) {
    memset(vcpu_context, 0, sizeof(*vcpu_context));
    vcpu_context->read_state = vcpu_state_read;
    vcpu_context->write_state = vcpu_state_write;
}

mx_status_t vcpu_loop(vcpu_context_t* vcpu_context) {
    thrd_t uart_thread;
    int ret = thrd_create(&uart_thread, uart_loop, vcpu_context);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create UART thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }
    ret = thrd_detach(uart_thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach UART thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }

    while (true) {
        mx_guest_packet_t packet;
        mx_status_t status = mx_vcpu_resume(vcpu_context->vcpu, &packet);
        if (status != MX_OK)
            return status;
        status = vcpu_handle_packet(vcpu_context, &packet);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet.type, status);
            return status;
        }
    }
}

mx_status_t vcpu_handle_packet(vcpu_context_t* vcpu_context, mx_guest_packet_t* packet) {
    switch (packet->type) {
    case MX_GUEST_PKT_MEMORY:
        return handle_memory(vcpu_context, &packet->memory);
    case MX_GUEST_PKT_IO:
        return handle_io(vcpu_context, &packet->io);
    default:
        fprintf(stderr, "Unhandled guest packet %d\n", packet->type);
        return MX_ERR_NOT_SUPPORTED;
    }
}
