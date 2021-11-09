// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <zircon/syscalls-next.h>
#include <zircon/syscalls.h>

#include <zxtest/zxtest.h>

#include "test_thread.h"
#include "userpager.h"

namespace pager_tests {

// Tests that a VMO created with TRAP_DIRTY can be supplied, and generates VMO_DIRTY requests when
// written to.
VMO_VMAR_TEST(SimpleTrapDirty) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));

  TestThread t1([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  // Supply the page first and then attempt to write to it.
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  TestThread t2([vmo]() -> bool {
    uint8_t data = 0x77;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  // No mechanism to resolve the dirty request for now. Nothing more to do beyond verifying the
  // generation of the request.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
  ASSERT_TRUE(t2.Wait());

  // Writes to a VMO created without TRAP_DIRTY go through without blocking.
  Vmo* vmo_no_trap;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo_no_trap));
  ASSERT_TRUE(pager.SupplyPages(vmo_no_trap, 0, 1));
  uint8_t data = 0xcc;
  ASSERT_OK(vmo_no_trap->vmo().write(&data, 0, sizeof(data)));

  // Verify that a non pager-backed vmo cannot be created with TRAP_DIRTY.
  zx_handle_t handle;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            zx_vmo_create(zx_system_get_page_size(), ZX_VMO_TRAP_DIRTY, &handle));
}

// Tests that writing to the VMO with zx_vmo_write generates DIRTY requests as expected.
TEST(Pager, DirtyRequestsOnVmoWrite) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  TestThread t([vmo]() -> bool {
    uint64_t data = 77;
    // write alternate pages {0, 2, 4, 6, 8}.
    for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
      if (vmo->vmo().write(&data, i * zx_system_get_page_size(), sizeof(data)) != ZX_OK) {
        return false;
      }
    }
    // write consecutive runs of pages too.
    // pages written at this point are [0] [2,3,4] [6] [8].
    if (vmo->vmo().write(&data, 3 * zx_system_get_page_size(), sizeof(data)) != ZX_OK) {
      return false;
    }
    uint8_t buf[5 * zx_system_get_page_size()];
    // pages written are [11, 16).
    return vmo->vmo().write(&buf, 11 * zx_system_get_page_size(), sizeof(buf)) == ZX_OK;
  });
  ASSERT_TRUE(t.Start());

  for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
    ASSERT_TRUE(t.WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageDirty(vmo, i, 1, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.DirtyPages(vmo, i, 1));
  }

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 3, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 3, 1));

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 11, 5, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 11, 5));

  ASSERT_TRUE(t.Wait());

  // No more requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that writing to the VMO through a VM mapping generates DIRTY requests as expected.
TEST(Pager, DirtyRequestsViaMapping) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  zx_vaddr_t ptr;
  TestThread t([vmo, &ptr]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   kNumPages * zx_system_get_page_size(), &ptr) != ZX_OK) {
      printf("could not map vmo\n");
      return false;
    }

    auto buf = reinterpret_cast<uint8_t*>(ptr);
    // write alternate pages {0, 2, 4, 6, 8}.
    for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
      buf[i * zx_system_get_page_size()] = 0xcc;
    }
    // write consecutive runs of pages too.
    // pages written at this point are [0] [2,3,4] [6] [8].
    buf[3 * zx_system_get_page_size()] = 0xcc;
    // pages written are [11, 16).
    for (uint64_t i = 11; i < 16; i++) {
      buf[i * zx_system_get_page_size()] = 0xcc;
    }
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, kNumPages * zx_system_get_page_size());
  });

  ASSERT_TRUE(t.Start());

  for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
    ASSERT_TRUE(t.WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageDirty(vmo, i, 1, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.DirtyPages(vmo, i, 1));
  }

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 3, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 3, 1));

  ASSERT_TRUE(t.WaitForBlocked());
  // We're touching pages one by one via the mapping, so we'll see page requests for individual
  // pages. Wait for the first page request and dirty the whole range.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 11, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 11, 5));

  ASSERT_TRUE(t.Wait());

  // No more requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that no DIRTY requests are generated for clones of pager-backed VMOs.
TEST(Pager, NoDirtyRequestsForClones) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 3;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  // Write to the clone.
  TestThread t1([&vmo = clone->vmo()]() -> bool {
    uint8_t data[kNumPages * zx_system_get_page_size()];
    return vmo.write(data, 0, kNumPages * zx_system_get_page_size()) == ZX_OK;
  });
  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  // Writing the pages in the clone should trigger faults in the parent. Wait to see the first one.
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // No dirty requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t1.Wait());

  // Write to the parent now. This should trigger dirty requests.
  TestThread t2([&vmo = vmo->vmo()]() -> bool {
    uint8_t data[kNumPages * zx_system_get_page_size()];
    return vmo.write(data, 0, kNumPages * zx_system_get_page_size()) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, kNumPages, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, kNumPages));

  // No remaining requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that writes for overlapping ranges generate the expected DIRTY requests.
