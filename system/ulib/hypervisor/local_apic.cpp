// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/local_apic.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <zircon/assert.h>

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

// clang-format on
void LocalApic::set_id(uint32_t id) {
    fbl::AutoLock lock(&mutex_);
    regs_->id.u32 = id;
}

uint32_t LocalApic::ldr() const {
    fbl::AutoLock lock(&mutex_);
    return regs_->ldr.u32;
}

uint32_t LocalApic::dfr() const {
    fbl::AutoLock lock(&mutex_);
    return regs_->dfr.u32;
}

zx_status_t LocalApic::Handler(const zx_packet_guest_mem_t* mem, instruction_t* inst) {
    ZX_ASSERT(mem->addr >= LOCAL_APIC_PHYS_BASE);
    zx_vaddr_t offset = mem->addr - LOCAL_APIC_PHYS_BASE;
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
            return ZX_OK;
    case LOCAL_APIC_REGISTER_ISR_31_0 ... LOCAL_APIC_REGISTER_ISR_255_224:
    case LOCAL_APIC_REGISTER_TMR_31_0 ... LOCAL_APIC_REGISTER_TMR_255_224:
    case LOCAL_APIC_REGISTER_IRR_31_0 ... LOCAL_APIC_REGISTER_IRR_255_224:
        return inst_read32(inst, 0);
    case LOCAL_APIC_REGISTER_ID: {
        // The IO APIC implementation currently assumes these won't change.
        fbl::AutoLock lock(&mutex_);
        if (inst->type == INST_MOV_WRITE && inst_val32(inst) != regs_->id.u32) {
            fprintf(stderr, "Changing APIC IDs is not supported.\n");
            return ZX_ERR_NOT_SUPPORTED;
        }
        uintptr_t addr = reinterpret_cast<uintptr_t>(regs_) + offset;
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
        fbl::AutoLock lock(&mutex_);
        uintptr_t addr = reinterpret_cast<uintptr_t>(regs_) + offset;
        return inst_rw32(inst, reinterpret_cast<uint32_t*>(addr));
    }
    case LOCAL_APIC_REGISTER_INITIAL_COUNT: {
        uint32_t initial_count;
        zx_status_t status = inst_write32(inst, &initial_count);
        if (status != ZX_OK)
            return status;
        return initial_count > 0 ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }
    }

    fprintf(stderr, "Unhandled local APIC address %#lx\n", offset);
    return ZX_ERR_NOT_SUPPORTED;
}

