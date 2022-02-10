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
VMO_VMAR_TEST(PagerWriteback, SimpleTrapDirty) {
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

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  TestThread t2([vmo, &expected]() -> bool {
    uint8_t data = 0x77;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
  ASSERT_TRUE(t2.Wait());

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), check_vmar));

  // Writes to a VMO created without TRAP_DIRTY go through without blocking.
  Vmo* vmo_no_trap;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo_no_trap));
  ASSERT_TRUE(pager.SupplyPages(vmo_no_trap, 0, 1));
  uint8_t data = 0xcc;
  ASSERT_OK(vmo_no_trap->vmo().write(&data, 0, sizeof(data)));

  vmo_no_trap->GenerateBufferContents(expected.data(), 1, 0);
  expected[0] = data;
  ASSERT_TRUE(check_buffer_data(vmo_no_trap, 0, 1, expected.data(), check_vmar));

  // Verify that a non pager-backed vmo cannot be created with TRAP_DIRTY.
  zx_handle_t handle;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            zx_vmo_create(zx_system_get_page_size(), ZX_VMO_TRAP_DIRTY, &handle));
}

// Tests that writing to the VMO with zx_vmo_write generates DIRTY requests as expected.
TEST(PagerWriteback, DirtyRequestsOnVmoWrite) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  TestThread t([vmo, &expected]() -> bool {
    uint8_t data = 0x77;
    // write alternate pages {0, 2, 4, 6, 8}.
    for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
      expected[i * zx_system_get_page_size()] = data;
      if (vmo->vmo().write(&data, i * zx_system_get_page_size(), sizeof(data)) != ZX_OK) {
        return false;
      }
    }
    // write consecutive runs of pages too.
    // pages written at this point are [0] [2,3,4] [6] [8].
    expected[3 * zx_system_get_page_size()] = data;
    if (vmo->vmo().write(&data, 3 * zx_system_get_page_size(), sizeof(data)) != ZX_OK) {
      return false;
    }
    uint8_t buf[5 * zx_system_get_page_size()];
    memset(buf, 0, 5 * zx_system_get_page_size());
    memset(&expected[11 * zx_system_get_page_size()], 0, 5 * zx_system_get_page_size());
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

  // Verify dirty ranges.
  zx_vmo_dirty_range_t ranges[] = {{0, 1, 0}, {2, 3, 0}, {6, 1, 0}, {8, 1, 0}, {11, 5, 0}};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, ranges, sizeof(ranges) / sizeof(zx_vmo_dirty_range_t)));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // No more requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that writing to the VMO through a VM mapping generates DIRTY requests as expected.
TEST(PagerWriteback, DirtyRequestsViaMapping) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  zx_vaddr_t ptr;
  TestThread t([vmo, &ptr, &expected]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   kNumPages * zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
      return false;
    }

    uint8_t data = 0xcc;
    auto buf = reinterpret_cast<uint8_t*>(ptr);
    // write alternate pages {0, 2, 4, 6, 8}.
    for (uint64_t i = 0; i < kNumPages / 2; i += 2) {
      expected[i * zx_system_get_page_size()] = data;
      buf[i * zx_system_get_page_size()] = data;
    }
    // write consecutive runs of pages too.
    // pages written at this point are [0] [2,3,4] [6] [8].
    expected[3 * zx_system_get_page_size()] = data;
    buf[3 * zx_system_get_page_size()] = data;
    // pages written are [11, 16).
    for (uint64_t i = 11; i < 16; i++) {
      expected[i * zx_system_get_page_size()] = data;
      buf[i * zx_system_get_page_size()] = data;
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

  // Verify dirty ranges.
  zx_vmo_dirty_range_t ranges[] = {{0, 1, 0}, {2, 3, 0}, {6, 1, 0}, {8, 1, 0}, {11, 5, 0}};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, ranges, sizeof(ranges) / sizeof(zx_vmo_dirty_range_t)));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // No more requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that no DIRTY requests are generated on a read.
