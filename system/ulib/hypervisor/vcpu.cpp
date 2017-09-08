// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

#include <hw/pci.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/block.h>
#include <hypervisor/decode.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/io_port.h>
#include <hypervisor/pci.h>
#include <hypervisor/uart.h>
#include <hypervisor/vcpu.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <fbl/unique_ptr.h>

#include "acpi_priv.h"

// clang-format off

/* Local APIC register addresses. */
#define LOCAL_APIC_REGISTER_ID              0x0020
#define LOCAL_APIC_REGISTER_VERSION         0x0030
#define LOCAL_APIC_REGISTER_LDR             0x00d0
#define LOCAL_APIC_REGISTER_DFR             0x00e0
#define LOCAL_APIC_REGISTER_SVR             0x00f0
#define LOCAL_APIC_REGISTER_ISR_31_0        0x0100
#define LOCAL_APIC_REGISTER_ISR_255_224     0x0170
#define LOCAL_APIC_REGISTER_TMR_31_0        0x0180
#define LOCAL_APIC_REGISTER_TMR_255_224     0x01f0
#define LOCAL_APIC_REGISTER_IRR_31_0        0x0200
#define LOCAL_APIC_REGISTER_IRR_255_224     0x0270
#define LOCAL_APIC_REGISTER_ESR             0x0280
#define LOCAL_APIC_REGISTER_ICR_31_0        0x0300
#define LOCAL_APIC_REGISTER_ICR_63_32       0x0310
#define LOCAL_APIC_REGISTER_LVT_TIMER       0x0320
#define LOCAL_APIC_REGISTER_LVT_THERMAL     0x0330
#define LOCAL_APIC_REGISTER_LVT_PERFMON     0x0340
#define LOCAL_APIC_REGISTER_LVT_LINT0       0x0350
#define LOCAL_APIC_REGISTER_LVT_LINT1       0x0360
#define LOCAL_APIC_REGISTER_LVT_ERROR       0x0370
#define LOCAL_APIC_REGISTER_INITIAL_COUNT   0x0380

/* Interrupt vectors. */
#define X86_INT_GP_FAULT                    13u

// clang-format on

