// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/io_apic.h>
#include <hypervisor/local_apic.h>
#include <hypervisor/vcpu.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/hypervisor.h>

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
#define LAST_REDIRECT_OFFSET            (FIRST_REDIRECT_OFFSET + IoApic::kNumRedirectOffsets - 1)

/* DESTMOD register. */
#define IO_APIC_DESTMOD_PHYSICAL        0x00
#define IO_APIC_DESTMOD_LOGICAL         0x01

#define LOCAL_APIC_DFR_FLAT_MODEL       0xf

// clang-format on

zx_status_t IoApic::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, IO_APIC_PHYS_BASE, IO_APIC_SIZE, 0, this);
}

zx_status_t IoApic::RegisterLocalApic(uint8_t local_apic_id, LocalApic* local_apic) {
    if (local_apic_id >= kMaxLocalApics)
        return ZX_ERR_OUT_OF_RANGE;
    if (local_apic_[local_apic_id] != nullptr)
        return ZX_ERR_ALREADY_EXISTS;

    local_apic->set_id(local_apic_id);
    local_apic_[local_apic_id] = local_apic;
    return ZX_OK;
}

zx_status_t IoApic::Redirect(uint32_t global_irq, uint8_t* out_vector,
                             zx_handle_t* out_vcpu) const {
    if (global_irq >= kNumRedirects)
        return ZX_ERR_OUT_OF_RANGE;

    RedirectEntry entry;
    {
        fbl::AutoLock lock(&mutex_);
        entry = redirect_[global_irq];
    }

    uint32_t vector = bits_shift(entry.lower, 7, 0);

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
    uint8_t destmod = BIT_SHIFT(entry.lower, 11);
    if (destmod == IO_APIC_DESTMOD_PHYSICAL) {
        uint32_t dest = bits_shift(entry.upper, 27, 24);
        LocalApic* apic = dest < kMaxLocalApics ? local_apic_[dest] : nullptr;
        if (apic == nullptr)
            return ZX_ERR_NOT_FOUND;
        *out_vector = static_cast<uint8_t>(vector);
        *out_vcpu = apic->vcpu();
        return ZX_OK;
    }

    // Logical DESTMOD.
    uint32_t dest = bits_shift(entry.upper, 31, 24);
    for (uint8_t local_apic_id = 0; local_apic_id < kMaxLocalApics; ++local_apic_id) {
        LocalApic* local_apic = local_apic_[local_apic_id];
        if (local_apic == nullptr)
            continue;

        // Intel Volume 3, Section 10.6.2.2: Each local APIC performs a
        // bit-wise AND of the MDA and its logical APIC ID.
        uint32_t logical_apic_id = bits_shift(local_apic->ldr(), 31, 24);
        if (!(logical_apic_id & dest))
            continue;

        // There also exists a 'cluster' model that is not implemented.
        uint32_t model = bits_shift(local_apic->dfr(), 31, 28);
        if (model != LOCAL_APIC_DFR_FLAT_MODEL) {
            fprintf(stderr, "APIC only supports the flat model.\n");
            return ZX_ERR_NOT_SUPPORTED;
        }

        // Note we're not currently respecting the DELMODE field and
        // instead are only delivering to the fist local APIC that is
        // targeted.
        *out_vector = static_cast<uint8_t>(vector);
        *out_vcpu = local_apic->vcpu();
        return ZX_OK;
    }
    return ZX_ERR_NOT_FOUND;
}

zx_status_t IoApic::SetRedirect(uint32_t global_irq, RedirectEntry& redirect) {
    if (global_irq >= kNumRedirects)
        return ZX_ERR_OUT_OF_RANGE;
    fbl::AutoLock lock(&mutex_);
    redirect_[global_irq] = redirect;
    return ZX_OK;
}