TEST(PagerWriteback, NoDirtyRequestsOnRead) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 3;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  zx_vaddr_t ptr;
  uint8_t tmp;
  TestThread t([vmo, &ptr, &tmp]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   kNumPages * zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
      return false;
    }

    auto buf = reinterpret_cast<uint8_t*>(ptr);
    // Read pages.
    for (uint64_t i = 0; i < kNumPages; i++) {
      tmp = buf[i * zx_system_get_page_size()];
    }
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, kNumPages * zx_system_get_page_size());
  });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  ASSERT_TRUE(t.Wait());

  // No dirty requests should be seen as none of the pages were dirtied.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));

  // Should be able to read from the VMO without faulting now.
  uint8_t buf[kNumPages * zx_system_get_page_size()];
  ASSERT_TRUE(vmo->vmo().read(buf, 0, kNumPages * zx_system_get_page_size()) == ZX_OK);

  // No dirty requests should be seen as none of the pages were dirtied.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));

  // No remaining reads.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // No dirty pages.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  // Verify contents.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that DIRTY requests are generated only on the first write.
TEST(PagerWriteback, DirtyRequestsRepeatedWrites) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  zx_vaddr_t ptr;
  TestThread t1([vmo, &ptr, &expected]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
      return false;
    }

    uint8_t data = 0xcc;
    expected[0] = data;
    *reinterpret_cast<uint8_t*>(ptr) = data;
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t1.Wait());

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page again.
  TestThread t2([ptr, &expected]() -> bool {
    uint8_t data = 0xdd;
    expected[0] = data;
    *reinterpret_cast<uint8_t*>(ptr) = data;
    return true;
  });

  ASSERT_TRUE(t2.Start());

  // No more requests seen.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t2.Wait());

  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
}

// Tests that DIRTY requests are generated on a write to a page that was previously read from.
TEST(PagerWriteback, DirtyRequestsOnWriteAfterRead) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  zx_vaddr_t ptr;
  uint8_t tmp;
  TestThread t1([vmo, &ptr, &tmp]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
      return false;
    }

    // Read from the page.
    tmp = *reinterpret_cast<uint8_t*>(ptr);
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  ASSERT_TRUE(t1.Start());

  // No read or dirty requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t1.Wait());

  // Now write to the page. This should trigger a dirty request.
  TestThread t2([ptr, &expected]() -> bool {
    uint8_t data = 0xdd;
    expected[0] = data;
    *reinterpret_cast<uint8_t*>(ptr) = data;
    return true;
  });

  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t2.Wait());

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // No more requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that no DIRTY requests are generated for clones of pager-backed VMOs.
TEST(PagerWriteback, NoDirtyRequestsForClones) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 3;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  // Write to the clone.
  TestThread t1([&vmo = clone->vmo()]() -> bool {
    uint8_t data[kNumPages * zx_system_get_page_size()];
    memset(data, 0xc, kNumPages * zx_system_get_page_size());
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

  // No dirty pages.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  for (uint64_t i = 0; i < kNumPages; i++) {
    uint8_t expected[zx_system_get_page_size()];
    memset(expected, 0xc, zx_system_get_page_size());
    uint8_t data[zx_system_get_page_size()];
    ASSERT_OK(clone->vmo().read(data, i * zx_system_get_page_size(), zx_system_get_page_size()));
    ASSERT_EQ(0, memcmp(expected, data, zx_system_get_page_size()));
  }

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // Write to the parent now. This should trigger dirty requests.
  TestThread t2([&vmo = vmo->vmo()]() -> bool {
    uint8_t data[kNumPages * zx_system_get_page_size()];
    memset(data, 0xd, kNumPages * zx_system_get_page_size());
    return vmo.write(data, 0, kNumPages * zx_system_get_page_size()) == ZX_OK;
  });
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, kNumPages, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, kNumPages));

  ASSERT_TRUE(t2.Wait());

  // Should now see the pages dirty.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = kNumPages};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  memset(expected.data(), 0xd, kNumPages * zx_system_get_page_size());
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // No remaining requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that writes for overlapping ranges generate the expected DIRTY requests.
TEST(PagerWriteback, DirtyRequestsOverlap) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 20;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  TestThread t1([vmo]() -> bool {
    // write pages [4,9).
    uint8_t data[5 * zx_system_get_page_size()];
    memset(data, 0xaa, sizeof(data));
    return vmo->vmo().write((void*)&data, 4 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());

  TestThread t2([vmo, &expected]() -> bool {
    // write pages [2,9).
    uint8_t data[7 * zx_system_get_page_size()];
    memset(data, 0xbb, sizeof(data));
    memset(expected.data() + 2 * zx_system_get_page_size(), 0xbb, sizeof(data));
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

  // Verify dirty ranges.
  std::vector<zx_vmo_dirty_range_t> ranges;
  ranges.push_back({.offset = 2, .length = 7});
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, ranges.data(), ranges.size()));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  TestThread t3([vmo, &expected]() -> bool {
    // write pages [11,16).
    uint8_t data[5 * zx_system_get_page_size()];
    memset(data, 0xcc, sizeof(data));
    memset(expected.data() + 11 * zx_system_get_page_size(), 0xcc, sizeof(data));
    return vmo->vmo().write((void*)&data, 11 * zx_system_get_page_size(), sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t3.Start());
  ASSERT_TRUE(t3.WaitForBlocked());

  TestThread t4([vmo, &expected]() -> bool {
    // write pages [15,19).
    uint8_t data[4 * zx_system_get_page_size()];
    memset(data, 0xdd, sizeof(data));
    memset(expected.data() + 15 * zx_system_get_page_size(), 0xdd, sizeof(data));
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

  // Verify dirty ranges.
  ranges.push_back({.offset = 11, .length = 5});
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, ranges.data(), ranges.size()));

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 16, 3, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 16, 3));

  ASSERT_TRUE(t4.Wait());

  // Verify dirty ranges.
  ranges.back().length = 8;
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, ranges.data(), ranges.size()));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // No remaining requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
}

