// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hypervisor/x86/local_apic.h>

#include <fbl/auto_lock.h>
#include <hypervisor/address.h>
#include <hypervisor/bits.h>
#include <hypervisor/guest.h>
#include <hypervisor/vcpu.h>
#include <zircon/assert.h>
#include <zx/time.h>

#include <limits.h>

// clang-format off

// Local APIC memory range.
static const uint64_t kLocalApicPhysBase        = 0xfee00000;
static const uint64_t kLocalApicSize            = PAGE_SIZE;

// Local APIC register addresses.
static const uint64_t kLocalApicId              = 0x020;
static const uint64_t kLocalApicVersion         = 0x030;
static const uint16_t kLocalApicEoi             = 0x0b0;
static const uint64_t kLocalApicLdr             = 0x0d0;
static const uint64_t kLocalApicDfr             = 0x0e0;
static const uint64_t kLocalApicSvr             = 0x0f0;
static const uint64_t kLocalApicIsr_31_0        = 0x100;
static const uint64_t kLocalApicIsr_255_224     = 0x170;
static const uint64_t kLocalApicTmr_31_0        = 0x180;
static const uint64_t kLocalApicTmr_255_224     = 0x1f0;
static const uint64_t kLocalApicIrr_31_0        = 0x200;
static const uint64_t kLocalApicIrr_255_224     = 0x270;
static const uint64_t kLocalApicEsr             = 0x280;
static const uint64_t kLocalApicLvtCmci         = 0x2f0;
static const uint64_t kLocalApicIcr_31_0        = 0x300;
static const uint64_t kLocalApicIcr_63_32       = 0x310;
static const uint64_t kLocalApicLvtTimer        = 0x320;
static const uint64_t kLocalApicLvtThermal      = 0x330;
static const uint64_t kLocalApicLvtPerfmon      = 0x340;
static const uint64_t kLocalApicLvtLint0        = 0x350;
static const uint64_t kLocalApicLvtLint1        = 0x360;
static const uint64_t kLocalApicLvtError        = 0x370;
static const uint64_t kLocalApicInitialCount    = 0x380;
static const uint64_t kLocalApicCurrentCount    = 0x390;
static const uint64_t kLocalApicDivideConfig    = 0x3e0;

// clang-format on

LocalApicTimer::LocalApicTimer(LocalApic* apic)
    : apic_(apic) {
    interrupt_.set_handler([this](async_t* async, zx_status_t status) {
        Interrupt();
        return ASYNC_TASK_FINISHED;
    });
    loop_.StartThread("LocalApicTimer");
}

LocalApicTimer::~LocalApicTimer() {
    // Destructor is not strictly nesessary, but instead of depending on C++
    // destruction order lets just shutdown the event loop explicitly.
    loop_.Shutdown();
}

zx_status_t LocalApicTimer::WriteLvt(uint32_t value) {
    fbl::AutoLock lock(&mutex_);
    vector_ = bits_shift(value, 7, 0);
    masked_ = bits_shift(value, 16, 16);

    uint32_t mode = bits_shift(value, 18, 17);
    // Return error for reserved value.
    if (mode > static_cast<uint32_t>(Mode::TscDeadline))
        return ZX_ERR_NOT_SUPPORTED;
    mode_ = static_cast<LocalApicTimer::Mode>(mode);
    UpdateLocked(zx_clock_get(ZX_CLOCK_MONOTONIC));
    return ZX_OK;
}

uint32_t LocalApicTimer::ReadLvt() const {
    fbl::AutoLock lock(&mutex_);
    return (static_cast<uint32_t>(mode_) << 17) |
           (static_cast<uint32_t>(masked_) << 16) |
           vector_;
}

zx_status_t LocalApicTimer::WriteDcr(uint32_t value) {
    // There is no mention of what should happen if someone update divisor
    // while APIC timer is still running Thus make the simplest thing possible:
    // update the divisor and call UpdateLocked to adjust async task deadline.
    fbl::AutoLock lock(&mutex_);
    uint32_t shift = bits_shift(value, 1, 0) | (bit_shift(value, 3) << 2);
    divisor_shift_ = (shift + 1) & 7;
    UpdateLocked(zx_clock_get(ZX_CLOCK_MONOTONIC));
    return ZX_OK;
}

uint32_t LocalApicTimer::ReadDcr() const {
    fbl::AutoLock lock(&mutex_);
    uint32_t shift = (divisor_shift_ - 1) & 7;
    return bits_shift(shift, 1, 0) | (bit_shift(shift, 2) << 3);
}

zx_status_t LocalApicTimer::WriteIcr(uint32_t value) {
    fbl::AutoLock lock(&mutex_);
    reset_time_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
    initial_count_ = value;
    UpdateLocked(reset_time_);
    return ZX_OK;
}

uint32_t LocalApicTimer::ReadIcr() const {
    fbl::AutoLock lock(&mutex_);
    return initial_count_;
}

