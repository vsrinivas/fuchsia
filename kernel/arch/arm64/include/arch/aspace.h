// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015-2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arm64/mmu.h>
#include <kernel/vm/arch_vm_aspace.h>
#include <magenta/compiler.h>
#include <mxtl/canary.h>
#include <mxtl/ref_counted.h>

__BEGIN_CDECLS

#define ARCH_ASPACE_MAGIC 0x41524153 // ARAS

struct arch_aspace {
    /* magic value for use-after-free detection */
    uint32_t magic;

    uint16_t asid;

    /* pointer to the translation table */
    paddr_t tt_phys;
    volatile pte_t *tt_virt;
    /** upper bound of the number of pages allocated to back the translation table */
    size_t pt_pages;

    uint flags;

    /* range of address space */
    vaddr_t base;
    size_t size;
};

__END_CDECLS

class ArmArchVmAspace final : public ArchVmAspaceInterface, mxtl::RefCounted<ArmArchVmAspace>  {
public:
    ArmArchVmAspace() { }
    virtual ~ArmArchVmAspace();

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

    paddr_t arch_table_phys() const override { return aspace_.tt_phys; }

    static void ContextSwitch(ArmArchVmAspace *from, ArmArchVmAspace *to);
private:
    mxtl::Canary<mxtl::magic("VAAS")> canary_;
    arch_aspace aspace_ = {};
};

using ArchVmAspace = ArmArchVmAspace;
