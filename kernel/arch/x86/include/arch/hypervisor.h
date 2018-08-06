// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>
#include <arch/x86/vmx_state.h>
#include <fbl/array.h>
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

class VmObject;
struct VmxInfo;

class VmxPage : public hypervisor::Page {
public:
    zx_status_t Alloc(const VmxInfo& info, uint8_t fill);

private:
    using hypervisor::Page::Alloc;
};

// Represents a guest within the hypervisor.
class Guest {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    zx_status_t SetTrap(uint32_t kind, zx_vaddr_t addr, size_t len,
                        fbl::RefPtr<PortDispatcher> port, uint64_t key);

    hypervisor::GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
    hypervisor::TrapMap* Traps() { return &traps_; }
    zx_paddr_t MsrBitmapsAddress() const { return msr_bitmaps_page_.PhysicalAddress(); }

    zx_status_t AllocVpid(uint16_t* vpid);
    zx_status_t FreeVpid(uint16_t vpid);

private:
    fbl::unique_ptr<hypervisor::GuestPhysicalAddressSpace> gpas_;
    hypervisor::TrapMap traps_;
    VmxPage msr_bitmaps_page_;

    fbl::Mutex vcpu_mutex_;
    // TODO(alexlegg): Find a good place for this constant to live (max vcpus).
    hypervisor::IdAllocator<uint16_t, 64> TA_GUARDED(vcpu_mutex_) vpid_allocator_;

    Guest() = default;
};

// Stores the local APIC state across VM exits.
struct LocalApicState {
    // Timer for APIC timer.
    timer_t timer;
    // Tracks pending interrupts.
    hypervisor::InterruptTracker<X86_INT_COUNT> interrupt_tracker;
    // LVT timer configuration
    uint32_t lvt_timer = LVT_MASKED; // Initial state is masked (Vol 3 Section 10.12.5.1).
    uint32_t lvt_initial_count;
    uint32_t lvt_divide_config;
};

// System time is time since boot time and boot time is some fixed point in the past. This
// structure keeps track of the state required to update system time in guest.
struct pvclock_system_time;
struct PvClockState {
    bool is_stable = false;
    uint32_t version = 0;
    pvclock_system_time* system_time = nullptr;
    hypervisor::GuestPtr guest_ptr;
};

// Represents a virtual CPU within a guest.
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
    const uint16_t vpid_;
    const thread_t* thread_;
    fbl::atomic_bool running_;
    LocalApicState local_apic_state_;
    PvClockState pvclock_state_;
    VmxState vmx_state_;
    VmxPage host_msr_page_;
    VmxPage guest_msr_page_;
    VmxPage vmcs_page_;

    Vcpu(Guest* guest, uint16_t vpid, const thread_t* thread);
};
