// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <bits.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/ktrace.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <lib/ktrace.h>
#include <platform/timer.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static constexpr uint32_t kGichHcrEn = 1u << 0;
static constexpr uint32_t kGichHcrUie = 1u << 1;
static constexpr uint32_t kGichMisrU = 1u << 1;
static constexpr uint32_t kSpsrDaif = 0b1111 << 6;
static constexpr uint32_t kSpsrEl1h = 0b0101;
static constexpr uint32_t kSpsrNzcv = 0b1111 << 28;

static uint64_t vmpidr_of(uint8_t vpid, uint64_t mpidr) {
    return (vpid - 1) | (mpidr & 0xffffff00fe000000);
}

static void gich_maybe_interrupt(GichState* gich_state) {
    uint64_t elrsr = gich_state->elrsr;
    if (elrsr == 0) {
        // All list registers are in use.
        return;
    }
    uint32_t vector = kTimerVector;
    hypervisor::InterruptType type = gich_state->interrupt_tracker.TryPop(vector);
    if (type != hypervisor::InterruptType::INACTIVE) {
        // We give timer interrupts precedence over all others. If we find a
        // timer interrupt is pending, process it first.
        goto has_timer;
    }
    while (elrsr != 0) {
        type = gich_state->interrupt_tracker.Pop(&vector);
        if (type == hypervisor::InterruptType::INACTIVE) {
            // There are no more pending interrupts.
            break;
        }
    has_timer:
        if (gich_state->active_interrupts.GetOne(vector)) {
            // Skip an interrupt if it was already active.
            continue;
        }
        uint32_t lr_index = __builtin_ctzl(elrsr);
        bool hw = type == hypervisor::InterruptType::PHYSICAL;
        uint64_t lr = gic_get_lr_from_vector(hw, 0, vector);
        gich_state->lr[lr_index] = lr;
        elrsr &= ~(1u << lr_index);
    }
}

static void gich_active_interrupts(GichState* gich_state) {
    gich_state->active_interrupts.ClearAll();
    for (uint32_t i = 0; i < gich_state->num_lrs; i++) {
        if (BIT(gich_state->elrsr, i)) {
            continue;
        }
        uint32_t vector = gic_get_vector_from_lr(gich_state->lr[i]);
        gich_state->active_interrupts.SetOne(vector);
    }
}

static VcpuExit vmexit_interrupt_ktrace_meta(uint32_t misr) {
    if (misr & kGichMisrU) {
        return VCPU_UNDERFLOW_MAINTENANCE_INTERRUPT;
    }
    return VCPU_PHYSICAL_INTERRUPT;
}

AutoGich::AutoGich(GichState* gich_state, uint64_t* curr_hcr)
    : gich_state_(gich_state) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();

    // Load
    gic_write_gich_vmcr(gich_state_->vmcr);
    gic_write_gich_apr(gich_state_->apr);
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        uint64_t lr = gich_state->lr[i];
        gic_write_gich_lr(i, lr);
        if (gic_get_pending_from_lr(lr)) {
            *curr_hcr |= HCR_EL2_VI;
        }
    }
}

AutoGich::~AutoGich() {
    DEBUG_ASSERT(arch_ints_disabled());

    // Save
    gich_state_->vmcr = gic_read_gich_vmcr();
    gich_state_->elrsr = gic_read_gich_elrsr();
    gich_state_->apr = gic_read_gich_apr();
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        gich_state_->lr[i] = !BIT(gich_state_->elrsr, i) ? gic_read_gich_lr(i) : 0;
    }

    arch_enable_ints();
}

zx_status_t El2StatePtr::Alloc() {
    zx_status_t status = page_.Alloc(0);
    if (status != ZX_OK) {
        return status;
    }
    state_ = page_.VirtualAddress<El2State>();
    return ZX_OK;
}