zx_status_t IoApic::Interrupt(uint32_t global_irq) const {
    uint8_t vector;
    zx_handle_t vcpu;
    zx_status_t status = Redirect(global_irq, &vector, &vcpu);
    if (status != ZX_OK)
        return status;
    return zx_vcpu_interrupt(vcpu, vector);
}

zx_status_t IoApic::Read(uint64_t addr, IoValue* value) {
    switch (addr) {
    case IO_APIC_IOREGSEL: {
        fbl::AutoLock lock(&mutex_);
        value->u32 = select_;
        return ZX_OK;
    }
    case IO_APIC_IOWIN: {
        uint32_t select_register;
        {
            fbl::AutoLock lock(&mutex_);
            select_register = select_;
        }
        return ReadRegister(select_register, value);
    }
    default:
        fprintf(stderr, "Unhandled IO APIC address %#lx\n", addr);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t IoApic::Write(uint64_t addr, const IoValue& value) {
    switch (addr) {
    case IO_APIC_IOREGSEL: {
        if (value.u32 > UINT8_MAX)
            return ZX_ERR_INVALID_ARGS;
        fbl::AutoLock lock(&mutex_);
        select_ = value.u32;
        return ZX_OK;
    }
    case IO_APIC_IOWIN: {
        uint32_t select_register;
        {
            fbl::AutoLock lock(&mutex_);
            select_register = select_;
        }
        return WriteRegister(select_register, value);
    }
    default:
        fprintf(stderr, "Unhandled IO APIC address %#lx\n", addr);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t IoApic::ReadRegister(uint32_t select_register, IoValue* value) {
    switch (select_register) {
    case IO_APIC_REGISTER_ID: {
        fbl::AutoLock lock(&mutex_);
        value->u32 = id_;
        return ZX_OK;
    }
    case IO_APIC_REGISTER_VER:
        // There are two redirect offsets per redirection entry. We return
        // the maximum redirection entry index.
        //
        // From Intel 82093AA, Section 3.2.2.
        value->u32 = (kNumRedirects - 1) << 16 | IO_APIC_VERSION;
        return ZX_OK;
    case IO_APIC_REGISTER_ARBITRATION:
        // Since we have a single I/O APIC, it is always the winner
        // of arbitration and its arbitration register is always 0.
        value->u32 = 0;
        return ZX_OK;
    case FIRST_REDIRECT_OFFSET... LAST_REDIRECT_OFFSET: {
        fbl::AutoLock lock(&mutex_);
        uint32_t redirect_offset = select_ - FIRST_REDIRECT_OFFSET;
        RedirectEntry& entry = redirect_[redirect_offset / 2];
        uint32_t redirect_register = redirect_offset % 2 == 0 ? entry.lower : entry.upper;
        value->u32 = redirect_register;
        return ZX_OK;
    }
    default:
        fprintf(stderr, "Unhandled IO APIC register %#x\n", select_register);
        return ZX_ERR_NOT_SUPPORTED;
    }
}

zx_status_t IoApic::WriteRegister(uint32_t select_register, const IoValue& value) {
    switch (select_register) {
    case IO_APIC_REGISTER_ID: {
        fbl::AutoLock lock(&mutex_);
        id_ = value.u32;
        return ZX_OK;
    }
    case FIRST_REDIRECT_OFFSET... LAST_REDIRECT_OFFSET: {
        fbl::AutoLock lock(&mutex_);
        uint32_t redirect_offset = select_ - FIRST_REDIRECT_OFFSET;
        RedirectEntry& entry = redirect_[redirect_offset / 2];
        uint32_t* redirect_register = redirect_offset % 2 == 0 ? &entry.lower : &entry.upper;
        *redirect_register = value.u32;
        return ZX_OK;
    }
    case IO_APIC_REGISTER_VER:
    case IO_APIC_REGISTER_ARBITRATION:
        // Read-only, ignore writes.
        return ZX_OK;
    default:
        fprintf(stderr, "Unhandled IO APIC register %#x\n", select_register);
        return ZX_ERR_NOT_SUPPORTED;
    }
}
