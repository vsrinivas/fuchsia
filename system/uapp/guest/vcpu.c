// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <hypervisor/acpi.h>
#include <hypervisor/decode.h>
#include <hypervisor/ports.h>
#include <hw/pci.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>
#include <virtio/block.h>
#include <virtio/virtio.h>

#if __x86_64__
#include <acpica/acpi.h>
#include <acpica/actypes.h>
#endif // __x86_64__

#include "constants.h"
#include "vcpu.h"

/* Memory-mapped device physical addresses. */
#define LOCAL_APIC_PHYS_BASE                    0xfee00000
#define LOCAL_APIC_PHYS_TOP                     (LOCAL_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define IO_APIC_PHYS_BASE                       0xfec00000
#define IO_APIC_PHYS_TOP                        (IO_APIC_PHYS_BASE + PAGE_SIZE - 1)
#define PCI_PHYS_BASE(bus, device, function)    (0xd0000000 + (((bus) << 20) | ((device) << 15) | ((function) << 12)))
#define PCI_PHYS_TOP(bus, device, function)     (PCI_PHYS_BASE(bus, device, function) + 4095)

/* Local APIC register addresses. */
#define LOCAL_APIC_REGISTER_ID                  0x0020
#define LOCAL_APIC_REGISTER_SVR                 0x00f0
#define LOCAL_APIC_REGISTER_ESR                 0x0280
#define LOCAL_APIC_REGISTER_LVT_TIMER           0x0320
#define LOCAL_APIC_REGISTER_LVT_ERROR           0x0370
#define LOCAL_APIC_REGISTER_INITIAL_COUNT       0x0380

/* IO APIC register addresses. */
#define IO_APIC_IOREGSEL                        0x00
#define IO_APIC_IOWIN                           0x10

/* IO APIC register addresses. */
#define IO_APIC_REGISTER_ID                     0x00
#define IO_APIC_REGISTER_VER                    0x01

/* IO APIC configuration constants. */
#define IO_APIC_VERSION                         0x11
#define FIRST_REDIRECT_OFFSET                   0x10
#define LAST_REDIRECT_OFFSET                    (FIRST_REDIRECT_OFFSET + IO_APIC_REDIRECT_OFFSETS - 1)

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
#define RTC_REGISTER_C                          12u
#define RTC_REGISTER_D                          13u

/* RTC register B flags. */
#define RTC_REGISTER_B_DAYLIGHT_SAVINGS         (1u << 0)
#define RTC_REGISTER_B_HOUR_FORMAT              (1u << 1)
#define RTC_REGISTER_B_DATA_MODE                (1u << 2)

/* I8042 status flags. */
#define I8042_STATUS_OUTPUT_FULL                (1u << 0)
#define I8042_STATUS_INPUT_FULL                 (1u << 1)

/* I8042 test constants. */
#define I8042_COMMAND_TEST                      0xaa
#define I8042_DATA_TEST_RESPONSE                0x55

/* PM register addresses. */
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
#define PCI_INTERRUPT_VIRTIO_BLOCK              33u

/* PCI device constants. */
#define PCI_DEVICE_ROOT_COMPLEX                 0u
#define PCI_DEVICE_VIRTIO_BLOCK                 1u
#define PCI_DEVICE_INVALID                      UINT16_MAX

/* PCI macros. */
// From a given address, get the PCI device index.
#define PCI_DEVICE(addr)                        (((addr - PCI_PHYS_BASE(0, 0, 0)) >> 15) & 0x1f)
#define PCI_ALIGN(n)                            ((((uintptr_t)n) + 4095) & ~4095)

static const uint16_t kPciDeviceBarSize[] = {
    0x10,   // PCI_DEVICE_ROOT_COMPLEX
    0x40,   // PCI_DEVICE_VIRTIO_BLOCK
};