// Tests that DIRTY requests are generated as expected for a VMO that has random offsets in various
// page states: {Empty, Clean, Dirty}.
TEST(PagerWriteback, DirtyRequestsRandomOffsets) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 10;
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

  ASSERT_TRUE(t.Wait());

  // No remaining requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that ZX_PAGER_OP_FAIL can fail DIRTY page requests and propagate the failure up.
TEST(PagerWriteback, FailDirtyRequests) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 2;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  zx_vaddr_t ptr;
  TestThread t1([vmo, &ptr]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
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

  // No pages should be dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

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

  // No pages should be dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that DIRTY requests are generated when offsets with zero page markers are written to.
TEST(PagerWriteback, DirtyRequestsForZeroPages) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint64_t kNumPages = 2;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  // Supply with empty source vmo so that the destination gets zero page markers.
  zx::vmo vmo_src;
  ASSERT_OK(zx::vmo::create(kNumPages * zx_system_get_page_size(), 0, &vmo_src));
  ASSERT_OK(
      pager.pager().supply_pages(vmo->vmo(), 0, kNumPages * zx_system_get_page_size(), vmo_src, 0));

  // Verify that the pager vmo has no committed pages, i.e. it only has markers.
  zx_info_vmo_t info;
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(0, info.committed_bytes);

  // No dirty pages yet.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);

  // Write to the first page with zx_vmo_write.
  TestThread t1([vmo, &expected]() -> bool {
    uint8_t data = 0xaa;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // Dirty the first page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  // Verify that the pager vmo has one committed page now.
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(zx_system_get_page_size(), info.committed_bytes);

  // Verify that the page is dirty.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  zx_vaddr_t ptr;
  // Map the second page of the vmo.
  ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(),
                                       zx_system_get_page_size(), zx_system_get_page_size(), &ptr));

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  // Write to the second page via the mapping.
  auto buf = reinterpret_cast<uint8_t*>(ptr);
  uint8_t data = 0xbb;
  TestThread t2([buf, data, &expected]() -> bool {
    *buf = data;
    expected[zx_system_get_page_size()] = data;
    return true;
  });

  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 1, 1, ZX_TIME_INFINITE));

  // Dirty the second page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 1, 1));
  ASSERT_TRUE(t2.Wait());

  // Verify that the pager vmo has both pages committed now.
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(kNumPages * zx_system_get_page_size(), info.committed_bytes);

  // Verify that both the pages are now dirty.
  range = {.offset = 0, .length = 2};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that ZX_PAGER_OP_DIRTY works for a mix of zero and non-zero pages.
TEST(PagerWriteback, DirtyZeroAndNonZeroPages) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint64_t kNumPages = 10;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);

  // Empty source vmo to supply with zero pages.
  zx::vmo vmo_src;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo_src));

  // For each page offset, supply either a zero or a non-zero page.
  uint64_t non_zero_count = 0;
  for (uint64_t i = 0; i < kNumPages; i++) {
    if (rand() % 2) {
      non_zero_count++;
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
      vmo->GenerateBufferContents(expected.data() + i * zx_system_get_page_size(), 1, i);
    } else {
      ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), i * zx_system_get_page_size(),
                                           zx_system_get_page_size(), vmo_src, 0));
    }
  }

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // Only non-zero pages should be committed.
  zx_info_vmo_t info;
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(non_zero_count * zx_system_get_page_size(), info.committed_bytes);

  // No dirty pages yet.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  // Verify that we're able to dirty the entire range regardless of the type of page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, kNumPages));

  // All the pages should be committed and dirty now.
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(kNumPages * zx_system_get_page_size(), info.committed_bytes);
  zx_vmo_dirty_range_t range = {.offset = 0, .length = kNumPages};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that ZX_PAGER_OP_FAIL can fail DIRTY page requests for zero pages.
