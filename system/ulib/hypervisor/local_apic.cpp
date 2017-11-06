// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/local_apic.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/guest.h>
#include <zircon/assert.h>

// clang-format off

/* Local APIC register addresses. */
static const uint64_t kLocalApicId              = 0x0020;
static const uint64_t kLocalApicVersion         = 0x0030;
static const uint16_t kLocalApicEoi             = 0x00b0;
static const uint64_t kLocalApicLdr             = 0x00d0;
static const uint64_t kLocalApicDfr             = 0x00e0;
static const uint64_t kLocalApicSvr             = 0x00f0;
static const uint64_t kLocalApicIsr_31_0        = 0x0100;
static const uint64_t kLocalApicIsr_255_224     = 0x0170;
static const uint64_t kLocalApicTmr_31_0        = 0x0180;
static const uint64_t kLocalApicTmr_255_224     = 0x01f0;
static const uint64_t kLocalApicIrr_31_0        = 0x0200;
static const uint64_t kLocalApicIrr_255_224     = 0x0270;
static const uint64_t kLocalApicEsr             = 0x0280;
static const uint64_t kLocalApicLvtCmci         = 0x02f0;
static const uint64_t kLocalApicIcr_31_0        = 0x0300;
static const uint64_t kLocalApicIcr_63_32       = 0x0310;
static const uint64_t kLocalApicLvtTimer        = 0x0320;
static const uint64_t kLocalApicLvtThermal      = 0x0330;
static const uint64_t kLocalApicLvtPerfmon      = 0x0340;
static const uint64_t kLocalApicLvtLint0        = 0x0350;
static const uint64_t kLocalApicLvtLint1        = 0x0360;
static const uint64_t kLocalApicLvtError        = 0x0370;
static const uint64_t kLocalApicInitialCount    = 0x0380;

// clang-format on

static_assert(__offsetof(LocalApic::Registers, id) == kLocalApicId, "");
static_assert(__offsetof(LocalApic::Registers, ldr) == kLocalApicLdr, "");
static_assert(__offsetof(LocalApic::Registers, dfr) == kLocalApicDfr, "");

zx_status_t LocalApic::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, LOCAL_APIC_PHYS_BASE, LOCAL_APIC_SIZE, 0,
                                this);
}

zx_status_t LocalApic::Read(uint64_t addr, IoValue* value) {
    if (addr % sizeof(Register))
        return ZX_ERR_IO_DATA_INTEGRITY;

    switch (addr) {
    case kLocalApicVersion: {
        // From Intel Volume 3, Section 10.4.8.
        //
        // We choose 15H as it causes us to be seen as a modern APIC by Linux,
        // and is the highest non-reserved value.
        const uint32_t version = 0x15;
        const uint32_t max_lvt_entry = 0x6; // LVT entries minus 1.
        const uint32_t eoi_suppression = 0; // Disable support for EOI-broadcast suppression.
        value->u32 = version | (max_lvt_entry << 16) | (eoi_suppression << 24);
        return ZX_OK;
    }
    case kLocalApicDfr:
    case kLocalApicLvtCmci:
    case kLocalApicIcr_31_0... kLocalApicIcr_63_32:
    case kLocalApicId:
    case kLocalApicLdr:
    case kLocalApicLvtError:
    case kLocalApicLvtLint0:
    case kLocalApicLvtLint1:
    case kLocalApicLvtPerfmon:
    case kLocalApicLvtThermal:
    case kLocalApicLvtTimer:
    case kLocalApicSvr: {
        fbl::AutoLock lock(&mutex_);
        uintptr_t reg = reinterpret_cast<uintptr_t>(registers_) + addr;
        value->u32 = *reinterpret_cast<volatile uint32_t*>(reg);
        return ZX_OK;
    }
    case kLocalApicEsr:
    case kLocalApicIsr_31_0... kLocalApicIsr_255_224:
    case kLocalApicTmr_31_0... kLocalApicTmr_255_224:
    case kLocalApicIrr_31_0... kLocalApicIrr_255_224:
        value->u32 = 0;
        return ZX_OK;
    }

    fprintf(stderr, "Unhandled local APIC address %#lx\n", addr);
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t LocalApic::Write(uint64_t addr, const IoValue& value) {
    if (addr % sizeof(Register))
        return ZX_ERR_IO_DATA_INTEGRITY;

    switch (addr) {
    case kLocalApicEoi:
        // fallthrough
    case kLocalApicEsr:
        // From Intel Volume 3, Section 10.5.3: Before attempt to read from the
        // ESR, software should first write to it.
        //
        // Therefore, we ignore writes to the ESR.
        return ZX_OK;
    case kLocalApicId: {
        // The IO APIC implementation currently assumes these won't change.
        fbl::AutoLock lock(&mutex_);
        return value.u32 != registers_->id.u32 ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }
    case kLocalApicDfr:
    case kLocalApicLvtCmci:
    case kLocalApicIcr_31_0... kLocalApicIcr_63_32:
    case kLocalApicLdr:
    case kLocalApicLvtError:
    case kLocalApicLvtLint0:
    case kLocalApicLvtLint1:
    case kLocalApicLvtPerfmon:
    case kLocalApicLvtThermal:
    case kLocalApicLvtTimer:
    case kLocalApicSvr: {
        fbl::AutoLock lock(&mutex_);
        uintptr_t reg = reinterpret_cast<uintptr_t>(registers_) + addr;
        *reinterpret_cast<volatile uint32_t*>(reg) = value.u32;
        return ZX_OK;
    }
    case kLocalApicInitialCount:
        return value.u32 > 0 ? ZX_ERR_NOT_SUPPORTED : ZX_OK;
    }

    fprintf(stderr, "Unhandled local APIC address %#lx\n", addr);
    return ZX_ERR_NOT_SUPPORTED;
}

void LocalApic::set_id(uint32_t id) {
    fbl::AutoLock lock(&mutex_);
    registers_->id.u32 = id;
}

uint32_t LocalApic::ldr() const {
    fbl::AutoLock lock(&mutex_);
    return registers_->ldr.u32;
}

uint32_t LocalApic::dfr() const {
    fbl::AutoLock lock(&mutex_);
    return registers_->dfr.u32;
}
