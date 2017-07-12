// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/x86/ioport.h>
#include <arch/x86/mmu.h>
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
    /** counter of pages allocated to back the translation table */
    size_t pt_pages;

    uint flags;

    /* range of address space */
    vaddr_t base;
    size_t size;

    /* cpus that are currently executing in this aspace
     * actually an mp_cpu_mask_t, but header dependencies. */
    volatile int active_cpus;
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

    paddr_t arch_table_phys() const override { return aspace_.pt_phys; }

    paddr_t pt_phys() const { return aspace_.pt_phys; }

    size_t pt_pages() const { return aspace_.pt_pages; }

    IoBitmap& io_bitmap() { return io_bitmap_; }

    static void ContextSwitch(X86ArchVmAspace *from, X86ArchVmAspace *to);
private:
    mxtl::Canary<mxtl::magic("VAAS")> canary_;
    IoBitmap io_bitmap_;
    arch_aspace aspace_ = {};
};

using ArchVmAspace = X86ArchVmAspace;
