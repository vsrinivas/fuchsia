// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/hypervisor.h>
#include <arch/ops.h>
#include <fbl/auto_call.h>
#include <hypervisor/cpu.h>
#include <hypervisor/guest_physical_address_space.h>
#include <platform/timer.h>
#include <vm/pmm.h>
#include <zircon/errors.h>
#include <zircon/syscalls/hypervisor.h>

#include "el2_cpu_state_priv.h"
#include "vmexit_priv.h"

static const uint32_t kSpsrEl1h = 0b0101;
static const uint32_t kSpsrDaif = 0b1111 << 6;
static const uint32_t kSpsrNzcv = 0b1111 << 28;

static zx_paddr_t vttbr_of(uint8_t vmid, zx_paddr_t table) {
    return static_cast<zx_paddr_t>(vmid) << 48 | table;
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

    vcpu->el2_state_.guest_state.system_state.elr_el2 = ip;
    vcpu->el2_state_.guest_state.system_state.spsr_el2 = kSpsrEl1h | kSpsrDaif;

    auto_call.cancel();
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
    zx_paddr_t vttbr = vttbr_of(vmid_, gpas_->table_phys());
    zx_status_t status;
    do {
        status = arm64_el2_resume(vaddr_to_paddr(&el2_state_), vttbr);
        if (status == ZX_ERR_NEXT) {
            // We received a physical interrupt, return to the guest.
            status = ZX_OK;
        } else if (status == ZX_OK) {
            status = vmexit_handler(&el2_state_.guest_state, gpas_, traps_, packet);
        } else {
            dprintf(SPEW, "VCPU resume failed: %d\n", status);
        }
    } while (status == ZX_OK);
    return status == ZX_ERR_NEXT ? ZX_OK : status;
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
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len) {
    return vcpu->ReadState(kind, buffer, len);
}

zx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len) {
    return vcpu->WriteState(kind, buffer, len);
}