TEST(PagerWriteback, FailDirtyRequestsForZeroPages) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));

  // Supply with empty source vmo so that the destination gets zero page markers.
  zx::vmo vmo_src;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo_src));
  ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), 0, zx_system_get_page_size(), vmo_src, 0));

  // Verify that the pager vmo has no committed pages, i.e. it only has markers.
  zx_info_vmo_t info;
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(0, info.committed_bytes);

  // No dirty pages yet.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);

  // Attempt to write to the first page.
  TestThread t([vmo]() -> bool {
    uint8_t data = 0xaa;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });
  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // Fail the dirty request.
  ASSERT_TRUE(pager.FailPages(vmo, 0, 1));

  // The thread should exit with failure.
  ASSERT_TRUE(t.WaitForFailure());

  // No committed pages still.
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(0, info.committed_bytes);

  // No dirty pages too.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
}

// Tests that DIRTY requests are generated for ranges including zero pages as expected.
TEST(PagerWriteback, DirtyRequestsForZeroRanges) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint64_t kNumPages = 10;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);

  // Empty source vmo to supply with zero pages.
  zx::vmo vmo_src;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo_src));

  // Supply zero page markers for pages 0 and 1.
  ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), 0, zx_system_get_page_size(), vmo_src, 0));
  ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), zx_system_get_page_size(),
                                       zx_system_get_page_size(), vmo_src, 0));

  // Attempt to write to the range [0, 2).
  TestThread t1([vmo, &expected]() -> bool {
    std::vector<uint8_t> data(2 * zx_system_get_page_size(), 0xaa);
    memset(expected.data(), 0xaa, 2 * zx_system_get_page_size());
    return vmo->vmo().write(data.data(), 0, 2 * zx_system_get_page_size()) == ZX_OK;
  });

  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());

  // We should see a dirty request for the range [0, 2). Verifies that the range is extended to
  // include another marker.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 2, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 2));
  ASSERT_TRUE(t1.Wait());

  // Verify dirty pages.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = 2};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 2, expected.data(), true));

  // Supply a zero marker for page 2 and a non-zero page for page 3.
  ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), 2 * zx_system_get_page_size(),
                                       zx_system_get_page_size(), vmo_src, 0));
  ASSERT_TRUE(pager.SupplyPages(vmo, 3, 1));

  // Attempt to write to the range [2, 4).
  TestThread t2([vmo, &expected]() -> bool {
    std::vector<uint8_t> data(2 * zx_system_get_page_size(), 0xbb);
    memset(expected.data() + 2 * zx_system_get_page_size(), 0xbb, 2 * zx_system_get_page_size());
    return vmo->vmo().write(data.data(), 2 * zx_system_get_page_size(),
                            2 * zx_system_get_page_size()) == ZX_OK;
  });

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  // We should see a dirty request for the range [2, 4). Verifies that the range is extended to
  // include a non-zero clean page.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 2, 2, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 2, 2));
  ASSERT_TRUE(t2.Wait());

  // Verify dirty pages.
  range = {.offset = 0, .length = 4};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 4, expected.data(), true));

  // For the rest of the pages, supply a mix of zero and non-zero pages, leaving a gap at the end.
  for (uint64_t i = 4; i < kNumPages - 1; i++) {
    if (rand() % 2) {
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
    } else {
      ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), i * zx_system_get_page_size(),
                                           zx_system_get_page_size(), vmo_src, 0));
    }
  }

  // Attempt to write to the range [4, 10).
  TestThread t3([vmo, &expected]() -> bool {
    size_t len = kNumPages - 4;
    std::vector<uint8_t> data(len * zx_system_get_page_size(), 0xcc);
    memset(expected.data() + 4 * zx_system_get_page_size(), 0xcc, len * zx_system_get_page_size());
    return vmo->vmo().write(data.data(), 4 * zx_system_get_page_size(),
                            len * zx_system_get_page_size()) == ZX_OK;
  });

  ASSERT_TRUE(t3.Start());
  ASSERT_TRUE(t3.WaitForBlocked());

  // We should see a dirty request for pages [4, 9). Verifies that zero and non-zero clean pages get
  // picked up in a single range, and that the range stops before a gap.
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 4, 5, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 4, 5));
  ASSERT_TRUE(t3.WaitForBlocked());

  // We should now see a read request followed by a dirty request for the last gap.
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 9, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 9, 1));
  ASSERT_TRUE(t3.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 9, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 9, 1));
  ASSERT_TRUE(t3.Wait());

  // Verify dirty pages.
  range = {.offset = 0, .length = kNumPages};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that no DIRTY requests are generated on a commit.