TEST(Pager, DirtyRequestsOverlap) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  TestThread t1([vmo]() -> bool {
    // write pages [4,9).
    uint8_t data[5 * zx_system_get_page_size()];
    memset(data, 0xaa, sizeof(data));
    return vmo->vmo().write((void*)&data, 4 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());

  TestThread t2([vmo]() -> bool {
    // write pages [2,9).
    uint8_t data[7 * zx_system_get_page_size()];
    memset(data, 0xbb, sizeof(data));
    return vmo->vmo().write((void*)&data, 2 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 4, 5, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 2, 2, ZX_TIME_INFINITE));

  // Dirty the range [4,9).
  ASSERT_TRUE(pager.DirtyPages(vmo, 4, 5));
  ASSERT_TRUE(t1.Wait());

  // Dirty the range [2,4).
  ASSERT_TRUE(pager.DirtyPages(vmo, 2, 2));
  ASSERT_TRUE(t2.Wait());

  TestThread t3([vmo]() -> bool {
    // write pages [11,16).
    uint8_t data[5 * zx_system_get_page_size()];
    memset(data, 0xcc, sizeof(data));
    return vmo->vmo().write((void*)&data, 11 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t3.Start());
  ASSERT_TRUE(t3.WaitForBlocked());

  TestThread t4([vmo]() -> bool {
    // write pages [15,19).
    uint8_t data[4 * zx_system_get_page_size()];
    memset(data, 0xdd, sizeof(data));
    return vmo->vmo().write((void*)&data, 15 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t4.Start());
  ASSERT_TRUE(t4.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 11, 5, ZX_TIME_INFINITE));
  // No remaining requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));

  // Dirty the range [11,16).
  ASSERT_TRUE(pager.DirtyPages(vmo, 11, 5));

  // This should terminate t3, and wake up t3 until it blocks again for the remaining range.
  ASSERT_TRUE(t3.Wait());
  ASSERT_TRUE(t4.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 16, 3, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 16, 3));

  ASSERT_TRUE(t4.Wait());

  // No remaining requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
}

// Tests that DIRTY requests are generated as expected for a VMO that has random offsets in various
// page states: {Empty, Clean, Dirty}.
TEST(Pager, DirtyRequestsRandomOffsets) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  int page_state[kNumPages];  // 0 for empty, 1 for clean, and 2 for dirty
  for (uint64_t i = 0; i < kNumPages; i++) {
    page_state[i] = rand() % 3;
    if (page_state[i] == 0) {
      // Page not present. Skip ahead.
      continue;
    } else if (page_state[i] == 1) {
      // Page is present and clean.
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
    } else {
      // Page is present and dirty.
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
      ASSERT_TRUE(pager.DirtyPages(vmo, i, 1));
    }
  }

  // Now write to the entire range. We should see a combination of read and dirty requests.
  TestThread t([&vmo = vmo->vmo()]() -> bool {
    uint8_t data[kNumPages * zx_system_get_page_size()];
    return vmo.write(data, 0, kNumPages * zx_system_get_page_size()) == ZX_OK;
  });
  ASSERT_TRUE(t.Start());

  uint64_t clean_start = 0;
  uint64_t clean_len = 0;
  for (uint64_t i = 0; i < kNumPages; i++) {
    if (page_state[i] == 0) {
      // Page is not present.
      // This might break an in-progress clean run, resolve that first.
      if (clean_len > 0) {
        ASSERT_TRUE(t.WaitForBlocked());
        ASSERT_TRUE(pager.WaitForPageDirty(vmo, clean_start, clean_len, ZX_TIME_INFINITE));
        ASSERT_TRUE(pager.DirtyPages(vmo, clean_start, clean_len));
      }
      // Should see a read request for this page now.
      ASSERT_TRUE(t.WaitForBlocked());
      ASSERT_TRUE(pager.WaitForPageRead(vmo, i, 1, ZX_TIME_INFINITE));
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));

      // After the supply, visit this page again, as it might get combined into a subsequent clean
      // run. Set the page's state to clean, and decrement i.
      page_state[i] = 1;
      i--;

      clean_start = i + 1;
      clean_len = 0;

    } else if (page_state[i] == 1) {
      // Page is present and clean. Accumulate into the clean run.
      clean_len++;

    } else {
      // Page is present and dirty.
      // This might break an in-progress clean run, resolve that first.
      if (clean_len > 0) {
        ASSERT_TRUE(t.WaitForBlocked());
        ASSERT_TRUE(pager.WaitForPageDirty(vmo, clean_start, clean_len, ZX_TIME_INFINITE));
        ASSERT_TRUE(pager.DirtyPages(vmo, clean_start, clean_len));
      }
      clean_start = i + 1;
      clean_len = 0;
    }
  }

  // Resolve the last clean run if any.
  if (clean_len > 0) {
    ASSERT_TRUE(t.WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageDirty(vmo, clean_start, clean_len, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.DirtyPages(vmo, clean_start, clean_len));
  }

  // No remaining requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that ZX_PAGER_OP_FAIL can fail DIRTY page requests and propagate the failure up.
TEST(Pager, FailDirtyRequests) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 2;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  zx_vaddr_t ptr;
  TestThread t1([vmo, &ptr]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   zx_system_get_page_size(), &ptr) != ZX_OK) {
      printf("could not map vmo\n");
      return false;
    }
    // Write page 0.
    auto buf = reinterpret_cast<uint8_t*>(ptr);
    buf[0] = 0xcc;
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.FailPages(vmo, 0, 1));

  ASSERT_TRUE(t1.WaitForCrash(ptr, ZX_ERR_IO));

  TestThread t2([vmo]() -> bool {
    uint8_t data = 0xdd;
    // Write page 1.
    return vmo->vmo().write(&data, zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });

  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 1, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.FailPages(vmo, 1, 1));

  ASSERT_TRUE(t2.WaitForFailure());
}

}  // namespace pager_tests
