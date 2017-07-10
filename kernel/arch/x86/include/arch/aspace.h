// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/mmu.h>
#include <kernel/spinlock.h>
#include <kernel/vm/arch_vm_aspace.h>
#include <magenta/compiler.h>
#include <mxtl/canary.h>
#include <mxtl/ref_counted.h>

__BEGIN_CDECLS

#define ARCH_ASPACE_MAGIC 0x41524153 // ARAS

struct arch_aspace {
    /* magic value for use-after-free detection */
    uint32_t magic;

    /* pointer to the translation table */
    paddr_t pt_phys;
    pt_entry_t *pt_virt;

    uint flags;

    /* range of address space */
    vaddr_t base;
    size_t size;

    /* cpus that are currently executing in this aspace
     * actually an mp_cpu_mask_t, but header dependencies. */
    volatile int active_cpus;

    /* Pointer to a bitmap::RleBitmap representing the range of ports
     * enabled in this aspace. */
    void *io_bitmap;
    spin_lock_t io_bitmap_lock;
};

__END_CDECLS

class X86ArchVmAspace final : public ArchVmAspaceInterface, mxtl::RefCounted<X86ArchVmAspace>  {
public:
    X86ArchVmAspace() { }
    virtual ~X86ArchVmAspace();

    status_t Init(vaddr_t base, size_t size, uint mmu_flags) override;
    status_t Destroy() override;

    // main methods
    status_t Map(vaddr_t vaddr, paddr_t paddr, size_t count,
                 uint mmu_flags, size_t* mapped) override;
    status_t Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) override;
    status_t Protect(vaddr_t vaddr, size_t count, uint mmu_flags) override;
    status_t Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) override;

    vaddr_t PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                     vaddr_t end, uint next_region_mmu_flags,
                     vaddr_t align, size_t size, uint mmu_flags) override;

    arch_aspace& GetInnerAspace() { return aspace_; }

    static void ContextSwitch(X86ArchVmAspace *from, X86ArchVmAspace *to);
private:
    mxtl::Canary<mxtl::magic("VAAS")> canary_;
    arch_aspace aspace_ = {};
};

using ArchVmAspace = X86ArchVmAspace;
