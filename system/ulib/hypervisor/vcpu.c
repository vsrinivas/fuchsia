// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

/* Clear contiguous bits in a word. */
#define CLEAR_BITS(x, nbits, shift) ((x) & (~(((1UL << (nbits)) - 1) << (shift))))

/* Memory-mapped device physical addresses. */
#define LOCAL_APIC_PHYS_BASE                    0xfee00000
#define LOCAL_APIC_PHYS_TOP                     (LOCAL_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define IO_APIC_PHYS_BASE                       0xfec00000
#define IO_APIC_PHYS_TOP                        (IO_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define PCI_PHYS_BASE(bus, device, function) \
    (0xd0000000 + (((bus) << 20) | ((device) << 15) | ((function) << 12)))
#define PCI_PHYS_TOP(bus, device, function)     (PCI_PHYS_BASE(bus, device, function) + 4095)

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

/* PCI BAR register addresses. */
#define PCI_REGISTER_BAR_0                      0x10
#define PCI_REGISTER_BAR_1                      0x14
#define PCI_REGISTER_BAR_2                      0x18
#define PCI_REGISTER_BAR_3                      0x1c
#define PCI_REGISTER_BAR_4                      0x20
#define PCI_REGISTER_BAR_5                      0x24

/* PCI configuration constants. */
#define PCI_BAR_IO_TYPE_PIO                     0x01
#define PCI_VENDOR_ID_VIRTIO                    0x1af4
#define PCI_VENDOR_ID_INTEL                     0x8086
#define PCI_DEVICE_ID_VIRTIO_BLOCK              0x1042
#define PCI_DEVICE_ID_INTEL_Q35                 0x29c0
#define PCI_CLASS_BRIDGE_HOST                   0x0600

/* PCI device constants. */
#define PCI_DEVICE_INVALID                      UINT16_MAX

/* PCI macros. */
// From a given address, get the PCI device index.
#define PCI_DEVICE(addr)                        (((addr - PCI_PHYS_BASE(0, 0, 0)) >> 15) & 0x1f)

// Get the PCI type 1 address for a register.
#define PCI_TYPE1_ADDR(bus, device, function, reg) \
    (0x80000000 | ((bus) << 16) | ((device) << 11) | ((function) << 8) | (reg))

/* Interrupt vectors. */
#define X86_INT_GP_FAULT                        0xd

static const uint16_t kPciDeviceBarSize[] = {
    0x10,   // PCI_DEVICE_ROOT_COMPLEX
    0x40,   // PCI_DEVICE_VIRTIO_BLOCK
};

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

