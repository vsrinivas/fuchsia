// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/unittest/unittest.h>
#include <lib/zircon-internal/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/arm64/mmu.h>
#include <arch/aspace.h>
#include <vm/arch_vm_aspace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm_aspace.h>

namespace {

constexpr size_t kTestAspaceSize = (1UL << 48);
constexpr size_t kTestVirtualAddress = (1UL << 30);  // arbitrary address

bool arm64_test_perms() {
  BEGIN_TEST;

  ArmArchVmAspace aspace(0, kTestAspaceSize, ArmAspaceType::kUser);
  EXPECT_EQ(ZX_OK, aspace.Init());

  auto map_query_test = [&](uint mmu_perms) -> bool {
    paddr_t pa = 0;
    size_t count;
    vm_page_t* vm_page;
    pmm_alloc_page(/*alloc_flags=*/0, &vm_page, &pa);
    EXPECT_EQ(ZX_OK, aspace.Map(kTestVirtualAddress, &pa, 1, mmu_perms,
                                ArchVmAspaceInterface::ExistingEntryAction::Error, &count));
    EXPECT_EQ(1u, count);

    paddr_t query_pa;
    uint query_flags;
    EXPECT_EQ(ZX_OK, aspace.Query(kTestVirtualAddress, &query_pa, &query_flags));
    EXPECT_EQ(pa, query_pa);
    EXPECT_EQ(mmu_perms, query_flags);

    // FUTURE ENHANCEMENT: use a private api to read the terminal page table entry
    // and validate the bits that were set.

    EXPECT_EQ(ZX_OK,
              aspace.Unmap(kTestVirtualAddress, 1, ArchVmAspace::EnlargeOperation::No, &count));
    EXPECT_EQ(1u, count);

    return all_ok;
  };

  // map nox page, query to see that X bit isn't set
  map_query_test(ARCH_MMU_FLAG_PERM_READ);
  map_query_test(ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  map_query_test(ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE);
  map_query_test(ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE | ARCH_MMU_FLAG_PERM_EXECUTE);

  // map X page, query to see that X bit is set
  map_query_test(ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_READ);
  map_query_test(ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
  map_query_test(ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_EXECUTE);
  map_query_test(ARCH_MMU_FLAG_PERM_USER | ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE |
                 ARCH_MMU_FLAG_PERM_EXECUTE);

  // TODO: fxbug.dev/88451 Add a more comprehensive test that reads back the page table entries
  // and all the translation tables leading up to it to make sure the permission bits are set
  // properly. Requires plumbing through some code to the ArmArchVmAspace to return a copy of all
  // the levels of the translation tables and terminal entry.

  EXPECT_EQ(ZX_OK, aspace.Destroy());

  END_TEST;
}

}  // anonymous namespace

UNITTEST_START_TESTCASE(arm64_mmu_tests)
UNITTEST("perms", arm64_test_perms)
UNITTEST_END_TESTCASE(arm64_mmu_tests, "arm64_mmu", "arm64 mmu tests")