static mx_status_t handle_rtc(uint8_t rtc_index, uint8_t* value) {
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm) == NULL)
        return MX_ERR_INTERNAL;
    switch (rtc_index) {
    case RTC_REGISTER_SECONDS:
        *value = tm.tm_sec;
        break;
    case RTC_REGISTER_MINUTES:
        *value = tm.tm_min;
        break;
    case RTC_REGISTER_HOURS:
        *value = tm.tm_hour;
        break;
    case RTC_REGISTER_DAY_OF_MONTH:
        *value = tm.tm_mday;
        break;
    case RTC_REGISTER_MONTH:
        *value = tm.tm_mon;
        break;
    case RTC_REGISTER_YEAR:
        // RTC expects the number of years since 2000.
        *value = tm.tm_year - 100;
        break;
    case RTC_REGISTER_B:
        *value = RTC_REGISTER_B_HOUR_FORMAT | RTC_REGISTER_B_DATA_MODE;
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

static const virtio_blk_config_t block_config = {
    .capacity = (8u << 20) / SECTOR_SIZE,
    .size_max = 0,
    .seg_max = 0,
    .geometry = {
        .cylinders = 0,
        .heads = 0,
        .sectors = 0,
    },
    .blk_size = PAGE_SIZE,
};

static mx_status_t handle_virtio_block_read(uint16_t port, uint8_t* input_size,
                                            mx_guest_port_in_ret_t* port_in_ret) {
    switch (port) {
    case VIRTIO_PCI_QUEUE_SIZE:
        *input_size = 2;
        port_in_ret->u16 = VIRTIO_QUEUE_SIZE;
        return MX_OK;
    case VIRTIO_PCI_DEVICE_STATUS:
        *input_size = 1;
        port_in_ret->u8 = 0;
        return MX_OK;
    case VIRTIO_PCI_ISR_STATUS:
        *input_size = 1;
        port_in_ret->u8 = 1;
        return MX_OK;
    case VIRTIO_PCI_CONFIG_OFFSET_NOMSI ...
         VIRTIO_PCI_CONFIG_OFFSET_NOMSI + sizeof(block_config) - 1: {
        *input_size = 1;
        uint8_t* buf = (uint8_t*)&block_config;
        port_in_ret->u8 = buf[port - VIRTIO_PCI_CONFIG_OFFSET_NOMSI];
        return MX_OK;
    }}

    fprintf(stderr, "Unhandled block read %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_port_in(vcpu_context_t* context, const mx_guest_port_in_t* port_in) {
    uint8_t input_size = 0;
    mx_guest_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = MX_GUEST_PKT_TYPE_PORT_IN;

    mx_status_t status = MX_OK;
    io_port_state_t* io_port_state = &context->guest_state->io_port_state;
    switch (port_in->port) {
    case UART_LINE_CONTROL_IO_PORT:
        input_size = 1;
        packet.port_in_ret.u8 = io_port_state->uart_line_control;
        break;
    case UART_MODEM_CONTROL_IO_PORT:
        input_size = 1;
        packet.port_in_ret.u8 = 0;
        break;
    case UART_LINE_STATUS_IO_PORT:
        input_size = 1;
        packet.port_in_ret.u8 = UART_STATUS_IDLE | UART_STATUS_EMPTY;
        break;
    case RTC_DATA_PORT: {
        input_size = 1;
        status = handle_rtc(io_port_state->rtc_index, &packet.port_in_ret.u8);
        break;
    }
    case I8042_DATA_PORT:
        input_size = 1;
        if (io_port_state->i8042_command == I8042_COMMAND_TEST) {
            packet.port_in_ret.u8 = I8042_DATA_TEST_RESPONSE;
        } else {
            packet.port_in_ret.u8 = 0;
        }
        break;
    case I8042_COMMAND_PORT:
        input_size = 1;
        packet.port_in_ret.u8 = I8042_STATUS_OUTPUT_FULL;
        break;
#if __x86_64__
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        input_size = 2;
        packet.port_in_ret.u16 = io_port_state->pm1_enable;
        break;
#endif // __x86_64__
    default: {
        uint16_t port_off;
        switch (pci_device(context->guest_state->pci_device_state, port_in->port, &port_off)) {
        case PCI_DEVICE_VIRTIO_BLOCK:
            status = handle_virtio_block_read(port_off, &input_size, &packet.port_in_ret);
            break;
        default:
            fprintf(stderr, "Unhandled port in %#x\n", port_in->port);
            return MX_ERR_NOT_SUPPORTED;
        }
    }}

    if (status != MX_OK)
        return status;
    if (port_in->access_size != input_size)
        return MX_ERR_IO_DATA_INTEGRITY;
    uint32_t num_packets;
    return mx_fifo_write(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
}

static uint32_t ring_index(virtio_queue_t* queue, uint32_t index) {
    return index % queue->size;
}

static mx_status_t null_block_device(void* mem_addr, size_t mem_size, virtio_queue_t* queue) {
    for (; queue->index < queue->avail->idx; queue->index++, queue->used->idx++) {
        uint16_t desc_index = queue->avail->ring[ring_index(queue, queue->index)];
        if (desc_index >= queue->size)
            return MX_ERR_OUT_OF_RANGE;
        volatile struct vring_used_elem* used =
            &queue->used->ring[ring_index(queue, queue->used->idx)];
        used->id = desc_index;
        virtio_blk_req_t* req = NULL;
        while (true) {
            struct vring_desc desc = queue->desc[desc_index];
            const uint64_t end = desc.addr + desc.len;
            if (end < desc.addr || end > mem_size)
                return MX_ERR_OUT_OF_RANGE;
            if (req == NULL) {
                // Header.
                if (desc.len != sizeof(virtio_blk_req_t))
                    return MX_ERR_INVALID_ARGS;
                req = mem_addr + desc.addr;
            } else if (desc.flags & VRING_DESC_F_NEXT) {
                // Payload.
                if (req->type == VIRTIO_BLK_T_IN)
                    memset(mem_addr + desc.addr, 0, desc.len);
                used->len += desc.len;
            } else {
                // Status.
                if (desc.len != sizeof(uint8_t))
                    return MX_ERR_INVALID_ARGS;
                uint8_t* status = mem_addr + desc.addr;
                *status = VIRTIO_BLK_S_OK;
                break;
            }
            desc_index = desc.next;
        };
    }
    return MX_OK;
}

static uint8_t irq_redirect(const io_apic_state_t* io_apic_state, uint8_t global_irq) {
    return io_apic_state->redirect[global_irq * 2] & UINT8_MAX;
}

static mx_status_t handle_virtio_block_write(vcpu_context_t* context, uint16_t port,
                                             const mx_guest_port_out_t* port_out) {
    void* mem_addr = context->guest_state->mem_addr;
    size_t mem_size = context->guest_state->mem_size;
    virtio_queue_t* queue = &context->guest_state->pci_device_state[PCI_DEVICE_VIRTIO_BLOCK].queue;
    switch (port) {
    case VIRTIO_PCI_DEVICE_STATUS:
        if (port_out->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_PFN: {
        if (port_out->access_size != 4)
            return MX_ERR_IO_DATA_INTEGRITY;
        queue->desc = mem_addr + (port_out->u32 * PAGE_SIZE);
        queue->avail = (void*)&queue->desc[queue->size];
        queue->used_event = (void*)&queue->avail->ring[queue->size];
        queue->used = (void*)PCI_ALIGN(queue->used_event + sizeof(uint16_t));
        queue->avail_event = (void*)&queue->used->ring[queue->size];
        volatile const void* end = queue->avail_event + 1;
        if (end < (void*)queue->desc || end > mem_addr + mem_size) {
            fprintf(stderr, "Ring is outside of guest memory\n");
            memset(queue, 0, sizeof(virtio_queue_t));
            return MX_ERR_OUT_OF_RANGE;
        }
        return MX_OK;
    }
    case VIRTIO_PCI_QUEUE_SIZE:
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        queue->size = port_out->u16;
        return MX_OK;
    case VIRTIO_PCI_QUEUE_SELECT:
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (port_out->u16 != 0) {
            fprintf(stderr, "Only one queue per device is supported\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        return MX_OK;
    case VIRTIO_PCI_QUEUE_NOTIFY: {
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        if (port_out->u16 != 0) {
            fprintf(stderr, "Only one queue per device is supported\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        mx_status_t status = null_block_device(mem_addr, mem_size, queue);
        if (status != MX_OK)
            return status;
        uint8_t interrupt = irq_redirect(&context->guest_state->io_apic_state,
                                         PCI_INTERRUPT_VIRTIO_BLOCK);
        return mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_INTERRUPT,
                                &interrupt, sizeof(interrupt), NULL, 0);
    }}

    fprintf(stderr, "Unhandled block write %#x\n", port);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_port_out(vcpu_context_t* context, const mx_guest_port_out_t* port_out) {
    io_port_state_t* io_port_state = &context->guest_state->io_port_state;
    switch (port_out->port) {
    case PIC1_PORT ... PIC1_PORT + 1:
    case PIC2_PORT ... PIC2_PORT + 2:
    case I8253_CONTROL_PORT:
    case I8042_DATA_PORT:
    case UART_RECEIVE_IO_PORT + 1 ... UART_LINE_CONTROL_IO_PORT - 1:
    case UART_LINE_CONTROL_IO_PORT + 1 ... UART_SCR_SCRATCH_IO_PORT:
        return MX_OK;
    case UART_RECEIVE_IO_PORT:
        for (int i = 0; i < port_out->access_size; i++) {
            io_port_state->buffer[io_port_state->offset++] = port_out->data[i];
            if (io_port_state->offset == IO_BUFFER_SIZE || port_out->data[i] == '\r') {
                printf("%.*s", io_port_state->offset, io_port_state->buffer);
                io_port_state->offset = 0;
            }
        }
        return MX_OK;
    case UART_LINE_CONTROL_IO_PORT:
        if (port_out->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->uart_line_control = port_out->u8;
        return MX_OK;
    case RTC_INDEX_PORT:
        if (port_out->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->rtc_index = port_out->u8;
        return MX_OK;
    case I8042_COMMAND_PORT:
        if (port_out->access_size != 1)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->i8042_command = port_out->u8;
        return MX_OK;
#if __x86_64__
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
        if (port_out->access_size != 2)
            return MX_ERR_IO_DATA_INTEGRITY;
        io_port_state->pm1_enable = port_out->u16;
        return MX_OK;
#endif // __x86_64__
    }

    uint16_t port_off;
    switch (pci_device(context->guest_state->pci_device_state, port_out->port, &port_off)) {
    case PCI_DEVICE_VIRTIO_BLOCK:
        return handle_virtio_block_write(context, port_off, port_out);
    }

    fprintf(stderr, "Unhandled port out %#x\n", port_out->port);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_local_apic(local_apic_state_t* local_apic_state,
                                     const mx_guest_mem_trap_t* mem_trap, instruction_t* inst) {
    MX_ASSERT(mem_trap->guest_paddr >= LOCAL_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem_trap->guest_paddr - LOCAL_APIC_PHYS_BASE;
    switch (offset) {
    case LOCAL_APIC_REGISTER_ID:
        return inst_read32(inst, 0);
    case LOCAL_APIC_REGISTER_ESR:
        // From Intel Volume 3, Section 10.5.3: Before attempt to read from the
        // ESR, software should first write to it.
        //
        // Therefore, we ignore writes to the ESR.
        if (inst->type == INST_MOV_WRITE)
            return MX_OK;
    case LOCAL_APIC_REGISTER_SVR:
    case LOCAL_APIC_REGISTER_LVT_TIMER:
    case LOCAL_APIC_REGISTER_LVT_ERROR:
        return inst_rw32(inst, local_apic_state->apic_addr + offset);;
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
                                  const mx_guest_mem_trap_t* mem_trap, instruction_t* inst) {
    MX_ASSERT(mem_trap->guest_paddr >= IO_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem_trap->guest_paddr - IO_APIC_PHYS_BASE;
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
        case FIRST_REDIRECT_OFFSET ... LAST_REDIRECT_OFFSET: {
            uint32_t i = io_apic_state->select - FIRST_REDIRECT_OFFSET;
            return inst_rw32(inst, io_apic_state->redirect + i);
        }}
    }

    fprintf(stderr, "Unhandled IO APIC %#lx\n", offset);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t handle_pci_device(pci_device_state_t* pci_device_state,
                                     const mx_guest_mem_trap_t* mem_trap, instruction_t* inst,
                                     uint8_t device, uint16_t vendor_id, uint16_t device_id) {
    MX_ASSERT(mem_trap->guest_paddr >= PCI_PHYS_BASE(0, device, 0));
    mx_vaddr_t offset = mem_trap->guest_paddr - PCI_PHYS_BASE(0, device, 0);
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
    case PCI_CONFIG_REVISION_ID:
    case PCI_CONFIG_CLASS_CODE:
    case PCI_CONFIG_CLASS_CODE_SUB:
    case PCI_CONFIG_CLASS_CODE_BASE:
    case PCI_CONFIG_CAPABILITIES:
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

static mx_status_t unhandled_mem_trap(const mx_guest_mem_trap_t* mem_trap, instruction_t* inst) {
    fprintf(stderr, "Unhandled address %#lx\n", mem_trap->guest_paddr);
    if (inst->type == INST_MOV_READ)
        *inst->reg = UINT64_MAX;
    return MX_OK;
}

static mx_status_t handle_mem_trap(vcpu_context_t* context, const mx_guest_mem_trap_t* mem_trap) {
    mx_guest_gpr_t guest_gpr;
    mx_status_t status = mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_GET_GPR,
                                          NULL, 0, &guest_gpr, sizeof(guest_gpr));
    if (status != MX_OK)
        return status;

    instruction_t inst;
#if __aarch64__
    status = MX_ERR_NOT_SUPPORTED;
#elif __x86_64__
    status = inst_decode(mem_trap->instruction_buffer, mem_trap->instruction_length, &guest_gpr,
                         &inst);
#else
#error Unsupported architecture
#endif

    if (status != MX_OK) {
        fprintf(stderr, "Unsupported instruction:");
#if __x86_64__
        for (uint8_t i = 0; i < mem_trap->instruction_length; i++)
            fprintf(stderr, " %x", mem_trap->instruction_buffer[i]);
#endif // __x86_64__
        fprintf(stderr, "\n");
    } else {
        status = MX_ERR_UNAVAILABLE;
        guest_state_t* guest_state = context->guest_state;
        switch (mem_trap->guest_paddr) {
        case LOCAL_APIC_PHYS_BASE ... LOCAL_APIC_PHYS_TOP:
            status = handle_local_apic(&context->local_apic_state, mem_trap, &inst);
            break;
        case IO_APIC_PHYS_BASE ... IO_APIC_PHYS_TOP:
            mtx_lock(&guest_state->mutex);
            status = handle_io_apic(&guest_state->io_apic_state, mem_trap, &inst);
            mtx_unlock(&guest_state->mutex);
            break;
        case PCI_PHYS_BASE(0, PCI_DEVICE_ROOT_COMPLEX, 0) ...
             PCI_PHYS_TOP(0, PCI_DEVICE_ROOT_COMPLEX, 0): {
            mtx_lock(&guest_state->mutex);
            pci_device_state_t* pci_device_state =
                &guest_state->pci_device_state[PCI_DEVICE_ROOT_COMPLEX];
            status = handle_pci_device(pci_device_state, mem_trap, &inst, PCI_DEVICE_ROOT_COMPLEX,
                                       PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_Q35);
            mtx_unlock(&guest_state->mutex);
            break;
        }
        case PCI_PHYS_BASE(0, PCI_DEVICE_VIRTIO_BLOCK, 0) ...
             PCI_PHYS_TOP(0, PCI_DEVICE_VIRTIO_BLOCK, 0): {
            mtx_lock(&guest_state->mutex);
            pci_device_state_t* pci_device_state =
                &guest_state->pci_device_state[PCI_DEVICE_VIRTIO_BLOCK];
            status = handle_pci_device(pci_device_state, mem_trap, &inst, PCI_DEVICE_VIRTIO_BLOCK,
                                       PCI_VENDOR_ID_VIRTIO, PCI_DEVICE_ID_VIRTIO_BLOCK);
            mtx_unlock(&guest_state->mutex);
            break;
        }
        default:
            status = unhandled_mem_trap(mem_trap, &inst);
            break;
        }
    }

    mx_guest_packet_t packet;
    packet.type = MX_GUEST_PKT_TYPE_MEM_TRAP;
    packet.mem_trap_ret.fault = status != MX_OK;

    // If there was an attempt to read or test memory, update the GPRs.
    if (status == MX_OK && (inst.type == INST_MOV_READ || inst.type == INST_TEST)) {
        status = mx_hypervisor_op(context->guest, MX_HYPERVISOR_OP_GUEST_SET_GPR,
                                  &guest_gpr, sizeof(guest_gpr), NULL, 0);
        if (status != MX_OK)
            return status;
    }
    uint32_t num_packets;
    return mx_fifo_write(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
}

static mx_status_t vcpu_wait(mx_handle_t vcpu_fifo, mx_signals_t signals) {
    mx_signals_t observed = 0;
    while (!(observed & signals)) {
        mx_status_t status = mx_object_wait_one(vcpu_fifo, signals | MX_FIFO_PEER_CLOSED,
                                                MX_TIME_INFINITE, &observed);
        if (status != MX_OK)
            return status;
        if (observed & MX_FIFO_PEER_CLOSED)
            return MX_ERR_PEER_CLOSED;
    }
    return MX_OK;
}

mx_status_t vcpu_loop(vcpu_context_t* context) {
    mx_guest_packet_t packet[PAGE_SIZE / MX_GUEST_MAX_PKT_SIZE];
    while (true) {
        mx_status_t status = vcpu_wait(context->vcpu_fifo, MX_FIFO_READABLE);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to wait for readable on the VCPU: %d\n", status);
            return status;
        }

        uint32_t num_packets;
        status = mx_fifo_read(context->vcpu_fifo, &packet, sizeof(packet), &num_packets);
        if (status != MX_OK)
            return status;

        for (uint32_t i = 0; i < num_packets; i++) {
            mx_status_t status;
            switch (packet[i].type) {
            case MX_GUEST_PKT_TYPE_PORT_IN:
                mtx_lock(&context->guest_state->mutex);
                status = handle_port_in(context, &packet[i].port_in);
                mtx_unlock(&context->guest_state->mutex);
                break;
            case MX_GUEST_PKT_TYPE_PORT_OUT:
                mtx_lock(&context->guest_state->mutex);
                status = handle_port_out(context, &packet[i].port_out);
                mtx_unlock(&context->guest_state->mutex);
                break;
            case MX_GUEST_PKT_TYPE_MEM_TRAP:
                status = handle_mem_trap(context, &packet[i].mem_trap);
                break;
            default:
                fprintf(stderr, "Unhandled guest packet %d\n", packet[i].type);
                return MX_ERR_NOT_SUPPORTED;
            }
            if (status != MX_OK) {
                fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet[i].type, status);
                return status;
            }
        }
    }
}
