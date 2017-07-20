// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/interrupts.h>
#include <arch/x86/vmx_state.h>
#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <hypervisor/packet_mux.h>
#include <kernel/event.h>
#include <kernel/spinlock.h>
#include <kernel/timer.h>
#include <magenta/types.h>
#include <magenta/syscalls/hypervisor.h>
#include <mxtl/array.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

static const uint16_t kNumInterrupts = X86_MAX_INT + 1;

typedef struct vm_page vm_page_t;

class FifoDispatcher;
class GuestPhysicalAddressSpace;
class VmObject;
struct VmxInfo;

class VmxPage {
public:
    VmxPage() = default;
    ~VmxPage();
    DISALLOW_COPY_ASSIGN_AND_MOVE(VmxPage);

    status_t Alloc(const VmxInfo& info, uint8_t fill);
    paddr_t PhysicalAddress() const;
    void* VirtualAddress() const;

    template<typename T>
    T* VirtualAddress() const {
        return static_cast<T*>(VirtualAddress());
    }

    bool IsAllocated() const { return pa_ != 0; }

private:
    paddr_t pa_ = 0;
};

/* Represents a guest within the hypervisor. */
class Guest {
public:
    static status_t Create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    status_t SetTrap(mx_trap_address_space_t aspace, mx_vaddr_t addr, size_t len,
                     mxtl::RefPtr<FifoDispatcher> fifo);

    GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
    const PacketMux& Mux() const { return mux_; }
    paddr_t ApicAccessAddress() const { return apic_access_page_.PhysicalAddress(); }
    paddr_t MsrBitmapsAddress() const { return msr_bitmaps_page_.PhysicalAddress(); }

private:
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
    PacketMux mux_;
    VmxPage apic_access_page_;
    VmxPage msr_bitmaps_page_;

    Guest() = default;
};

/* Stores the local APIC state across VM exits. */
struct LocalApicState {
    // Timer for APIC timer.
    timer_t timer;
    // Event for handling block on HLT.
    event_t event;
    // Lock for the interrupt bitmap.
    SpinLock interrupt_lock;
    // Bitmap of active interrupts.
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumInterrupts>> interrupt_bitmap;
    // Virtual local APIC address.
    void* apic_addr = nullptr;
};

/* Represents a virtual CPU within a guest. */
class Vcpu {
public:
    static status_t Create(mx_vaddr_t ip, mx_vaddr_t cr3, mxtl::RefPtr<VmObject> apic_vmo,
                           paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                           GuestPhysicalAddressSpace* gpas, const PacketMux& mux,
                           mxtl::unique_ptr<Vcpu>* out);
    ~Vcpu();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

    status_t Resume(mx_guest_packet_t* packet);
    status_t Interrupt(uint32_t interrupt);
    status_t ReadState(mx_vcpu_state_t* state) const;
    status_t WriteState(const mx_vcpu_state_t& state);

    uint16_t vpid() const { return vpid_; }

private:
    const uint16_t vpid_;
    mxtl::RefPtr<VmObject> apic_vmo_;
    LocalApicState local_apic_state_;
    GuestPhysicalAddressSpace* gpas_;
    const PacketMux& mux_;
    VmxState vmx_state_;
    VmxPage host_msr_page_;
    VmxPage guest_msr_page_;
    VmxPage vmcs_page_;

    explicit Vcpu(uint16_t vpid, mxtl::RefPtr<VmObject> apic_vmo, GuestPhysicalAddressSpace* gpas,
                  const PacketMux& mux);
};

/* Create a guest. */
status_t arch_guest_create(mxtl::RefPtr<VmObject> physmem, mxtl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
status_t arch_guest_set_trap(Guest* guest, mx_trap_address_space_t aspace, mx_vaddr_t addr,
                             size_t len, mxtl::RefPtr<FifoDispatcher> fifo);

/* Create a VCPU. */
status_t x86_vcpu_create(mx_vaddr_t ip, mx_vaddr_t cr3, mxtl::RefPtr<VmObject> apic_vmo,
                         paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                         GuestPhysicalAddressSpace* gpas, const PacketMux& mux,
                         mxtl::unique_ptr<Vcpu>* out);

/* Resume execution of a VCPU. */
status_t arch_vcpu_resume(Vcpu* vcpu, mx_guest_packet_t* packet);

/* Issue an interrupt on a VCPU. */
status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
status_t arch_vcpu_read_state(const Vcpu* vcpu, mx_vcpu_state_t* state);

/* Write the register state of a VCPU. */
status_t arch_vcpu_write_state(Vcpu* vcpu, const mx_vcpu_state_t& state);