static mx_status_t handle_local_apic(local_apic_t* local_apic, const mx_packet_guest_mem_t* mem,
                                     instruction_t* inst) {
    MX_ASSERT(mem->addr >= LOCAL_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem->addr - LOCAL_APIC_PHYS_BASE;
    // From Intel Volume 3, Section 10.4.1.: All 32-bit registers should be
    // accessed using 128-bit aligned 32-bit loads or stores. Some processors
    // may support loads and stores of less than 32 bits to some of the APIC
    // registers. This is model specific behavior and is not guaranteed to work
    // on all processors.
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
    case LOCAL_APIC_REGISTER_TMR_31_0 ... LOCAL_APIC_REGISTER_TMR_255_224:
    case LOCAL_APIC_REGISTER_IRR_31_0 ... LOCAL_APIC_REGISTER_IRR_255_224:
        return inst_read32(inst, 0);
    case LOCAL_APIC_REGISTER_ID: {
        // The IO APIC implementation currently assumes these won't change.
        if (inst->type == INST_MOV_WRITE && inst_val32(inst) != local_apic->regs->id.u32) {
            fprintf(stderr, "Changing APIC IDs is not supported.\n");
            return MX_ERR_NOT_SUPPORTED;
        }
        uintptr_t addr = reinterpret_cast<uintptr_t>(local_apic->apic_addr) + offset;
        return inst_rw32(inst, reinterpret_cast<uint32_t*>(addr));
    }
    case LOCAL_APIC_REGISTER_DFR:
    case LOCAL_APIC_REGISTER_ICR_31_0 ... LOCAL_APIC_REGISTER_ICR_63_32:
    case LOCAL_APIC_REGISTER_LDR:
    case LOCAL_APIC_REGISTER_LVT_ERROR:
    case LOCAL_APIC_REGISTER_LVT_LINT0:
    case LOCAL_APIC_REGISTER_LVT_LINT1:
    case LOCAL_APIC_REGISTER_LVT_PERFMON:
    case LOCAL_APIC_REGISTER_LVT_THERMAL:
    case LOCAL_APIC_REGISTER_LVT_TIMER:
    case LOCAL_APIC_REGISTER_SVR: {
        uintptr_t addr = reinterpret_cast<uintptr_t>(local_apic->apic_addr) + offset;
        return inst_rw32(inst, reinterpret_cast<uint32_t*>(addr));
    }
    case LOCAL_APIC_REGISTER_INITIAL_COUNT: {
        uint32_t initial_count;
        mx_status_t status = inst_write32(inst, &initial_count);
        if (status != MX_OK)
            return status;
        return initial_count > 0 ? MX_ERR_NOT_SUPPORTED : MX_OK;
    }
    }

    fprintf(stderr, "Unhandled local APIC address %#lx\n", offset);
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t unhandled_mem(const mx_packet_guest_mem_t* mem, const instruction_t* inst) {
    fprintf(stderr, "Unhandled address %#lx\n", mem->addr);
    if (inst->type == INST_MOV_READ)
        *inst->reg = UINT64_MAX;
    return MX_OK;
}

static mx_status_t handle_mmio_read(vcpu_ctx_t* vcpu_ctx, mx_vaddr_t addr, uint8_t access_size,
                                    mx_vcpu_io_t* io) {
    switch (addr) {
    case PCI_ECAM_PHYS_BASE ... PCI_ECAM_PHYS_TOP:
        return pci_ecam_read(vcpu_ctx->guest_ctx->bus, addr, access_size, io);
    default:
        return MX_ERR_NOT_FOUND;
    }
}

static mx_status_t handle_mmio_write(vcpu_ctx_t* vcpu_ctx, mx_vaddr_t addr, mx_vcpu_io_t* io) {
    switch (addr) {
    case PCI_ECAM_PHYS_BASE ... PCI_ECAM_PHYS_TOP:
        return pci_ecam_write(vcpu_ctx->guest_ctx->bus, addr, io);
    default:
        return MX_ERR_NOT_FOUND;
    }
}

static mx_status_t handle_mmio(vcpu_ctx_t* vcpu_ctx, const mx_packet_guest_mem_t* mem, const instruction_t* inst) {
    mx_status_t status;
    mx_vcpu_io_t mmio;
    if (inst->type == INST_MOV_WRITE) {
        switch (inst->mem) {
        case 2:
            status = inst_write16(inst, &mmio.u16);
            break;
        case 4:
            status = inst_write32(inst, &mmio.u32);
            break;
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
        if (status != MX_OK)
            return status;
        mmio.access_size = inst->mem;
        return handle_mmio_write(vcpu_ctx, mem->addr, &mmio);
    }

    if (inst->type == INST_MOV_READ) {
        status = handle_mmio_read(vcpu_ctx, mem->addr, inst->mem, &mmio);
        if (status != MX_OK)
            return status;
        switch (inst->mem) {
        case 1:
            return inst_read8(inst, mmio.u8);
        case 2:
            return inst_read16(inst, mmio.u16);
        case 4:
            return inst_read32(inst, mmio.u32);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }

    if (inst->type == INST_TEST) {
        status = handle_mmio_read(vcpu_ctx, mem->addr, inst->mem, &mmio);
        if (status != MX_OK)
            return status;
        switch (inst->mem) {
        case 1:
            return inst_test8(inst, static_cast<uint8_t>(inst->imm), mmio.u8);
        default:
            return MX_ERR_NOT_SUPPORTED;
        }
    }

    return MX_ERR_INVALID_ARGS;
}

static mx_status_t handle_mem(vcpu_ctx_t* vcpu_ctx, const mx_packet_guest_mem_t* mem) {
    mx_vcpu_state_t vcpu_state;
    mx_status_t status = vcpu_ctx->read_state(vcpu_ctx, MX_VCPU_STATE, &vcpu_state,
                                              sizeof(vcpu_state));
    if (status != MX_OK)
        return status;

    instruction_t inst;
#if __aarch64__
    status = MX_ERR_NOT_SUPPORTED;
#elif __x86_64__
    status = inst_decode(mem->inst_buf, mem->inst_len, &vcpu_state, &inst);
#else
#error Unsupported architecture
#endif

    if (status != MX_OK) {
        fprintf(stderr, "Unsupported instruction:");
#if __x86_64__
        for (uint8_t i = 0; i < mem->inst_len; i++)
            fprintf(stderr, " %x", mem->inst_buf[i]);
#endif // __x86_64__
        fprintf(stderr, "\n");
    } else {
        guest_ctx_t* guest_ctx = vcpu_ctx->guest_ctx;
        switch (mem->addr) {
        case LOCAL_APIC_PHYS_BASE ... LOCAL_APIC_PHYS_TOP:
            status = handle_local_apic(&vcpu_ctx->local_apic, mem, &inst);
            break;
        case IO_APIC_PHYS_BASE ... IO_APIC_PHYS_TOP:
            status = io_apic_handler(guest_ctx->io_apic, mem, &inst);
            break;
        default: {
            status = handle_mmio(vcpu_ctx, mem, &inst);
            if (status == MX_ERR_NOT_FOUND)
                status = unhandled_mem(mem, &inst);
            break;
        }
    }
    }

    if (status != MX_OK) {
        return mx_vcpu_interrupt(vcpu_ctx->vcpu, X86_INT_GP_FAULT);
    } else if (inst.type == INST_MOV_READ || inst.type == INST_TEST) {
        // If there was an attempt to read or test memory, update the GPRs.
        return vcpu_ctx->write_state(vcpu_ctx, MX_VCPU_STATE, &vcpu_state,
                                     sizeof(vcpu_state));
    }
    return status;
}

static mx_status_t handle_input(vcpu_ctx_t* vcpu_ctx, const mx_packet_guest_io_t* io) {
#if __x86_64__
    mx_status_t status = MX_OK;
    mx_vcpu_io_t vcpu_io;
    memset(&vcpu_io, 0, sizeof(vcpu_io));
    switch (io->port) {
    case I8042_COMMAND_PORT:
    case I8042_DATA_PORT:
    case PIC1_DATA_PORT:
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
    case RTC_DATA_PORT:
        status = io_port_read(vcpu_ctx->guest_ctx->io_port, io->port, &vcpu_io);
        break;
    case UART_RECEIVE_PORT ... UART_SCR_SCRATCH_PORT:
        status = uart_read(vcpu_ctx->guest_ctx->uart, io->port, &vcpu_io);
        break;
    case PCI_CONFIG_ADDRESS_PORT_BASE ... PCI_CONFIG_ADDRESS_PORT_TOP:
    case PCI_CONFIG_DATA_PORT_BASE ... PCI_CONFIG_DATA_PORT_TOP:
        status = pci_bus_read(vcpu_ctx->guest_ctx->bus, io->port, io->access_size, &vcpu_io);
        break;
    default: {
        uint8_t bar;
        uint16_t port_off;
        pci_bus_t* bus = vcpu_ctx->guest_ctx->bus;
        pci_device_t* pci_device = pci_mapped_device(bus, PCI_BAR_IO_TYPE_PIO, io->port, &bar,
                                                     &port_off);
        if (pci_device) {
            status = pci_device->ops->read_bar(pci_device, bar, port_off, io->access_size,
                                               &vcpu_io);
            break;
        }
        status = MX_ERR_NOT_SUPPORTED;
    }
    }
    if (status != MX_OK) {
        fprintf(stderr, "Unhandled port in %#x: %d\n", io->port, status);
        return status;
    }
    if (vcpu_io.access_size != io->access_size) {
        fprintf(stderr, "Unexpected size (%u != %u) for port in %#x\n", vcpu_io.access_size,
                io->access_size, io->port);
        return MX_ERR_IO_DATA_INTEGRITY;
    }
    return vcpu_ctx->write_state(vcpu_ctx, MX_VCPU_IO, &vcpu_io, sizeof(vcpu_io));
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t handle_output(vcpu_ctx_t* vcpu_ctx, const mx_packet_guest_io_t* io) {
#if __x86_64__
    switch (io->port) {
    case I8042_COMMAND_PORT:
    case I8042_DATA_PORT:
    case I8253_CHANNEL_0:
    case I8253_CONTROL_PORT:
    case PIC1_COMMAND_PORT ... PIC1_DATA_PORT:
    case PIC2_COMMAND_PORT ... PIC2_DATA_PORT:
    case PM1_EVENT_PORT + PM1A_REGISTER_ENABLE:
    case PM1_EVENT_PORT + PM1A_REGISTER_STATUS:
    case RTC_INDEX_PORT:
        return io_port_write(vcpu_ctx->guest_ctx->io_port, io);
    case PCI_CONFIG_ADDRESS_PORT_BASE ... PCI_CONFIG_ADDRESS_PORT_TOP:
    case PCI_CONFIG_DATA_PORT_BASE ... PCI_CONFIG_DATA_PORT_TOP:
        return pci_bus_write(vcpu_ctx->guest_ctx->bus, io);
    case UART_INTERRUPT_ENABLE_PORT ... UART_SCR_SCRATCH_PORT:
        return uart_write(vcpu_ctx->guest_ctx->uart, io);
    }
    fprintf(stderr, "Unhandled port out %#x\n", io->port);
    return MX_ERR_NOT_SUPPORTED;
#else // __x86_64__
    return MX_ERR_NOT_SUPPORTED;
#endif // __x86_64__
}

static mx_status_t handle_io(vcpu_ctx_t* vcpu_ctx, const mx_packet_guest_io_t* io) {
    return io->input ? handle_input(vcpu_ctx, io) : handle_output(vcpu_ctx, io);
}

static mx_status_t vcpu_state_read(vcpu_ctx_t* vcpu_ctx, uint32_t kind, void* buffer,
                                   uint32_t len) {
    return mx_vcpu_read_state(vcpu_ctx->vcpu, kind, buffer, len);
}

static mx_status_t vcpu_state_write(vcpu_ctx_t* vcpu_ctx, uint32_t kind, const void* buffer,
                                    uint32_t len) {
    return mx_vcpu_write_state(vcpu_ctx->vcpu, kind, buffer, len);
}

void vcpu_init(vcpu_ctx_t* vcpu_ctx) {
    memset(vcpu_ctx, 0, sizeof(*vcpu_ctx));
    vcpu_ctx->read_state = vcpu_state_read;
    vcpu_ctx->write_state = vcpu_state_write;
}

mx_status_t vcpu_loop(vcpu_ctx_t* vcpu_ctx) {
    while (true) {
        mx_port_packet_t packet;
        mx_status_t status = mx_vcpu_resume(vcpu_ctx->vcpu, &packet);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to resume VCPU %d\n", status);
            return status;
        }
        status = vcpu_packet_handler(vcpu_ctx, &packet);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to handle guest packet %d: %d\n", packet.type, status);
            return status;
        }
    }
}

mx_status_t vcpu_packet_handler(vcpu_ctx_t* vcpu_ctx, mx_port_packet_t* packet) {
    switch (packet->type) {
    case MX_PKT_TYPE_GUEST_MEM:
        return handle_mem(vcpu_ctx, &packet->guest_mem);
    case MX_PKT_TYPE_GUEST_IO:
        return handle_io(vcpu_ctx, &packet->guest_io);
    default:
        fprintf(stderr, "Unhandled guest packet %d\n", packet->type);
        return MX_ERR_NOT_SUPPORTED;
    }
}

typedef struct device {
    mx_handle_t port;
    device_handler_fn_t handler;
    void* ctx;
} device_t;

static int device_loop(void* ctx) {
    fbl::unique_ptr<device_t> device(static_cast<device_t*>(ctx));

    while (true) {
        mx_port_packet_t packet;
        mx_status_t status = mx_port_wait(device->port, MX_TIME_INFINITE, &packet, 0);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to wait for device port %d\n", status);
            break;
        }

        status = device->handler(&packet, device->ctx);
        if (status != MX_OK) {
            fprintf(stderr, "Unable to handle packet for device %d\n", status);
            break;
        }
    }

    mx_handle_close(device->port);
    return MX_ERR_INTERNAL;
}

mx_status_t device_async(mx_handle_t guest, const trap_args_t* traps, size_t num_traps,
                         device_handler_fn_t handler, void* ctx) {
    if (num_traps == 0)
        return MX_ERR_INVALID_ARGS;

    int ret;
    auto device = new device_t{MX_HANDLE_INVALID, handler, ctx};

    mx_status_t status = mx_port_create(0, &device->port);
    if (status != MX_OK) {
        fprintf(stderr, "Failed to create device port %d\n", status);
        goto mem_cleanup;
    }

    for (size_t i = 0; i < num_traps; ++i) {
        const trap_args_t* trap = &traps[i];

        status = mx_guest_set_trap(guest, trap->kind, trap->addr, trap->len, device->port,
                                   trap->key);
        if (status != MX_OK) {
            fprintf(stderr, "Failed to set trap for device port %d\n", status);
            goto cleanup;
        }
    }

    thrd_t thread;
    ret = thrd_create(&thread, device_loop, device);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to create device thread %d\n", ret);
        goto cleanup;
    }

    ret = thrd_detach(thread);
    if (ret != thrd_success) {
        fprintf(stderr, "Failed to detach device thread %d\n", ret);
        return MX_ERR_INTERNAL;
    }

    return MX_OK;
cleanup:
    mx_handle_close(device->port);
mem_cleanup:
    delete device;
    return MX_ERR_INTERNAL;
}