TEST(PagerWriteback, NoDirtyRequestsOnCommit) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 5;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  // Supply some pages.
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 2));

  // Commit the vmo.
  TestThread t([vmo]() -> bool {
    return vmo->vmo().op_range(ZX_VMO_OP_COMMIT, 0, kNumPages * zx_system_get_page_size(), nullptr,
                               0) == ZX_OK;
  });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  // Should see a read request for the uncommitted portion.
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 2, kNumPages - 2, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 2, kNumPages - 2));

  // The thread should be able to exit now.
  ASSERT_TRUE(t.Wait());

  // No dirty requests should be seen as none of the pages were dirtied.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));

  // No remaining reads.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // No dirty pages.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
}

// Tests that no DIRTY requests are generated when a mapping is created with MAP_RANGE.
TEST(PagerWriteback, NoDirtyRequestsOnMapRange) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 3;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(kNumPages, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), kNumPages, 0);

  zx_vaddr_t ptr;
  TestThread t1([vmo, &ptr]() -> bool {
    // Map the vmo, and populate mappings for all committed pages. We know the pages are
    // pre-committed so we should not block on reads. And we should not be generating any dirty
    // requests to block on either.
    return zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE | ZX_VM_MAP_RANGE, 0,
                                      vmo->vmo(), 0, kNumPages * zx_system_get_page_size(),
                                      &ptr) == ZX_OK;
  });
  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, kNumPages * zx_system_get_page_size());
  });

  ASSERT_TRUE(t1.Start());

  // No dirty requests should be seen as none of the pages were dirtied.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  // No reads either.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t1.Wait());

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  uint8_t tmp;
  TestThread t2([&ptr, &tmp]() -> bool {
    // Read the mapped pages. This will not block.
    auto buf = reinterpret_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < kNumPages; i++) {
      tmp = buf[i * zx_system_get_page_size()];
    }
    return true;
  });

  ASSERT_TRUE(t2.Start());

  // No dirty or read requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t2.Wait());

  // No dirty pages.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  TestThread t3([&ptr, &expected]() -> bool {
    // Now try to write to the vmo. This should result in write faults and dirty requests.
    auto buf = reinterpret_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < kNumPages; i++) {
      uint8_t data = 0xcc;
      buf[i * zx_system_get_page_size()] = data;
      expected[i * zx_system_get_page_size()] = data;
    }
    return true;
  });

  ASSERT_TRUE(t3.Start());

  // The thread will block on dirty requests for each page.
  for (uint64_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(t3.WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageDirty(vmo, i, 1, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.DirtyPages(vmo, i, 1));
  }

  // The thread should now exit.
  ASSERT_TRUE(t3.Wait());

  // All pages are dirty now.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = kNumPages};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // No more dirty or read requests.
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that no DIRTY requests are generated when previously dirty pages are mapped and written to.
TEST(PagerWriteback, NoDirtyRequestsMapExistingDirty) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  // Dirty the page.
  TestThread t1([vmo, &expected]() -> bool {
    uint8_t data = 0xcc;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });

  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t1.Wait());

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Map the page and try writing to it.
  zx_vaddr_t ptr;
  TestThread t2([vmo, &ptr, &expected]() -> bool {
    // Map the vmo.
    if (zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                   zx_system_get_page_size(), &ptr) != ZX_OK) {
      fprintf(stderr, "could not map vmo\n");
      return false;
    }

    uint8_t data = 0xdd;
    *reinterpret_cast<uint8_t*>(ptr) = data;
    expected[0] = data;
    return true;
  });

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  ASSERT_TRUE(t2.Start());

  // No read or dirty requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  ASSERT_TRUE(t2.Wait());

  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
}

// Tests that dirty ranges cannot be queried on a clone.
TEST(PagerWriteback, NoQueryOnClone) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  // Dirty the page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  // Create a clone.
  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  // Write to the clone.
  uint8_t data = 0x77;
  ASSERT_OK(clone->vmo().write(&data, 0, sizeof(data)));

  // Can query dirty ranges on the parent.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Cannot query dirty ranges on the clone.
  uint64_t num_ranges = 0;
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            zx_pager_query_dirty_ranges(pager.pager().get(), clone->vmo().get(), 0,
                                        zx_system_get_page_size(), &range, sizeof(range),
                                        &num_ranges, nullptr));
}

