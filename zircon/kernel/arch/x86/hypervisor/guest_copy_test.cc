// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/fit/defer.h>
#include <lib/unittest/unittest.h>

#include <arch/user_copy.h>
#include <vm/physmap.h>
#include <vm/vm_object_paged.h>

#include "guest_copy_priv.h"

namespace {

bool guest_copy() {
  BEGIN_TEST;

  constexpr size_t kVmoSize = PAGE_SIZE * 5;
  fbl::RefPtr<VmObjectPaged> vmo;
  ASSERT_OK(VmObjectPaged::CreateContiguous(PMM_ALLOC_FLAG_ANY, kVmoSize, 0, &vmo));
  ASSERT_OK(vmo->CommitRangePinned(0, kVmoSize, true));
  auto defer = fit::defer([vmo] { vmo->Unpin(0, kVmoSize); });

  paddr_t pa;
  ASSERT_OK(vmo->LookupContiguous(0, kVmoSize, &pa));
  auto addr = static_cast<uint8_t*>(paddr_to_physmap(pa));
  memset(addr, 0, kVmoSize);

  auto gpas = hypervisor::GuestPhysicalAddressSpace::Create();
  ASSERT_OK(gpas.status_value());

  constexpr uint32_t kVmarFlags = VMAR_FLAG_SPECIFIC;
  constexpr uint kArchMmuFlags = ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE;
  fbl::RefPtr<VmMapping> mapping;
  ASSERT_OK(gpas->RootVmar()->CreateVmMapping(0, kVmoSize, 0, kVmarFlags, vmo, 0, kArchMmuFlags,
                                              "page-table", &mapping));

  hypervisor::DefaultTlb tlb;
  GuestPageTable gpt{tlb, *gpas, 0};

  // Test 1: Copy with empty page tables.
  uint64_t data;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, arch_copy_from_guest(gpt, &data, 0, sizeof(data)));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, arch_copy_to_guest(gpt, 0, &data, sizeof(data)));

  // Test 2: Copy with 1GB mapping.
  // PML4 entry pointing to PAGE_SIZE
  auto pte = reinterpret_cast<uint64_t*>(addr);
  pte[0] = PAGE_SIZE | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;
  // PDP entry with 1GB page, pointing to 0 to form an identity mapping.
  pte = reinterpret_cast<uint64_t*>(addr + PAGE_SIZE);
  pte[0] = X86_MMU_PG_PS | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;

  uintptr_t offset = PAGE_SIZE * 2;
  auto target = reinterpret_cast<uint64_t*>(addr + offset);
  *target = 0xfeedfacefeedface;

  uint64_t actual = 0;
  ASSERT_EQ(ZX_OK,
            arch_copy_from_guest(gpt, &actual, reinterpret_cast<void*>(offset), sizeof(actual)));
  ASSERT_EQ(0xfeedfacefeedface, actual);

  actual = 0xcafebeefcafebeef;
  ASSERT_EQ(ZX_OK,
            arch_copy_to_guest(gpt, reinterpret_cast<void*>(offset), &actual, sizeof(actual)));
  ASSERT_EQ(0xcafebeefcafebeef, *target);

  // Test 3: Copy with 2MB mapping.
  // PDP entry pointing to PAGE_SIZE * 2
  pte = reinterpret_cast<uint64_t*>(addr + PAGE_SIZE);
  pte[0] = PAGE_SIZE * 2 | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;
  // PD entry with 2MB page, pointing to 0 to form an identity mapping.
  pte = reinterpret_cast<uint64_t*>(addr + PAGE_SIZE * 2);
  pte[0] = X86_MMU_PG_PS | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;

  offset = PAGE_SIZE * 3;
  target = reinterpret_cast<uint64_t*>(addr + offset);
  *target = 0xfeedfacefeedface;

  actual = 0;
  ASSERT_EQ(ZX_OK,
            arch_copy_from_guest(gpt, &actual, reinterpret_cast<void*>(offset), sizeof(actual)));
  ASSERT_EQ(0xfeedfacefeedface, actual);

  actual = 0xcafebeefcafebeef;
  ASSERT_EQ(ZX_OK,
            arch_copy_to_guest(gpt, reinterpret_cast<void*>(offset), &actual, sizeof(actual)));
  ASSERT_EQ(0xcafebeefcafebeef, *target);

  // Test 4: Copy with 4KB mapping.
  // PD entry pointing to PAGE_SIZE * 3
  pte = reinterpret_cast<uint64_t*>(addr + PAGE_SIZE * 2);
  pte[0] = PAGE_SIZE * 3 | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;
  // PT entry with 4KB page, pointing to PAGE_SIZE * 4.
  pte = reinterpret_cast<uint64_t*>(addr + PAGE_SIZE * 3);
  pte[4] = PAGE_SIZE * 4 | X86_MMU_PG_P | X86_MMU_PG_U | X86_MMU_PG_RW;

  offset = PAGE_SIZE * 4;
  target = reinterpret_cast<uint64_t*>(addr + offset);
  *target = 0xfeedfacefeedface;

  actual = 0;
  ASSERT_EQ(ZX_OK,
            arch_copy_from_guest(gpt, &actual, reinterpret_cast<void*>(offset), sizeof(actual)));
  ASSERT_EQ(0xfeedfacefeedface, actual);

  actual = 0xcafebeefcafebeef;
  ASSERT_EQ(ZX_OK,
            arch_copy_to_guest(gpt, reinterpret_cast<void*>(offset), &actual, sizeof(actual)));
  ASSERT_EQ(0xcafebeefcafebeef, *target);

  // Test 5: Copy across boundary, where we go from mapped to unmapped.
  // Set an offset, where half of the data is mapped, other other half is not.
  offset = PAGE_SIZE * 5 - sizeof(actual) / 2;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            arch_copy_from_guest(gpt, &actual, reinterpret_cast<void*>(offset), sizeof(actual)));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(x86_guest_copy)
UNITTEST("Exercise the guest copy logic", guest_copy)
UNITTEST_END_TESTCASE(x86_guest_copy, "x86-guest-copy", "x86-specific guest copy unit tests")
