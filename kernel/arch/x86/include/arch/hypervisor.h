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
#include <fbl/array.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>

static const uint16_t kNumInterrupts = X86_MAX_INT + 1;

typedef struct vm_page vm_page_t;

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
    static status_t Create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* out);
    ~Guest();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Guest);

    status_t SetTrap(uint32_t kind, mx_vaddr_t addr, size_t len,
                     fbl::RefPtr<PortDispatcher> port, uint64_t key);

    GuestPhysicalAddressSpace* AddressSpace() const { return gpas_.get(); }
    PacketMux& Mux() { return mux_; }
    paddr_t ApicAccessAddress() const { return apic_access_page_.PhysicalAddress(); }
    paddr_t MsrBitmapsAddress() const { return msr_bitmaps_page_.PhysicalAddress(); }

private:
    fbl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
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
    static status_t Create(mx_vaddr_t ip, mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                           paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                           GuestPhysicalAddressSpace* gpas, PacketMux& mux,
                           fbl::unique_ptr<Vcpu>* out);
    ~Vcpu();
    DISALLOW_COPY_ASSIGN_AND_MOVE(Vcpu);

    status_t Resume(mx_port_packet_t* packet);
    status_t Interrupt(uint32_t interrupt);
    status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

    uint16_t vpid() const { return vpid_; }

private:
    const thread_t* thread_;
    const uint16_t vpid_;
    fbl::RefPtr<VmObject> apic_vmo_;
    LocalApicState local_apic_state_;
    GuestPhysicalAddressSpace* gpas_;
    PacketMux& mux_;
    VmxState vmx_state_;
    VmxPage host_msr_page_;
    VmxPage guest_msr_page_;
    VmxPage vmcs_page_;

    Vcpu(const thread_t* thread, uint16_t vpid, fbl::RefPtr<VmObject> apic_vmo,
         GuestPhysicalAddressSpace* gpas, PacketMux& mux);
};

/* Create a guest. */
status_t arch_guest_create(fbl::RefPtr<VmObject> physmem, fbl::unique_ptr<Guest>* guest);

/* Set a trap within a guest. */
status_t arch_guest_set_trap(Guest* guest, uint32_t kind, mx_vaddr_t addr, size_t len,
                             fbl::RefPtr<PortDispatcher> port, uint64_t key);

/* Create a VCPU. */
status_t x86_vcpu_create(mx_vaddr_t ip, mx_vaddr_t cr3, fbl::RefPtr<VmObject> apic_vmo,
                         paddr_t apic_access_address, paddr_t msr_bitmaps_address,
                         GuestPhysicalAddressSpace* gpas, PacketMux& mux,
                         fbl::unique_ptr<Vcpu>* out);

/* Resume execution of a VCPU. */
status_t arch_vcpu_resume(Vcpu* vcpu, mx_port_packet_t* packet);

/* Issue an interrupt on a VCPU. */
status_t arch_vcpu_interrupt(Vcpu* vcpu, uint32_t interrupt);

/* Read the register state of a VCPU. */
status_t arch_vcpu_read_state(const Vcpu* vcpu, uint32_t kind, void* buffer, uint32_t len);

/* Write the register state of a VCPU. */
status_t arch_vcpu_write_state(Vcpu* vcpu, uint32_t kind, const void* buffer, uint32_t len);
