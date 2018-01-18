// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <platform.h>

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <dev/timer/arm_generic.h>
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

static constexpr uint32_t kSpsrIrq = 0b0010 << 6;
static constexpr uint32_t kSpsrDaif = 0b1111 << 6;
static constexpr uint32_t kSpsrEl1h = 0b0101;
static constexpr uint32_t kSpsrNzcv = 0b1111 << 28;
static constexpr uint32_t kTimerVector = 27;

enum TimerControl : uint64_t {
    ENABLE = 1u << 0,
    IMASK = 1u << 1,
};

// static
zx_status_t Vcpu::Create(zx_vaddr_t entry, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
                         TrapMap* traps, fbl::unique_ptr<Vcpu>* out) {
    uint8_t vpid;
    zx_status_t status = alloc_vpid(&vpid);
    if (status != ZX_OK)
        return status;
    auto auto_call = fbl::MakeAutoCall([vpid]() { free_vpid(vpid); });

    // For efficiency, we pin the thread to the CPU.
    thread_t* thread = pin_thread(vpid);

    fbl::AllocChecker ac;
    fbl::unique_ptr<Vcpu> vcpu(new (&ac) Vcpu(vmid, vpid, thread, gpas, traps));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;
    auto_call.cancel();

    timer_init(&vcpu->gich_state_.timer);
    status = vcpu->gich_state_.interrupt_tracker.Init();
    if (status != ZX_OK)
        return status;

    uint32_t hcr_ = gic_read_gich_hcr();
    gic_write_gich_hcr(hcr_ | kGichHcrEn);

    vcpu->gich_state_.num_lrs = (gic_read_gich_vtr() & kGichVtrListRegs) + 1;
    vcpu->gich_state_.elrs = (1 << vcpu->gich_state_.num_lrs) - 1;
    vcpu->el2_state_.guest_state.system_state.elr_el2 = entry;
    vcpu->el2_state_.guest_state.system_state.spsr_el2 = kSpsrDaif | kSpsrEl1h;
    vcpu->hcr_ = HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO | HCR_EL2_DC |
                 HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM | HCR_EL2_RW;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(uint8_t vmid, uint8_t vpid, const thread_t* thread, GuestPhysicalAddressSpace* gpas,
           TrapMap* traps)
    : vmid_(vmid), vpid_(vpid), thread_(thread), running_(false), gpas_(gpas), traps_(traps),
      el2_state_(/* zero-init */) {
    (void)thread_;
}

Vcpu::~Vcpu() {
    __UNUSED zx_status_t status = free_vpid(vpid_);
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

static bool gich_maybe_interrupt(GuestState* guest_state, GichState* gich_state) {
    if (guest_state->system_state.spsr_el2 & kSpsrIrq) {
        return false;
    }
    uint64_t prev_elrs = gic_read_gich_elrs();
    uint64_t elrs = prev_elrs;
    while (elrs != 0) {
        uint32_t vector;
        zx_status_t status = gich_state->interrupt_tracker.Pop(&vector);
        if (status != ZX_OK) {
            break;
        }
        size_t i = __builtin_ctzl(elrs);
        gic_write_gich_lr((uint32_t)i, kGichLrPending | vector);
        elrs &= ~(1u << i);
    }
    return elrs != prev_elrs;
}

static void deadline_callback(timer_t* timer, zx_time_t now, void* arg) {
    auto vcpu = static_cast<Vcpu*>(arg);
    vcpu->Interrupt(kTimerVector);
}

static zx_status_t timer_maybe_set(GuestState* guest_state, GichState* gich_state, Vcpu* vcpu) {
    bool enabled = guest_state->cntv_ctl_el0 & TimerControl::ENABLE;
    bool masked = guest_state->cntv_ctl_el0 & TimerControl::IMASK;
    if (!enabled || masked) {
        return ZX_OK;
    }
    timer_cancel(&gich_state->timer);

    uint64_t cntpct_deadline = guest_state->cntv_cval_el0;
    zx_time_t deadline = cntpct_to_zx_time(cntpct_deadline);
    if (deadline <= current_time()) {
        return gich_state->interrupt_tracker.Track(kTimerVector);
    }
    timer_set_oneshot(&gich_state->timer, deadline, deadline_callback, vcpu);
    return ZX_OK;
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!check_pinned_cpu_invariant(vpid_, thread_))
        return ZX_ERR_BAD_STATE;
    zx_paddr_t vttbr = arm64_vttbr(vmid_, gpas_->table_phys());
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
        }
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt, return to the guest.
            status = ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&hcr_, guest_state, &gich_state_, gpas_, traps_, packet);
        } else {
            dprintf(INFO, "VCPU resume failed: %d\n", status);
        }
        if (status == ZX_OK) {
            status = timer_maybe_set(guest_state, &gich_state_, this);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

zx_status_t Vcpu::Interrupt(uint32_t vector) {
    zx_status_t status = gich_state_.interrupt_tracker.Track(vector);
    if (status != ZX_OK) {
        return status;
    }
    if (running_.load()) {
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

zx_status_t arm_vcpu_create(zx_vaddr_t entry, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
                            TrapMap* traps, fbl::unique_ptr<Vcpu>* out) {
    return Vcpu::Create(entry, vmid, gpas, traps, out);
}

zx_status_t arch_vcpu_resume(Vcpu* vcpu, zx_port_packet_t* packet) {
    return vcpu->Resume(packet);
}

zx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt) {
    return vcpu->Interrupt(interrupt);
}

zx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len) {
    return vcpu->ReadState(kind, buffer, len);
}

zx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len) {
    return vcpu->WriteState(kind, buffer, len);
}
