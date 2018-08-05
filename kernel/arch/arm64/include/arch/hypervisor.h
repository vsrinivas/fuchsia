// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64/el2_state.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <hypervisor/guest_physical_address_space.h>
#include <hypervisor/id_allocator.h>
#include <hypervisor/interrupt_tracker.h>
#include <hypervisor/page.h>
#include <hypervisor/trap_map.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <zircon/types.h>

static constexpr uint16_t kNumInterrupts = 256;
static constexpr uint32_t kTimerVector = 27;
static_assert(kTimerVector < kNumInterrupts, "Timer vector is out of range");

typedef struct zx_port_packet zx_port_packet_t;
using InterruptBitmap = bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumInterrupts>>;

class PortDispatcher;

class Guest {
public:
    static zx_status_t Create(fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

    hypervisor::GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
    hypervisor::TrapMap* Traps() { return &traps_; }
    uint8_t Vmid() const { return vmid_; }

    zx_status_t AllocVpid(uint8_t* vpid);
    zx_status_t FreeVpid(uint8_t vpid);

private:
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas_;
    hypervisor::TrapMap traps_;
    const uint8_t vmid_;

    fbl::Mutex vcpu_mutex_;
    // TODO(alexlegg): Find a good place for this constant to live (max vcpus).
    hypervisor::IdAllocator<uint8_t, 8> TA_GUARDED(vcpu_mutex_) vpid_allocator_;

    explicit Guest(uint8_t vmid);
};

// Stores the state of the GICH across VM exits.
struct GichState {
    // Timer for ARM generic timer.
    timer_t timer;
    // Tracks pending interrupts.
    hypervisor::InterruptTracker<kNumInterrupts> interrupt_tracker;
    // Tracks active interrupts.
    InterruptBitmap active_interrupts;

    // GICH state to be restored between VM exits.
    uint32_t num_lrs;
    uint32_t vmcr;
    uint64_t elrsr;
    uint32_t apr;
    uint64_t lr[64] = {};
};

// Loads a GICH within a given scope.
class AutoGich {
public:
    AutoGich(GichState* gich_state);
    ~AutoGich();

private:
    GichState* gich_state_;
};

// Provides a smart pointer to an EL2State allocated in its own page.
//
// We allocate an EL2State into its own page as the structure is passed between
// EL1 and EL2, which have different address spaces mappings. This ensures that
// EL2State will not cross a page boundary and be incorrectly accessed in EL2.
class El2StatePtr {
public:
    zx_status_t Alloc();

    paddr_t PhysicalAddress() const { return page_.PhysicalAddress(); }
    El2State* operator->() const { return state_; }

private:
    hypervisor::Page page_;
    El2State* state_ = nullptr;
};

class Vcpu {
public:
    static zx_status_t Create(Guest* guest, zx_vaddr_t entry, fbl::unique_ptr<Vcpu>* out);
    ~Vcpu();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

    zx_status_t Resume(zx_port_packet_t* packet);
    zx_status_t Interrupt(uint32_t interrupt);
    zx_status_t ReadState(uint32_t kind, void* buf, size_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buf, size_t len);

private:
    Guest* guest_;
    const uint8_t vpid_;
    const thread_t* thread_;
    fbl::atomic_bool running_;
    GichState gich_state_;
    El2StatePtr el2_state_;
    uint64_t hcr_;

    Vcpu(Guest* guest, uint8_t vpid, const thread_t* thread);
};