static mx_status_t handle_pci_device(pci_device_state_t* pci_device_state,
                                     const mx_guest_memory_t* memory, instruction_t* inst,
                                     uint8_t device, uint16_t vendor_id, uint16_t device_id) {
    MX_ASSERT(memory->addr >= PCI_PHYS_BASE(0, device, 0));
    mx_vaddr_t offset = memory->addr - PCI_PHYS_BASE(0, device, 0);
    switch (offset) {
    case PCI_CONFIG_VENDOR_ID:
        return inst_read16(inst, vendor_id);
    case PCI_CONFIG_DEVICE_ID:
        return inst_read16(inst, device_id);
    case PCI_CONFIG_COMMAND:
        return inst_rw16(inst, &pci_device_state->command);
    case PCI_CONFIG_STATUS:
        switch (inst->type) {
        case INST_MOV_READ:
            return inst_read16(inst, PCI_STATUS_INTERRUPT);
        case INST_TEST:
            return inst_test8(inst, PCI_STATUS_NEW_CAPS, PCI_STATUS_INTERRUPT);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    case PCI_CONFIG_HEADER_TYPE:
        return inst_read8(inst, PCI_HEADER_TYPE_STANDARD);
    case PCI_CONFIG_INTERRUPT_PIN:
        return inst_read8(inst, 1);
    case PCI_CONFIG_CAPABILITIES:
    case PCI_CONFIG_CLASS_CODE:
    case PCI_CONFIG_CLASS_CODE_BASE:
    case PCI_CONFIG_CLASS_CODE_SUB:
    case PCI_CONFIG_REVISION_ID:
        return inst_read8(inst, 0);
    case PCI_REGISTER_BAR_0: {
        uint32_t* bar = &pci_device_state->bar[0];
        const uint32_t value = (pci_device_state->command & PCI_COMMAND_IO_EN) ?
                               *bar | PCI_BAR_IO_TYPE_PIO : UINT32_MAX;
        switch (inst->type) {
        case INST_MOV_READ:
            return inst_read32(inst, value);
        case INST_MOV_WRITE: {
            mx_status_t status = inst_write32(inst, bar);
            if (status != MX_OK)
                return status;
            // We zero bits in the BAR in order to set the size.
            *bar &= ~(kPciDeviceBarSize[device] - 1);
            *bar |= PCI_BAR_IO_TYPE_PIO;
            return MX_OK;
        }
        case INST_TEST:
            return inst_test8(inst, PCI_BAR_IO_TYPE_PIO, value);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }
    case PCI_REGISTER_BAR_1:
    case PCI_REGISTER_BAR_2:
    case PCI_REGISTER_BAR_3:
    case PCI_REGISTER_BAR_4:
    case PCI_REGISTER_BAR_5: {
        uint32_t default_bar = 0;
        return inst_rw32(inst, &default_bar);
    }}

    fprintf(stderr, "Unhandled PCI device %d %#lx\n", device, offset);
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
    mx_status_t status = vcpu_context->read_state(vcpu_context, &vcpu_state);
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
        status = MX_ERR_UNAVAILABLE;
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
        case PCI_PHYS_BASE(0, PCI_DEVICE_ROOT_COMPLEX, 0) ...
             PCI_PHYS_TOP(0, PCI_DEVICE_ROOT_COMPLEX, 0): {
            mtx_lock(&guest_state->mutex);
            pci_device_state_t* pci_device_state =
                &guest_state->pci_device_state[PCI_DEVICE_ROOT_COMPLEX];
            status = handle_pci_device(pci_device_state, memory, &inst, PCI_DEVICE_ROOT_COMPLEX,
                                       PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_Q35);
            mtx_unlock(&guest_state->mutex);
            break;
        }
        case PCI_PHYS_BASE(0, PCI_DEVICE_VIRTIO_BLOCK, 0) ...
             PCI_PHYS_TOP(0, PCI_DEVICE_VIRTIO_BLOCK, 0): {
            mtx_lock(&guest_state->mutex);
            pci_device_state_t* pci_device_state =
                &guest_state->pci_device_state[PCI_DEVICE_VIRTIO_BLOCK];
            status = handle_pci_device(pci_device_state, memory, &inst, PCI_DEVICE_VIRTIO_BLOCK,
                                       PCI_VENDOR_ID_VIRTIO, PCI_DEVICE_ID_VIRTIO_BLOCK);
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
        return mx_hypervisor_op(vcpu_context->vcpu, MX_HYPERVISOR_OP_VCPU_INTERRUPT,
                                &vector, sizeof(vector), NULL, 0);
    } else if (inst.type == INST_MOV_READ || inst.type == INST_TEST) {
        // If there was an attempt to read or test memory, update the GPRs.
        return vcpu_context->write_state(vcpu_context, &vcpu_state);
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

// TODO(abdulla): Introduce a syscall to associate a port range with a FIFO, so
// that we can directly communicate with the handler and remove this function.
static uint16_t pci_device(pci_device_state_t* pci_device_state, uint16_t port,
                           uint16_t* port_off) {
    for (unsigned i = 0; i < PCI_MAX_DEVICES; i++) {
        uint16_t bar0 = pci_device_state[i].bar[0];
        if (!(bar0 & PCI_BAR_IO_TYPE_PIO))
            continue;
        uint16_t bar_base = bar0 & ~PCI_BAR_IO_TYPE_PIO;
        uint16_t bar_size = kPciDeviceBarSize[i];
        if (port >= bar_base && port < bar_base + bar_size) {
            *port_off = port - bar_base;
            return i;
        }
    }
    return PCI_DEVICE_INVALID;
}

static mx_status_t handle_input(vcpu_context_t* vcpu_context, const mx_guest_io_t* io) {
#if __x86_64__
    mx_vcpu_state_t state;
    mx_status_t status = vcpu_context->read_state(vcpu_context, &state);
    if (status != MX_OK)
        return status;

    io_packet_t* io_packet = (io_packet_t*)&state.rax;
    io_port_state_t* io_port_state = &vcpu_context->guest_state->io_port_state;
    switch (io->port) {
    case UART_LINE_CONTROL_IO_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = io_port_state->uart_line_control;
        break;
    case UART_INTERRUPT_ENABLE_PORT:
    case UART_MODEM_CONTROL_IO_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = 0;
        break;
    case UART_LINE_STATUS_IO_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = UART_STATUS_IDLE | UART_STATUS_EMPTY;
        break;
    case RTC_DATA_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        status = handle_rtc(io_port_state->rtc_index, &io_packet->u8);
        if (status != MX_OK)
            return status;
        break;
    case I8042_DATA_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = io_port_state->i8042_command == I8042_COMMAND_TEST ?
                       I8042_DATA_TEST_RESPONSE : 0;
        break;
    case I8042_COMMAND_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = I8042_STATUS_OUTPUT_FULL;
        break;
    case PCI_CONFIG_ADDRESS_PORT_BASE... PCI_CONFIG_ADDRESS_PORT_TOP: {
        size_t bit_offset = ((io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8);
        uint32_t addr = io_port_state->pci_config_address >> bit_offset;
        switch (io->access_size) {
        case 1:
            io_packet->u8 = addr & UINT8_MAX;
            break;
        case 2:
            io_packet->u16 = addr & UINT16_MAX;
            break;
        case 4:
            io_packet->u32 = addr & UINT32_MAX;
            break;
        default:
            fprintf(stderr, "Unhandled port in %#x\n", io->port);
            return MX_ERR_NOT_SUPPORTED;
        }
        break;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP:
        // TODO(tjdetwiler): Temporarily stub out these ports to appease linux
        // with broken PCI until they're wired into accessing the virtual PCI
        // bus. Linux wants to find a host bridge so we'll give it one.
        switch (io_port_state->pci_config_address) {
        case PCI_TYPE1_ADDR(0, 0, 0, PCI_CONFIG_CLASS_CODE_SUB):
            if (io->access_size != 2)
                return MX_ERR_IO_DATA_INTEGRITY;
            io_packet->u16 = PCI_CLASS_BRIDGE_HOST;
            break;
        case PCI_TYPE1_ADDR(0, 0, 0, PCI_CONFIG_VENDOR_ID):
            if (io->access_size != 2)
                return MX_ERR_IO_DATA_INTEGRITY;
            io_packet->u16 = PCI_VENDOR_ID_INTEL;
            break;
        default:
            io_packet->u32 = 0;
            break;
        }
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u16 = 0;
        break;
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u16 = io_port_state->pm1_enable;
        break;
    case PIC1_DATA_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_packet->u8 = PIC_INVALID;
        break;
    default: {
        uint16_t port_off;
        switch (pci_device(vcpu_context->guest_state->pci_device_state, io->port, &port_off)) {
        case PCI_DEVICE_VIRTIO_BLOCK:
            status = handle_virtio_block_read(vcpu_context->guest_state, port_off, io->access_size,
                                              io_packet);
            if (status != MX_OK)
                return status;
            break;
        default:
            fprintf(stderr, "Unhandled port in %#x\n", io->port);
            return MX_ERR_NOT_SUPPORTED;
        }
    }}

    return vcpu_context->write_state(vcpu_context, &state);
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
    case UART_INTERRUPT_ENABLE_PORT ... UART_LINE_CONTROL_IO_PORT - 1:
    case UART_LINE_CONTROL_IO_PORT + 1 ... UART_SCR_SCRATCH_IO_PORT:
        return MX_OK;
    case UART_LINE_CONTROL_IO_PORT:
        if (io->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->uart_line_control = io->u8;
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
        size_t bit_offset = ((io->port - PCI_CONFIG_ADDRESS_PORT_BASE) * 8);
        size_t bit_size = io->access_size * 8;

        // Clear out the bits we'll be modifying.
        io_port_state->pci_config_address = CLEAR_BITS(
            io_port_state->pci_config_address, bit_size, bit_offset);

        // Write config address.
        switch (io->access_size) {
        case 1:
            io_port_state->pci_config_address |= (io->u8 << bit_offset);
            break;
        case 2:
            io_port_state->pci_config_address |= (io->u16 << bit_offset);
            break;
        case 4:
            io_port_state->pci_config_address |= io->u32;
            break;
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
        return MX_OK;
    }
    case PCI_CONFIG_DATA_PORT_BASE... PCI_CONFIG_DATA_PORT_TOP:
        // TODO(tjdetwiler): Temporarily stub out these ports.
        return MX_OK;
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (io->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->pm1_enable = io->u16;
        return MX_OK;
    }

    uint16_t port_off;
    switch (pci_device(vcpu_context->guest_state->pci_device_state, io->port, &port_off)) {
    case PCI_DEVICE_VIRTIO_BLOCK:
        return handle_virtio_block_write(vcpu_context, port_off, io);
    }

    fprintf(stderr, "Unhandled port out %#x\n", io->port);
    return MX_ERR_NOT_SUPPORTED;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t handle_io(vcpu_context_t* vcpu_context, mx_guest_io_t* io) {
    mtx_lock(&vcpu_context->guest_state->mutex);
    mx_status_t status = io->input ?
                         handle_input(vcpu_context, io) : handle_output(vcpu_context, io);
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

static int serial_loop(void* arg) {
    mx_handle_t* guest = arg;

    mx_handle_t user_fifo;
    mx_handle_t kernel_fifo;
    mx_status_t status = fifo_create(&user_fifo, &kernel_fifo);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create serial FIFO %d\n", status);
        return status;
    }

    struct {
        mx_trap_address_space_t aspace;
        mx_vaddr_t addr;
        size_t len;
        mx_handle_t fifo;
    } trap_args = { MX_TRAP_IO, UART_RECEIVE_IO_PORT, 1, kernel_fifo };
    status = mx_hypervisor_op(*guest, MX_HYPERVISOR_OP_GUEST_SET_TRAP,
                              &trap_args, sizeof(trap_args), NULL, 0);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to set trap for serial FIFO %d\n", status);
        return status;
    }

    uint8_t buffer[UART_BUFFER_SIZE];
    uint16_t offset = 0;
    mx_guest_packet_t packets[PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE];
    while (true) {
        status = fifo_wait(user_fifo, MX_FIFO_READABLE);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to wait for serial FIFO %d\n", status);
            return status;
        }

        uint32_t num_packets;
        status = mx_fifo_read(user_fifo, packets, sizeof(packets), &num_packets);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to read from serial FIFO %d\n", status);
            return status;
        }

        for (uint32_t i = 0; i < num_packets; i++) {
            if (packets[i].type != MX_GUEST_PKT_TYPE_IO) {
                fprintf(stderr, "Invalid packet type for serial FIFO %d\n", packets[i].type);
                return MX_ERR_INTERNAL;
            }
            mx_guest_io_t* io = &packets[i].io;
            if (io->port != UART_RECEIVE_IO_PORT) {
                fprintf(stderr, "Invalid IO port for serial FIFO %#x\n", io->port);
                return MX_ERR_INTERNAL;
            }
            for (int i = 0; i < io->access_size; i++) {
                buffer[offset++] = io->data[i];
                if (offset == UART_BUFFER_SIZE || io->data[i] == '\r') {
                    printf("%.*s", offset, buffer);
                    offset = 0;
                }
            }
        }
    }
}

static mx_status_t vcpu_state_read(vcpu_context_t* vcpu_context,
                                   mx_vcpu_state_t* vcpu_state) {
    return mx_hypervisor_op(vcpu_context->vcpu, MX_HYPERVISOR_OP_VCPU_READ_STATE,
                            NULL, 0, vcpu_state, sizeof(*vcpu_state));
}

static mx_status_t vcpu_state_write(vcpu_context_t* vcpu_context,
                                    mx_vcpu_state_t* vcpu_state) {
    return mx_hypervisor_op(vcpu_context->vcpu, MX_HYPERVISOR_OP_VCPU_WRITE_STATE,
                            vcpu_state, sizeof(*vcpu_state), NULL, 0);
}

void vcpu_init(vcpu_context_t* vcpu_context) {
    memset(vcpu_context, 0, sizeof(*vcpu_context));
    vcpu_context->read_state = &vcpu_state_read;
    vcpu_context->write_state = &vcpu_state_write;
}

mx_status_t vcpu_loop(vcpu_context_t* vcpu_context) {
    thrd_t serial_thread;
    int ret = thrd_create(&serial_thread, serial_loop, &vcpu_context->guest_state->guest);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create serial thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }
    ret = thrd_detach(serial_thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach serial thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }

    while (true) {
        mx_guest_packet_t packet;
        mx_status_t status = mx_hypervisor_op(vcpu_context->vcpu, MX_HYPERVISOR_OP_VCPU_RESUME,
                                              NULL, 0, &packet, sizeof(packet));
        if (status != MX_OK)
            return status;
        status = vcpu_handle_packet(vcpu_context, &packet);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet.type, status);
            return status;
        }
    }
}

mx_status_t vcpu_handle_packet(vcpu_context_t* vcpu_context,
                               mx_guest_packet_t* packet) {
    switch (packet->type) {
    case MX_GUEST_PKT_TYPE_MEMORY:
        return handle_memory(vcpu_context, &packet->memory);
    case MX_GUEST_PKT_TYPE_IO:
        return handle_io(vcpu_context, &packet->io);
    default:
        fprintf(stderr, "Unhandled guest packet %d\n", packet->type);
        return MX_ERR_NOT_SUPPORTED;
    }
}
