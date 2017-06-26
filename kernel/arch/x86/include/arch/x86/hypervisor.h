// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <kernel/spinlock.h>
#include <magenta/types.h>
#include <mxtl/array.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

static const uint16_t kNumVpids = 64;

typedef struct mx_guest_gpr mx_guest_gpr_t;
typedef struct vm_page vm_page_t;

class FifoDispatcher;
class VmObject;
struct VmxInfo;
class VmxonPerCpu;
class VmcsPerCpu;
class GuestPhysicalAddressSpace;

class VmxPage {
public:
    ~VmxPage();

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

class VmxonContext {
public:
    static status_t Create(mxtl::unique_ptr<VmxonContext>* context);

    ~VmxonContext();

    VmxonPerCpu* PerCpu();
    status_t AllocVpid(uint16_t* vpid);
    status_t ReleaseVpid(uint16_t vpid);

private:
    SpinLock vpid_lock_;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kNumVpids>> vpid_bitmap_;
    mxtl::Array<VmxonPerCpu> per_cpus_;

    explicit VmxonContext(mxtl::Array<VmxonPerCpu> per_cpus);
};

class VmcsContext {
public:
    static status_t Create(VmxonContext* hypervisor,
                           mxtl::RefPtr<VmObject> phys_mem,
                           mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                           mxtl::unique_ptr<VmcsContext>* context);

    ~VmcsContext();

    paddr_t Pml4Address();
    paddr_t ApicAccessAddress();
    paddr_t MsrBitmapsAddress();
    VmcsPerCpu* PerCpu();

    status_t Enter();
    status_t MemTrap(vaddr_t guest_paddr, size_t size);
    status_t Interrupt(uint8_t interrupt);
    status_t SetGpr(const mx_guest_gpr_t& guest_gpr);
    status_t GetGpr(mx_guest_gpr_t* guest_gpr) const;
    status_t SetApicMem(mxtl::RefPtr<VmObject> apic_mem);

    status_t set_ip(uintptr_t guest_ip);
    uintptr_t ip() const {  return ip_; }
    status_t set_cr3(uintptr_t guest_cr3);
    uintptr_t cr3() const { return cr3_; }

    VmxonContext* hypervisor() { return hypervisor_; }
    GuestPhysicalAddressSpace* gpas() { return gpas_.get(); }
    FifoDispatcher* ctl_fifo() { return ctl_fifo_.get(); }

private:
    uintptr_t ip_ = UINTPTR_MAX;
    uintptr_t cr3_ = UINTPTR_MAX;
    VmxonContext* hypervisor_;
    mxtl::unique_ptr<GuestPhysicalAddressSpace> gpas_;
    mxtl::RefPtr<FifoDispatcher> ctl_fifo_;

    VmxPage msr_bitmaps_page_;
    VmxPage apic_address_page_;
    mxtl::Array<VmcsPerCpu> per_cpus_;

    explicit VmcsContext(VmxonContext* hypervisor, mxtl::RefPtr<FifoDispatcher> ctl_fifo,
                         mxtl::Array<VmcsPerCpu> per_cpus);
};

using HypervisorContext = VmxonContext;
using GuestContext = VmcsContext;

/* Set the local APIC memory of the guest context.
 */
status_t x86_guest_set_apic_mem(const mxtl::unique_ptr<GuestContext>& context,
                                mxtl::RefPtr<VmObject> apic_mem);

/* Set the initial CR3 of the guest context.
 */
status_t x86_guest_set_cr3(const mxtl::unique_ptr<GuestContext>& context, uintptr_t guest_cr3);
