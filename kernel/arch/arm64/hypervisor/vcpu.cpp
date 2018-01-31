// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform.h>

#include <arch/hypervisor.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_hw_interface.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/event.h>
#include <kernel/mp.h>
#include <platform/timer.h>
#include <vm/pmm.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static constexpr uint32_t kSpsrDaif = 0b1111 << 6;
static constexpr uint32_t kSpsrEl1h = 0b0101;
static constexpr uint32_t kSpsrNzcv = 0b1111 << 28;

uint64_t vmpidr_of(uint8_t vpid, uint64_t mpidr) {
    return (vpid - 1) | (mpidr & 0xffffff00fe000000);
}

// static
zx_status_t Vcpu::Create(Guest* guest, zx_vaddr_t entry, fbl::unique_ptr<Vcpu>* out) {
    GuestPhysicalAddressSpace* gpas = guest->AddressSpace();
    if (entry >= gpas->size())
        return ZX_ERR_INVALID_ARGS;

    uint8_t vpid;
    zx_status_t status = guest->AllocVpid(&vpid);
    if (status != ZX_OK)
        return status;
    auto auto_call = fbl::MakeAutoCall([guest, vpid]() { guest->FreeVpid(vpid); });

    // For efficiency, we pin the thread to the CPU.
    thread_t* thread = pin_thread(vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(guest, vpid, thread));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto_call.cancel();

    timer_init(&vcpu->gich_state_.timer);
    status = vcpu->gich_state_.interrupt_tracker.Init();
    if (status != ZX_OK)
        return status;

    gic_write_gich_hcr(GICH_HCR_EN);
    vcpu->gich_state_.active_interrupts.Reset(kNumInterrupts);
    vcpu->gich_state_.num_lrs = (gic_read_gich_vtr() & GICH_VTR_LIST_REGS_MASK) + 1;
    vcpu->gich_state_.elrs = (1 << vcpu->gich_state_.num_lrs) - 1;
    vcpu->el2_state_.guest_state.system_state.elr_el2 = entry;
    vcpu->el2_state_.guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
    uint64_t mpidr = ARM64_READ_SYSREG(mpidr_el1);
    vcpu->el2_state_.guest_state.system_state.vmpidr_el2 = vmpidr_of(vpid, mpidr);
    vcpu->el2_state_.host_state.system_state.vmpidr_el2 = mpidr;
    vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO | HCR_EL2_DC |
                 HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM | HCR_EL2_RW;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(Guest* guest, uint8_t vpid, const thread_t* thread)
    : guest_(guest), vpid_(vpid), thread_(thread), running_(false), el2_state_(/* zero-init */) {
    (void)thread_;
}

Vcpu::~Vcpu() {
    __UNUSED zx_status_t status = guest_->FreeVpid(vpid_);
    DEBUG_ASSERT(status == ZX_OK);
}

AutoGich::AutoGich(GichState* gich_state)
    : gich_state_(gich_state) {
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();

    // Load
    gic_write_gich_vmcr(gich_state_->vmcr);
    gic_write_gich_elrs(gich_state_->elrs);
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        gic_write_gich_lr(i, gich_state->lr[i]);
    }
}

AutoGich::~AutoGich() {
    DEBUG_ASSERT(arch_ints_disabled());

    // Save
    gich_state_->vmcr = gic_read_gich_vmcr();
    gich_state_->elrs = gic_read_gich_elrs();
    for (uint32_t i = 0; i < gich_state_->num_lrs; i++) {
        gich_state_->lr[i] = gic_read_gich_lr(i);
    }
    arch_enable_ints();
}

static void gich_active_interrupts(InterruptBitmap* active_interrupts) {
    active_interrupts->ClearAll();
    uint32_t lr_limit = __builtin_ctzl(gic_read_gich_elrs());
    for (uint32_t i = 0; i < lr_limit; i++) {
        uint32_t vector = gic_read_gich_lr(i) & GICH_LR_VIRTUAL_ID_MASK;
        active_interrupts->SetOne(vector);
    }
}

static bool gich_maybe_interrupt(GuestState* guest_state, GichState* gich_state) {
    uint64_t prev_elrs = gic_read_gich_elrs();
    uint64_t elrs = prev_elrs;
    while (elrs != 0) {
        uint32_t vector;
        zx_status_t status = gich_state->interrupt_tracker.Pop(&vector);
        if (status != ZX_OK) {
            break;
        }
        uint32_t lr_index = __builtin_ctzl(elrs);
        elrs &= ~(1u << lr_index);
        if (gich_state->active_interrupts.GetOne(vector)) {
            continue;
        }
        uint32_t lr = GICH_LR_PENDING | (vector & GICH_LR_VIRTUAL_ID_MASK);
        gic_write_gich_lr(lr_index, lr);
    }
    return elrs != prev_elrs;
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    zx_paddr_t vttbr = arm64_vttbr(guest_->Vmid(), guest_->AddressSpace()->table_phys());
    zx_paddr_t state = vaddr_to_paddr(&el2_state_);
    GuestState* guest_state = &el2_state_.guest_state;
    zx_status_t status;
    do {
        {
            AutoGich auto_gich(&gich_state_);
            uint64_t curr_hcr = hcr_;
            if (gich_maybe_interrupt(guest_state, &gich_state_)) {
                curr_hcr |= HCR_EL2_VI;
            }
            running_.store(true);
            status = arm64_el2_resume(vttbr, state, curr_hcr);
            running_.store(false);
            gich_active_interrupts(&gich_state_.active_interrupts);
        }
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt, return to the guest.
            status = ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&hcr_, guest_state, &gich_state_, guest_->AddressSpace(),
                                    guest_->Traps(), packet);
        } else {
            dprintf(INFO, "VCPU resume failed: %d\n", status);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
    bool signaled = false;
    zx_status_t status = gich_state_.interrupt_tracker.Interrupt(vector, &signaled);
    if (status != ZX_OK) {
        return status;
    } else if (!signaled && running_.load()) {
        mp_reschedule(MP_IPI_TARGET_MASK, cpu_num_to_mask(cpu_of(vpid_)), 0);
    }
    return ZX_OK;
}

zx_status_t Vcpu::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    if (!check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t))
        return ZX_ERR_INVALID_ARGS;

    auto state = static_cast<zx_vcpu_state_t*>(buffer);
    memcpy(state->x, el2_state_.guest_state.x, sizeof(uint64_t) * GS_NUM_REGS);
    state->sp = el2_state_.guest_state.system_state.sp_el1;
    state->cpsr = el2_state_.guest_state.system_state.spsr_el2 & kSpsrNzcv;
    return ZX_OK;
}

zx_status_t Vcpu::WriteState(uint32_t kind, const void* buffer, uint32_t len) {
    if (!check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t))
        return ZX_ERR_INVALID_ARGS;

    auto state = static_cast<const zx_vcpu_state_t*>(buffer);
    memcpy(el2_state_.guest_state.x, state->x, sizeof(uint64_t) * GS_NUM_REGS);
    el2_state_.guest_state.system_state.sp_el1 = state->sp;
    el2_state_.guest_state.system_state.spsr_el2 |= state->cpsr & kSpsrNzcv;
    return ZX_OK;
}
