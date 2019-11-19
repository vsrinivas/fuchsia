// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <err.h>
#include <lib/unittest/unittest.h>
#include <zircon/types.h>

#include <arch/aspace.h>
#include <arch/mmu.h>
#include <arch/x86/mmu.h>
#include <vm/arch_vm_aspace.h>
#include <vm/pmm.h>

static bool check_virtual_address_mapped(uint64_t* pml4, vaddr_t va) {
  constexpr uint kPageTableLevels = 4;

  // Virtual Address is split at [47:39], [38:30], [29:21], [20:12]
  uint64_t offsets[kPageTableLevels] = {BITS_SHIFT(va, 47, 39), BITS_SHIFT(va, 38, 30),
                                        BITS_SHIFT(va, 29, 21), BITS_SHIFT(va, 20, 12)};
  uint64_t* current_level = pml4;
  for (uint i = 0; i < kPageTableLevels; i++) {
    uint64_t index = offsets[i];
    uint64_t pte = current_level[index];
    if ((pte & X86_MMU_PG_P) == 0) {
      return false;
    }

    uint64_t next_level_va = X86_PHYS_TO_VIRT(pte & X86_PG_FRAME);
    current_level = reinterpret_cast<uint64_t*>(next_level_va);
  }
  return true;
}

static bool x86_arch_vmaspace_usermmu_tests() {
  BEGIN_TEST;

  constexpr uint kPtPerPageTable = PAGE_SIZE / sizeof(uint64_t);
  constexpr uint kUserPtPerPageTable = kPtPerPageTable / 2;
  {
    constexpr uint64_t kTestAspaceSize = 4ull * 1024 * 1024 * 1024;
    constexpr uintptr_t kTestVirtualAddress = kTestAspaceSize - PAGE_SIZE;
    // Basic test - make an aspace, map something, query it, check page tables, unmap
    X86ArchVmAspace<pmm_alloc_page> aspace;
    EXPECT_EQ(ZX_OK, aspace.Init(0, kTestAspaceSize, /*mmu_flags=*/0));
    uint64_t* const pml4 = reinterpret_cast<uint64_t*>(X86_PHYS_TO_VIRT(aspace.pt_phys()));
    // Expect no user mode mappings in an empty address space.
    for (uint i = 0; i < kUserPtPerPageTable; i++) {
      EXPECT_EQ(pml4[i], 0u);
    }

    paddr_t pa = 0;
    size_t mapped;
    vm_page_t* vm_page;
    pmm_alloc_page(/*alloc_flags=*/0, &vm_page, &pa);
    EXPECT_EQ(ZX_OK, aspace.Map(kTestVirtualAddress, &pa, 1,
                                ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE, &mapped));
    EXPECT_EQ(1u, mapped);
    // Directly examine page tables to ensure there's a mapping.
    EXPECT_EQ(check_virtual_address_mapped(pml4, kTestVirtualAddress), true);

    // Use query() interface to find a mapping.
    paddr_t retrieved_pa;
    uint flags;
    EXPECT_EQ(ZX_OK, aspace.Query(kTestVirtualAddress, &retrieved_pa, &flags));
    EXPECT_EQ(retrieved_pa, pa);

    size_t unmapped;
    EXPECT_EQ(ZX_OK, aspace.Unmap(kTestVirtualAddress, 1, &unmapped));
    EXPECT_EQ(unmapped, mapped);
    EXPECT_EQ(check_virtual_address_mapped(pml4, kTestVirtualAddress), false);
    // Expect no user mode mappings after the user mapping was removed.
    for (uint i = 0; i < kUserPtPerPageTable; i++) {
      EXPECT_EQ(pml4[i], 0u);
    }
    pmm_free_page(vm_page);

    aspace.Destroy();
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(x86_mmu_tests)
UNITTEST("user-aspace page table tests", x86_arch_vmaspace_usermmu_tests)
UNITTEST_END_TESTCASE(x86_mmu_tests, "x86_mmu", "x86 mmu tests")
