// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <hypervisor/address.h>
#include <hypervisor/io_apic.h>
#include <magenta/assert.h>

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

// clang-format on

void io_apic_init(io_apic_t* io_apic) {
    memset(io_apic, 0, sizeof(*io_apic));
}

uint8_t io_apic_redirect(const io_apic_t* io_apic, uint8_t global_irq) {
    mtx_lock((mtx_t*)&io_apic->mutex);
    uint8_t vector = io_apic->redirect[global_irq * 2] & UINT8_MAX;
    mtx_unlock((mtx_t*)&io_apic->mutex);
    return vector;
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

mx_status_t io_apic_handler(io_apic_t* io_apic, const mx_guest_memory_t* memory,
                            const instruction_t* inst) {
    MX_ASSERT(memory->addr >= IO_APIC_PHYS_BASE);
    mx_vaddr_t offset = memory->addr - IO_APIC_PHYS_BASE;

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