// Tests that WRITEBACK_BEGIN/END clean pages as expected.
TEST(PagerWriteback, SimpleWriteback) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  // Dirty the page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Begin writeback on the page.
  ASSERT_TRUE(pager.WritebackBeginPages(vmo, 0, 1));

  // The page is still dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  // This should transition the page to clean, and a subsequent write should trigger
  // another dirty request.
  ASSERT_TRUE(pager.WritebackEndPages(vmo, 0, 1));

  // No dirty pages after writeback end.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  TestThread t([vmo, &expected]() -> bool {
    uint8_t data = 0x77;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });

  ASSERT_TRUE(t.Start());

  // We should see a dirty request now.
  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());

  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
}

// Tests that a write after WRITEBACK_BEGIN but before WRITEBACK_END is handled correctly.
TEST(PagerWriteback, DirtyDuringWriteback) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  // Dirty the page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  // Begin writeback on the page.
  ASSERT_TRUE(pager.WritebackBeginPages(vmo, 0, 1));

  // The page is still dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page before ending writeback. This should generate a dirty request.
  TestThread t1([vmo, &expected]() -> bool {
    uint8_t data = 0xcc;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });

  ASSERT_TRUE(t1.Start());

  // Verify that we saw the dirty request but do not acknowledge it yet. The write will remain
  // blocked.
  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // End the writeback. This should transition the page to clean.
  ASSERT_TRUE(pager.WritebackEndPages(vmo, 0, 1));
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));

  // The writing thread is still blocked.
  ASSERT_TRUE(t1.WaitForBlocked());

  // Now dirty the page, unblocking the writing thread.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  // The page is dirty again.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Begin another writeback, and try writing again before ending it. This time acknowledge the
  // dirty request while the writeback is in progress.
  ASSERT_TRUE(pager.WritebackBeginPages(vmo, 0, 1));
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  // Write to the page before ending writeback. This should generate a dirty request.
  TestThread t2([vmo, &expected]() -> bool {
    uint8_t data = 0xdd;
    expected[0] = data;
    return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
  });

  ASSERT_TRUE(t2.Start());

  // Verify that we saw the dirty request.
  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // This should reset the page state to dirty so that it is not moved to clean when the writeback
  // ends later.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t2.Wait());

  // Verify that the page is dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Now end the writeback. This should *not* clean the page, as a write was accepted after
  // beginning the writeback.
  ASSERT_TRUE(pager.WritebackEndPages(vmo, 0, 1));
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
}

// Tests that mapping write permissions are cleared as expected on writeback.
TEST(PagerWriteback, WritebackWithMapping) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmoWithOptions(1, ZX_VMO_TRAP_DIRTY, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  zx_vaddr_t ptr;
  // Map the vmo.
  ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                       zx_system_get_page_size(), &ptr));

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  // Write to the vmo. This will be trapped and generate a dirty request.
  auto buf = reinterpret_cast<uint8_t*>(ptr);
  uint8_t data = 0xaa;
  TestThread t1([buf, data, &expected]() -> bool {
    *buf = data;
    expected[0] = data;
    return true;
  });

  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // Dirty the page.
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  // Verify that the page is dirty.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page again. This should go through without any page faults / dirty requests.
  data = 0xbb;
  *buf = data;
  expected[0] = data;
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Start a writeback.
  ASSERT_TRUE(pager.WritebackBeginPages(vmo, 0, 1));
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page again. This should result in a fault / dirty request.
  TestThread t2([buf]() -> bool {
    *buf = 0xcc;
    return true;
  });

  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));

  // Fail the dirty request so the writeback can complete.
  ASSERT_TRUE(pager.FailPages(vmo, 0, 1));
  ASSERT_TRUE(t2.WaitForCrash(ptr, ZX_ERR_IO));

  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Complete the writeback, making the page clean.
  ASSERT_TRUE(pager.WritebackEndPages(vmo, 0, 1));
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page again. This should again be trapped.
  data = 0xdd;
  TestThread t3([buf, data, &expected]() -> bool {
    *buf = data;
    expected[0] = data;
    return true;
  });

  ASSERT_TRUE(t3.Start());

  ASSERT_TRUE(t3.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));

  ASSERT_TRUE(t3.Wait());

  // The page is dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_EQ(data, *buf);
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
}

