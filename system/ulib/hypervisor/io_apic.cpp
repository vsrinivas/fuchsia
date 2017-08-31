// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/vcpu.h>
#include <magenta/assert.h>
#include <magenta/syscalls.h>
#include <magenta/syscalls/hypervisor.h>

// clang-format off

/* IO APIC register addresses. */
#define IO_APIC_IOREGSEL                0x00
#define IO_APIC_IOWIN                   0x10

/* IO APIC register addresses. */
#define IO_APIC_REGISTER_ID             0x00
#define IO_APIC_REGISTER_VER            0x01
#define IO_APIC_REGISTER_ARBITRATION    0x02

/* IO APIC configuration constants. */
#define IO_APIC_VERSION                 0x11
#define FIRST_REDIRECT_OFFSET           0x10
#define LAST_REDIRECT_OFFSET            (FIRST_REDIRECT_OFFSET + IO_APIC_REDIRECT_OFFSETS - 1)

/* DESTMOD register. */
#define IO_APIC_DESTMOD_PHYSICAL        0x00
#define IO_APIC_DESTMOD_LOGICAL         0x01

#define LOCAL_APIC_DFR_FLAT_MODEL       0xf

// clang-format on

void io_apic_init(io_apic_t* io_apic) {
    memset(io_apic, 0, sizeof(*io_apic));
}

mx_status_t io_apic_register_local_apic(io_apic_t* io_apic, uint8_t local_apic_id,
                                        local_apic_t* local_apic) {
    if (local_apic_id >= IO_APIC_MAX_LOCAL_APICS)
        return MX_ERR_OUT_OF_RANGE;
    if (io_apic->local_apic[local_apic_id] != NULL)
        return MX_ERR_ALREADY_EXISTS;

    local_apic->regs->id.u32 = local_apic_id;
    io_apic->local_apic[local_apic_id] = local_apic;
    return MX_OK;
}

mx_status_t io_apic_redirect(const io_apic_t* io_apic, uint32_t global_irq, uint8_t* out_vector,
                             mx_handle_t* out_vcpu) {
    if (global_irq >= IO_APIC_REDIRECTS)
        return MX_ERR_OUT_OF_RANGE;

    mtx_lock((mtx_t*)&io_apic->mutex);
    uint32_t lower = io_apic->redirect[global_irq * 2];
    uint32_t upper = io_apic->redirect[global_irq * 2 + 1];
    mtx_unlock((mtx_t*)&io_apic->mutex);

    uint32_t vector = bits_shift(lower, 7, 0);

    // The "destination mode" (DESTMOD) determines how the dest field in the
    // redirection entry should be interpreted.
    //
    // With a 'physical' mode, the destination is interpreted as the APIC ID
    // of the target APIC to receive the interrupt.
    //
    // With a 'logical' mode, the target depends on the 'logical destination
    // register' and the 'destination format register' in the connected local
    // APICs.
    //
    // See 82093AA (IOAPIC) Section 2.3.4.
    // See Intel Volume 3, Section 10.6.2.
    uint8_t destmod = BIT_SHIFT(lower, 11);
    if (destmod == IO_APIC_DESTMOD_PHYSICAL) {
        uint32_t dest = bits_shift(upper, 27, 24);
        local_apic_t* apic = dest < IO_APIC_MAX_LOCAL_APICS ? io_apic->local_apic[dest] : NULL;
        if (apic == NULL)
            return MX_ERR_NOT_FOUND;
        *out_vector = static_cast<uint8_t>(vector);
        *out_vcpu = apic->vcpu;
        return MX_OK;
    }

    // Logical DESTMOD.
    uint32_t dest = bits_shift(upper, 31, 24);
    for (uint8_t local_apic_id = 0; local_apic_id < IO_APIC_MAX_LOCAL_APICS; ++local_apic_id) {
        local_apic_t* local_apic = io_apic->local_apic[local_apic_id];
        if (local_apic == NULL)
            continue;

        // Intel Volume 3, Section 10.6.2.2: Each local APIC performs a
        // bit-wise AND of the MDA and its logical APIC ID.
        uint32_t logical_apic_id = bits_shift(local_apic->regs->ldr.u32, 31, 24);
        if (!(logical_apic_id & dest))
            continue;

        // There also exists a 'cluster' model that is not implemented.
        uint32_t model = bits_shift(local_apic->regs->dfr.u32, 31, 28);
        if (model != LOCAL_APIC_DFR_FLAT_MODEL) {
            fprintf(stderr, "APIC only supports the flat model.\n");
            return MX_ERR_NOT_SUPPORTED;
        }

        // Note we're not currently respecting the DELMODE field and
        // instead are only delivering to the fist local APIC that is
        // targeted.
        *out_vector = static_cast<uint8_t>(vector);
        *out_vcpu = local_apic->vcpu;
        return MX_OK;
    }
    return MX_ERR_NOT_FOUND;
}

mx_status_t io_apic_interrupt(const io_apic_t* io_apic, uint32_t global_irq) {
    uint8_t vector;
    mx_handle_t vcpu;
    mx_status_t status = io_apic_redirect(io_apic, global_irq, &vector, &vcpu);
    if (status != MX_OK)
        return status;
    return mx_vcpu_interrupt(vcpu, vector);
}

static mx_status_t io_apic_register_handler(io_apic_t* io_apic, const instruction_t* inst) {
    switch (io_apic->select) {
    case IO_APIC_REGISTER_ID:
        return inst_rw32(inst, &io_apic->id);
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
        uint32_t i = io_apic->select - FIRST_REDIRECT_OFFSET;
        return inst_rw32(inst, io_apic->redirect + i);
    }
    default:
        fprintf(stderr, "Unhandled IO APIC register %#x\n", io_apic->select);
        return MX_ERR_NOT_SUPPORTED;
    }
}

mx_status_t io_apic_handler(io_apic_t* io_apic, const mx_packet_guest_mem_t* mem,
                            const instruction_t* inst) {
    MX_ASSERT(mem->addr >= IO_APIC_PHYS_BASE);
    mx_vaddr_t offset = mem->addr - IO_APIC_PHYS_BASE;

    switch (offset) {
    case IO_APIC_IOREGSEL: {
        uint32_t select;
        mx_status_t status = inst_write32(inst, &select);
        if (status != MX_OK)
            return status;
        mtx_lock((mtx_t*)&io_apic->mutex);
        io_apic->select = select;
        mtx_unlock((mtx_t*)&io_apic->mutex);
        return select > UINT8_MAX ? MX_ERR_INVALID_ARGS : MX_OK;
    }
    case IO_APIC_IOWIN: {
        mtx_lock((mtx_t*)&io_apic->mutex);
        mx_status_t status = io_apic_register_handler(io_apic, inst);
        mtx_unlock((mtx_t*)&io_apic->mutex);
        return status;
    }
    default:
        fprintf(stderr, "Unhandled IO APIC address %#lx\n", offset);
        return MX_ERR_NOT_SUPPORTED;
    }
}
