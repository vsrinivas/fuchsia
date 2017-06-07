// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/vm/arch_vm_aspace.h>

#include <kernel/vm.h>
#include <magenta/errors.h>

ArchVmAspace::ArchVmAspace() {
}

ArchVmAspace::~ArchVmAspace() {
    // TODO: check that we've destroyed the aspace
}

status_t ArchVmAspace::Init(vaddr_t base, size_t size, uint mmu_flags) {
    return arch_internal::arch_mmu_init_aspace(&aspace_, base, size, mmu_flags);
}

status_t ArchVmAspace::Destroy() {
    return arch_internal::arch_mmu_destroy_aspace(&aspace_);
}

status_t ArchVmAspace::Map(vaddr_t vaddr, paddr_t paddr, size_t count, uint mmu_flags, size_t* mapped) {
    return arch_internal::arch_mmu_map(&aspace_, vaddr, paddr, count, mmu_flags, mapped);
}

status_t ArchVmAspace::Unmap(vaddr_t vaddr, size_t count, size_t* unmapped) {
    return arch_internal::arch_mmu_unmap(&aspace_, vaddr, count, unmapped);
}

status_t ArchVmAspace::Protect(vaddr_t vaddr, size_t count, uint mmu_flags) {
    return arch_internal::arch_mmu_protect(&aspace_, vaddr, count, mmu_flags);
}

status_t ArchVmAspace::Query(vaddr_t vaddr, paddr_t* paddr, uint* mmu_flags) {
    return arch_internal::arch_mmu_query(&aspace_, vaddr, paddr, mmu_flags);
}

vaddr_t ArchVmAspace::PickSpot(vaddr_t base, uint prev_region_mmu_flags,
                     vaddr_t end, uint next_region_mmu_flags,
                     vaddr_t align, size_t size, uint mmu_flags) {
    return PAGE_ALIGN(base);
}