// static
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, fbl::unique_ptr<Vcpu>* out) {
    hypervisor::GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (entry >= gpas->size()) {
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t vpid;
    zx_status_t status = guest->AllocVpid(&vpid);
    if (status != ZX_OK) {
        return status;
    }
    auto auto_call = fbl::MakeAutoCall([guest, vpid]() { guest->FreeVpid(vpid); });

    // For efficiency, we pin the thread to the CPU.
    thread_t* thread = hypervisor::pin_thread(vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    auto_call.cancel();

    status = vcpu->gich_state_.interrupt_tracker.Init();
    if (status != ZX_OK) {
        return status;
    }

    status = vcpu->el2_state_.Alloc();
    if (status != ZX_OK) {
        return status;
    }

    gic_write_gich_hcr(kGichHcrEn);
    vcpu->gich_state_.active_interrupts.Reset(kNumInterrupts);
    vcpu->gich_state_.num_lrs = gic_get_num_lrs();
    vcpu->gich_state_.vmcr = gic_default_gich_vmcr();
    vcpu->gich_state_.elrsr = 0;
    vcpu->gich_state_.apr = 0;
    vcpu->el2_state_->guest_state.system_state.elr_el2 = entry;
    vcpu->el2_state_->guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);
    vcpu->el2_state_->guest_state.system_state.vmpidr_el2 = vmpidr_of(vpid, mpidr);
    vcpu->el2_state_->host_state.system_state.vmpidr_el2 = mpidr;
    vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_DC | HCR_EL2_TWI |
                 HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM | HCR_EL2_RW;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint8_t vpid, const thread_t* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), running_(false) {}

Vcpu::~Vcpu() {
    __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
    DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    const ArchVmAspace& aspace = *guest_->AddressSpace()->arch_aspace();
    zx_paddr_t vttbr = arm64_vttbr(aspace.arch_asid(), aspace.arch_table_phys());
    GuestState* guest_state = &el2_state_->guest_state;
    zx_status_t status;
    do {
        timer_maybe_interrupt(guest_state, &gich_state_);
        gich_maybe_interrupt(&gich_state_);
        uint64_t curr_hcr = hcr_;
        uint32_t misr = 0;
        {
            AutoGich auto_gich(&gich_state_, &curr_hcr);

            // Underflow maintenance interrupt is signalled if there is one or no free LRs.
            // We use it in case when there is not enough free LRs to inject all pending
            // interrupts, so when guest finishes processing most of them, a maintenance
            // interrupt will cause VM exit and will give us a chance to inject the remaining
            // interrupts. The point of this is to reduce latency when processing interrupts.
            uint32_t gich_hcr = 0;
            if (gich_state_.interrupt_tracker.Pending() && gich_state_.num_lrs > 1) {
                gich_hcr = gic_read_gich_hcr() | kGichHcrUie;
                gic_write_gich_hcr(gich_hcr);
            }

            ktrace(TAG_VCPU_ENTER, 0, 0, 0, 0);
            running_.store(true);
            status = arm64_el2_resume(vttbr, el2_state_.PhysicalAddress(), curr_hcr);
            running_.store(false);

            // If we enabled underflow interrupt before we entered the guest we disable it
            // to deassert it in case it is signalled. For details please refer to ARM Generic
            // Interrupt Controller, Architecture Specification, 5.3.4 Maintenance Interrupt
            // Status Register, GICH_MISR. Description of U bit in GICH_MISR.
            if (gich_hcr & kGichHcrUie) {
                misr = gic_read_gich_misr();
                gic_write_gich_hcr(gich_hcr & ~kGichHcrUie);
            }
        }
        gich_active_interrupts(&gich_state_);
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt. If it was due to the thread
            // being killed, then we should exit with an error, otherwise return
            // to the guest.
            ktrace_vcpu_exit(vmexit_interrupt_ktrace_meta(misr),
                             guest_state->system_state.elr_el2);
            status = thread_->signals & THREAD_SIGNAL_KILL ? ZX_ERR_CANCELED : ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&hcr_, guest_state, &gich_state_, guest_->AddressSpace(),
                                    guest_->Traps(), packet);
        } else {
            ktrace_vcpu_exit(VCPU_FAILURE, guest_state->system_state.elr_el2);
            dprintf(INFO, "VCPU resume failed: %d\n", status);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

cpu_mask_t Vcpu::Interrupt(uint32_t vector, hypervisor::InterruptType type) {
    bool signaled = false;
    gich_state_.interrupt_tracker.Interrupt(vector, type, &signaled);
    if (signaled || !running_.load()) {
        return 0;
    }
    return cpu_num_to_mask(hypervisor::cpu_of(vpid_));
}

void Vcpu::VirtualInterrupt(uint32_t vector) {
    cpu_mask_t mask = Interrupt(vector, hypervisor::InterruptType::VIRTUAL);
    if (mask != 0) {
        mp_interrupt(MP_IPI_TARGET_MASK, mask);
    }
}

zx_status_t Vcpu::ReadState(uint32_t kind, void* buf, size_t len) const {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
        return ZX_ERR_BAD_STATE;
    } else if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto state = static_cast<zx_vcpu_state_t*>(buf);
    memcpy(state->x, el2_state_->guest_state.x, sizeof(uint64_t) * GS_NUM_REGS);
    state->sp = el2_state_->guest_state.system_state.sp_el1;
    state->cpsr = el2_state_->guest_state.system_state.spsr_el2 & kSpsrNzcv;
    return ZX_OK;
}

zx_status_t Vcpu::WriteState(uint32_t kind, const void* buf, size_t len) {
    if (!hypervisor::check_pinned_cpu_invariant(vpid_, thread_)) {
        return ZX_ERR_BAD_STATE;
    } else if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    auto state = static_cast<const zx_vcpu_state_t*>(buf);
    memcpy(el2_state_->guest_state.x, state->x, sizeof(uint64_t) * GS_NUM_REGS);
    el2_state_->guest_state.system_state.sp_el1 = state->sp;
    el2_state_->guest_state.system_state.spsr_el2 |= state->cpsr & kSpsrNzcv;
    return ZX_OK;
}