uint32_t LocalApicTimer::ReadCcr() const {
    fbl::AutoLock lock(&mutex_);
    uint64_t elapsed = zx_clock_get(ZX_CLOCK_MONOTONIC) - reset_time_;
    uint64_t ticks = elapsed >> divisor_shift_;

    switch (mode_) {
    case Mode::OneShot:
        if (ticks >= initial_count_)
            return 0;
        return static_cast<uint32_t>(initial_count_ - ticks);
    case Mode::Periodic:
        return static_cast<uint32_t>(initial_count_ - (ticks % (initial_count_ + 1)));
    case Mode::TscDeadline:
        // We don't support TscDeadline mode.
        break;
    }
    return 0;
}

void LocalApicTimer::UpdateLocked(zx_time_t now) {
    interrupt_.Cancel(loop_.async());
    expire_time_ = 0;

    if (masked_ || !initial_count_)
        return;

    uint64_t ticks = (now - reset_time_) >> divisor_shift_;
    uint64_t remain = 0;

    switch (mode_) {
    case Mode::OneShot:
        if (ticks >= initial_count_)
            return;
        remain = initial_count_ - ticks;
        break;
    case Mode::Periodic:
        remain = initial_count_ - (ticks % initial_count_);
        break;
    case Mode::TscDeadline:
        // We don't support TscDeadline mode.
        break;
    }

    expire_time_ = now + (remain << divisor_shift_);
    interrupt_.set_deadline(expire_time_);
    interrupt_.Post(loop_.async());
}

void LocalApicTimer::Interrupt() {
    fbl::AutoLock lock(&mutex_);
    if (!expire_time_)
        return;

    UpdateLocked(zx_clock_get(ZX_CLOCK_MONOTONIC));
    apic_->Interrupt(vector_);
}

// From Intel Volume 3, Section 10.4.1.: All 32-bit registers should be
// accessed using 128-bit aligned 32-bit loads or stores. Some processors
// may support loads and stores of less than 32 bits to some of the APIC
// registers. This is model specific behavior and is not guaranteed to work
// on all processors.
union Register {
    volatile uint32_t u32;
} __PACKED __ALIGNED(16);

// Local APIC register map.
struct LocalApic::Registers {
    Register reserved0[2];

    Register id;      // Read/Write.
    Register version; // Read Only.

    Register reserved1[4];

    Register tpr;       // Read/Write.
    Register apr;       // Read Only.
    Register ppr;       // Read Only.
    Register eoi;       // Write Only.
    Register rrd;       // Read Only.
    Register ldr;       // Read/Write.
    Register dfr;       // Read/Write.
    Register isr[8];    // Read Only.
    Register tmr[8];    // Read Only.
    Register irr[8];    // Read Only.
    Register esr;       // Read Only.

    Register reserved2[6];

    Register lvt_cmci;  // Read/Write.
} __PACKED;

static_assert(__offsetof(LocalApic::Registers, id) == kLocalApicId, "");
static_assert(__offsetof(LocalApic::Registers, ldr) == kLocalApicLdr, "");
static_assert(__offsetof(LocalApic::Registers, dfr) == kLocalApicDfr, "");

LocalApic::LocalApic(Vcpu* vcpu, uintptr_t apic_addr)
    : vcpu_(vcpu), registers_(reinterpret_cast<Registers*>(apic_addr)), timer_(this) {}

zx_status_t LocalApic::Init(Guest* guest) {
    return guest->CreateMapping(TrapType::MMIO_SYNC, kLocalApicPhysBase, kLocalApicSize, 0,
                                this);
}

zx_status_t LocalApic::Interrupt(uint32_t vector) {
    return vcpu_->Interrupt(vector);
}

zx_status_t LocalApic::Read(uint64_t addr, IoValue* value) const {
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
    case kLocalApicLvtTimer:
        value->u32 = timer_.ReadLvt();
        return ZX_OK;
    case kLocalApicInitialCount:
        value->u32 = timer_.ReadIcr();
        return ZX_OK;
    case kLocalApicCurrentCount:
        value->u32 = timer_.ReadCcr();
        return ZX_OK;
    case kLocalApicDivideConfig:
        value->u32 = timer_.ReadDcr();
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
    case kLocalApicSvr: {
        fbl::AutoLock lock(&mutex_);
        uintptr_t reg = reinterpret_cast<uintptr_t>(registers_) + addr;
        *reinterpret_cast<volatile uint32_t*>(reg) = value.u32;
        return ZX_OK;
    }
    case kLocalApicLvtTimer:
        {
            // Update the APIC page since the TSC timer emulation in the kernel
            // depends on reading this value out of the APIC page.
            fbl::AutoLock lock(&mutex_);
            uintptr_t reg = reinterpret_cast<uintptr_t>(registers_) + addr;
            *reinterpret_cast<volatile uint32_t*>(reg) = value.u32;
        }
        return timer_.WriteLvt(value.u32);
    case kLocalApicInitialCount:
        return timer_.WriteIcr(value.u32);
    case kLocalApicDivideConfig:
        return timer_.WriteDcr(value.u32);
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