// Tests that the zero page marker cannot be overwritten by another page, unless written to at which
// point it is forked.
TEST(PagerWriteback, CannotOverwriteZeroPage) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  uint32_t create_options[] = {0, ZX_VMO_TRAP_DIRTY};

  for (auto create_option : create_options) {
    Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmoWithOptions(1, create_option, &vmo));

    // Supply with empty source vmo so that the destination gets zero page markers.
    zx::vmo vmo_src;
    ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo_src));
    ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), 0, zx_system_get_page_size(), vmo_src, 0));

    // Verify that the pager vmo has no committed pages, i.e. it only has markers.
    zx_info_vmo_t info;
    ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    ASSERT_EQ(0, info.committed_bytes);

    // Buffer to verify VMO contents later.
    std::vector<uint8_t> expected(zx_system_get_page_size(), 0);

    // No dirty pages yet.
    ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
    ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

    // Commit a page in the source to attempt another supply.
    uint8_t data = 0xaa;
    ASSERT_OK(vmo_src.write(&data, 0, sizeof(data)));

    // Supplying the same page again should not overwrite the zero page marker. The supply will
    // succeed as a no-op.
    ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), 0, zx_system_get_page_size(), vmo_src, 0));

    // No committed pages still.
    ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    ASSERT_EQ(0, info.committed_bytes);

    // The VMO is still all zeros.
    ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

    // Now write to the VMO. This should fork the zero page.
    TestThread t1([vmo, &expected]() -> bool {
      uint8_t data = 0xbb;
      expected[0] = data;
      return vmo->vmo().write(&data, 0, sizeof(data)) == ZX_OK;
    });
    ASSERT_TRUE(t1.Start());

    // Wait for and acknowledge the dirty request if configured to trap dirty transitions.
    if (create_option == ZX_VMO_TRAP_DIRTY) {
      ASSERT_TRUE(t1.WaitForBlocked());
      ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
      // Dirty the first page.
      ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
    }

    ASSERT_TRUE(t1.Wait());

    // Verify that the pager vmo has one committed page now.
    ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    ASSERT_EQ(zx_system_get_page_size(), info.committed_bytes);

    // Verify that the page is dirty.
    zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
    ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

    // Verify written data.
    ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
  }
}

// Tests that VMOs created without the ZX_VMO_TRAP_DIRTY flag track dirty pages as expected.
TEST(PagerWriteback, SimpleDirtyNoTrap) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  // Create a VMO without the ZX_VMO_TRAP_DIRTY flag.
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
  vmo->GenerateBufferContents(expected.data(), 1, 0);

  // No dirty pages yet.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Write to the page now. This should go through without blocking.
  uint8_t data = 0x77;
  expected[0] = data;
  ASSERT_OK(vmo->vmo().write(&data, 0, sizeof(data)));

  // We should now have one dirty page.
  zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  // Verify written data.
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Begin writeback on the page.
  ASSERT_TRUE(pager.WritebackBeginPages(vmo, 0, 1));

  // The page is still dirty.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));

  // This should transition the page to clean, and a subsequent write should trigger
  // another dirty request.
  ASSERT_TRUE(pager.WritebackEndPages(vmo, 0, 1));

  // No dirty pages after writeback end.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // Map the vmo.
  zx_vaddr_t ptr;
  ASSERT_OK(zx::vmar::root_self()->map(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, vmo->vmo(), 0,
                                       zx_system_get_page_size(), &ptr));

  auto unmap = fit::defer([&]() {
    // Cleanup the mapping we created.
    zx::vmar::root_self()->unmap(ptr, zx_system_get_page_size());
  });

  // Write to the vmo again via the mapping.
  auto buf = reinterpret_cast<uint8_t*>(ptr);
  data = 0x55;
  *buf = data;
  expected[0] = data;

  // The page should get dirtied again.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));

  // No dirty or read requests seen.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageDirtyRequest(vmo, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that VMOs created without the ZX_VMO_TRAP_DIRTY flag track dirty pages as expected for a
