// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <dev/interrupt/arm_gic_regs.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <kernel/mp.h>
#include <platform/timer.h>
#include <vm/pmm.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static const uint32_t kSpsrEl1h = 0b0101;
static const uint32_t kSpsrNzcv = 0b1111 << 28;

static const uint32_t kGichHcrEn = 1u << 0;
static const uint32_t kGichLrPending = 0b01 << 28;

struct GicH {
    volatile uint32_t hcr;
    volatile uint32_t vtr;
    volatile uint32_t vmcr;
    volatile uint32_t reserved0;
    volatile uint32_t misr;
    volatile uint32_t reserved1[3];
    volatile uint64_t eisr;
    volatile uint32_t reserved2[2];
    volatile uint64_t elsr;
    volatile uint32_t reserved3[46];
    volatile uint32_t apr;
    volatile uint32_t reserved4[3];
    volatile uint32_t lr[64];
} __PACKED;

static_assert(__offsetof(GicH, hcr) == 0x00, "");
static_assert(__offsetof(GicH, vtr) == 0x04, "");
static_assert(__offsetof(GicH, vmcr) == 0x08, "");
static_assert(__offsetof(GicH, misr) == 0x10, "");
static_assert(__offsetof(GicH, eisr) == 0x20, "");
static_assert(__offsetof(GicH, elsr) == 0x30, "");
static_assert(__offsetof(GicH, apr) == 0xf0, "");
static_assert(__offsetof(GicH, lr) == 0x100, "");

static zx_status_t get_gich(uint8_t vpid, GicH** gich) {
    // Check for presence of GICv2 virtualisation extensions.
    //
    // TODO(abdulla): Support GICv3 virtualisation.
    if (GICH_OFFSET == 0)
        return ZX_ERR_NOT_SUPPORTED;

    *gich = reinterpret_cast<GicH*>(GICH_ADDRESS + 0x1000 + (vpid << 9));
    return ZX_OK;
}

// static
zx_status_t Vcpu::Create(zx_vaddr_t ip, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
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

    status = get_gich(vpid, &vcpu->gich_);
    if (status != ZX_OK)
        return status;

    vcpu->el2_state_.guest_state.system_state.elr_el2 = ip;
    vcpu->el2_state_.guest_state.system_state.spsr_el2 = kSpsrEl1h;
    vcpu->hcr_.store(HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO |
                     HCR_EL2_DC | HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM |
                     HCR_EL2_RW);
    vcpu->gich_->hcr |= kGichHcrEn;

    *out = fbl::move(vcpu);
    return ZX_OK;
}

Vcpu::Vcpu(uint8_t vmid, uint8_t vpid, const thread_t* thread, GuestPhysicalAddressSpace* gpas,
           TrapMap* traps)
    : vmid_(vmid), vpid_(vpid), thread_(thread), gpas_(gpas), traps_(traps),
    el2_state_(/* zero-init */) {
    (void) thread_;
}

Vcpu::~Vcpu() {
    __UNUSED zx_status_t status = free_vpid(vpid_);
    DEBUG_ASSERT(status == ZX_OK);
}

zx_status_t Vcpu::Resume(zx_port_packet_t* packet) {
    if (!check_pinned_cpu_invariant(thread_, vpid_))
        return ZX_ERR_BAD_STATE;
    zx_paddr_t vttbr = arm64_vttbr(vmid_, gpas_->table_phys());
    zx_paddr_t state = vaddr_to_paddr(&el2_state_);
    zx_status_t status;
    do {
        uint64_t hcr = hcr_.fetch_and(~HCR_EL2_VI);
        status = arm64_el2_resume(vttbr, state, hcr);
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt, return to the guest.
            status = ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&hcr_, &el2_state_.guest_state, gpas_, traps_, packet);
        } else {
            dprintf(INFO, "VCPU resume failed: %d\n", status);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
}

zx_status_t Vcpu::Interrupt(uint32_t interrupt) {
    // TODO(abdulla): Improve handling of list register exhaustion.
    uint64_t elsr = gich_->elsr;
    if (elsr == 0)
        return ZX_ERR_NO_RESOURCES;

    size_t i = __builtin_ctzl(elsr);
    gich_->lr[i] = kGichLrPending | interrupt;
    hcr_.fetch_or(HCR_EL2_VI);

    // TODO(abdulla): Handle the case when the VCPU is halted.
    DEBUG_ASSERT(!arch_ints_disabled());
    arch_disable_ints();
    auto cpu = cpu_of(vpid_);
    if (cpu == arch_curr_cpu_num()) {
        thread_reschedule();
    } else {
        mp_reschedule(MP_IPI_TARGET_MASK, cpu_num_to_mask(cpu), 0);
    }
    arch_enable_ints();
    return ZX_OK;
}

zx_status_t Vcpu::ReadState(uint32_t kind, void* buffer, uint32_t len) const {
    if (!check_pinned_cpu_invariant(thread_, vpid_))
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
    if (!check_pinned_cpu_invariant(thread_, vpid_))
        return ZX_ERR_BAD_STATE;
    if (kind != ZX_VCPU_STATE || len != sizeof(zx_vcpu_state_t))
        return ZX_ERR_INVALID_ARGS;

    auto state = static_cast<const zx_vcpu_state_t*>(buffer);
    memcpy(el2_state_.guest_state.x, state->x, sizeof(uint64_t) * GS_NUM_REGS);
    el2_state_.guest_state.system_state.sp_el1 = state->sp;
    el2_state_.guest_state.system_state.spsr_el2 |= state->cpsr & kSpsrNzcv;
    return ZX_OK;
}

zx_status_t arm_vcpu_create(zx_vaddr_t ip, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
                            TrapMap* traps, fbl::unique_ptr<Vcpu>* out) {
    return Vcpu::Create(ip, vmid, gpas, traps, out);
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
