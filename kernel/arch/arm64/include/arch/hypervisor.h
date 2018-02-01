// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64/el2_state.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <arch/arm64/hypervisor/gic/gicv2.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/trap_map.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <zircon/types.h>

static const uint16_t kNumInterrupts = 256;
static const uint32_t kGichHcrEn = 1u << 0;
static const uint32_t kGichVtrListRegs = 0b111111;
static const uint32_t kGichLrPending = 0b01 << 28;

typedef struct zx_port_packet zx_port_packet_t;

class GuestPhysicalAddressSpace;
class PortDispatcher;
class VmObject;

class Guest {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

    GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
    TrapMap* Traps() { return &traps_; }
    uint8_t Vmid() const { return vmid_; }

private:
    fbl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
    TrapMap traps_;
    const uint8_t vmid_;

    explicit Guest(uint8_t vmid);
};

// Stores the state of the GICH across VM exits.
struct GichState {
    // Timer for ARM generic timer.
    timer_t timer;
    // Tracks active interrupts.
    hypervisor::InterruptTracker<kNumInterrupts> interrupt_tracker;

    // GICH state to be restored between VM exits.
    uint32_t num_lrs;
    uint32_t vmcr = 0;
    uint64_t elrs;
    uint32_t lr[64] = {};
};

// Loads a GICH within a given scope.
class AutoGich {
public:
    AutoGich(GichState* gich_state);
    ~AutoGich();

private:
    GichState* gich_state_;
};

class Vcpu {
public:
    static zx_status_t Create(zx_vaddr_t entry, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
                              TrapMap* traps, fbl::unique_ptr<Vcpu>* out);
    ~Vcpu();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

    zx_status_t Resume(zx_port_packet_t* packet);
    zx_status_t Interrupt(uint32_t interrupt);
    zx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    const uint8_t vmid_;
    const uint8_t vpid_;
    const thread_t* thread_;
    fbl::atomic_bool running_;
    GichState gich_state_;
    GuestPhysicalAddressSpace* gpas_;
    TrapMap* traps_;
    El2State el2_state_;
    uint64_t hcr_;

    Vcpu(uint8_t vmid, uint8_t vpid, const thread_t* thread, GuestPhysicalAddressSpace* gpas,
         TrapMap* traps);
};

/* Create a guest. */
zx_status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
zx_status_t arch_guest_set_trap(Guest* guest, uint32_t kind, zx_vaddr_t addr, size_t len,
                                fbl::RefPtr<PortDispatcher> port, uint64_t key);

/* Create a VCPU. */
zx_status_t arm_vcpu_create(zx_vaddr_t entry, uint8_t vmid, GuestPhysicalAddressSpace* gpas,
                            TrapMap* traps, fbl::unique_ptr<Vcpu>* out);

/* Resume execution of a VCPU. */
zx_status_t arch_vcpu_resume(Vcpu* vcpu, zx_port_packet_t* packet);

/* Issue an interrupt on a VCPU. */
zx_status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
zx_status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len);

/* Write the register state of a VCPU. */
zx_status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len);