// random mix of zero and non-zero pages.
TEST(PagerWriteback, DirtyNoTrapRandomOffsets) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  // Create a VMO without the ZX_VMO_TRAP_DIRTY flag.
  Vmo* vmo;
  constexpr uint64_t kNumPages = 10;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  // Buffer to verify VMO contents later.
  std::vector<uint8_t> expected(kNumPages * zx_system_get_page_size(), 0);

  // Empty source vmo to supply with zero pages.
  zx::vmo vmo_src;
  ASSERT_OK(zx::vmo::create(zx_system_get_page_size(), 0, &vmo_src));

  // For each page offset, supply either a zero or a non-zero page.
  uint64_t non_zero_count = 0;
  for (uint64_t i = 0; i < kNumPages; i++) {
    if (rand() % 2) {
      non_zero_count++;
      ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
      vmo->GenerateBufferContents(expected.data() + i * zx_system_get_page_size(), 1, i);
    } else {
      ASSERT_OK(pager.pager().supply_pages(vmo->vmo(), i * zx_system_get_page_size(),
                                           zx_system_get_page_size(), vmo_src, 0));
    }
  }

  // Only non-zero pages should be committed.
  zx_info_vmo_t info;
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(non_zero_count * zx_system_get_page_size(), info.committed_bytes);

  // No dirty pages yet.
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, nullptr, 0));
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));

  // Verify that we're able to write to the entire range regardless of the type of page. Alter the
  // expected contents to verify later.
  uint8_t data = 0x77;
  for (uint64_t i = 0; i < kNumPages; i++) {
    expected[i * zx_system_get_page_size()] = data++;
  }
  ASSERT_OK(vmo->vmo().write(expected.data(), 0, kNumPages * zx_system_get_page_size()));

  // All the pages should be committed and dirty now.
  ASSERT_OK(vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  ASSERT_EQ(kNumPages * zx_system_get_page_size(), info.committed_bytes);
  zx_vmo_dirty_range_t range = {.offset = 0, .length = kNumPages};
  ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
  ASSERT_TRUE(check_buffer_data(vmo, 0, kNumPages, expected.data(), true));
}

// Tests that adding the WRITE permission with zx_vmar_protect does not override read-only mappings
// required in order to track dirty transitions.
TEST(PagerWriteback, DirtyAfterMapProtect) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  uint32_t create_options[] = {0, ZX_VMO_TRAP_DIRTY};

  for (auto create_option : create_options) {
    // Create a temporary VMAR to work with.
    zx::vmar vmar;
    zx_vaddr_t base_addr;
    ASSERT_OK(zx::vmar::root_self()->allocate(ZX_VM_CAN_MAP_READ | ZX_VM_CAN_MAP_WRITE, 0,
                                              zx_system_get_page_size(), &vmar, &base_addr));

    Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmoWithOptions(1, create_option, &vmo));
    ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

    // Buffer to verify VMO contents later.
    std::vector<uint8_t> expected(zx_system_get_page_size(), 0);
    vmo->GenerateBufferContents(expected.data(), 1, 0);

    zx_vaddr_t ptr;
    // Map the vmo read-only first so that the protect step below is not a no-op.
    ASSERT_OK(vmar.map(ZX_VM_PERM_READ, 0, vmo->vmo(), 0, zx_system_get_page_size(), &ptr));

    auto unmap = fit::defer([&]() {
      // Cleanup the mapping we created.
      vmar.unmap(ptr, zx_system_get_page_size());
    });

    // Read the VMO through the mapping so that the hardware mapping is created.
    uint8_t data = *reinterpret_cast<uint8_t*>(ptr);
    ASSERT_EQ(data, expected[0]);

    // Add the write permission now. This will allow us to write to the VMO below.
    ASSERT_OK(vmar.protect(ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, ptr, zx_system_get_page_size()));

    // Write to the vmo. This should trigger a write fault. If the protect above added the write
    // permission on the hardware mapping, this write will go through without generating a write
    // fault for dirty tracking.
    auto buf = reinterpret_cast<uint8_t*>(ptr);
    data = 0xaa;
    TestThread t([buf, data, &expected]() -> bool {
      *buf = data;
      expected[0] = data;
      return true;
    });

    ASSERT_TRUE(t.Start());

    if (create_option == ZX_VMO_TRAP_DIRTY) {
      ASSERT_TRUE(t.WaitForBlocked());
      ASSERT_TRUE(pager.WaitForPageDirty(vmo, 0, 1, ZX_TIME_INFINITE));
      // Dirty the page.
      ASSERT_TRUE(pager.DirtyPages(vmo, 0, 1));
    }
    ASSERT_TRUE(t.Wait());

    // Verify that the page is dirty.
    zx_vmo_dirty_range_t range = {.offset = 0, .length = 1};
    ASSERT_TRUE(pager.VerifyDirtyRanges(vmo, &range, 1));
    ASSERT_EQ(data, *buf);
    ASSERT_TRUE(check_buffer_data(vmo, 0, 1, expected.data(), true));
  }
}

}  // namespace pager_tests
