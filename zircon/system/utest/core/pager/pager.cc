// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fzl/memory-probe.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/port.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>

#include <iterator>
#include <memory>
#include <vector>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <zxtest/zxtest.h>

#include "test_thread.h"
#include "userpager.h"

__BEGIN_CDECLS
__WEAK extern zx_handle_t get_root_resource(void);
__END_CDECLS

namespace pager_tests {

static bool check_buffer_data(Vmo* vmo, uint64_t offset, uint64_t len, const void* data,
                              bool check_vmar) {
  return check_vmar ? vmo->CheckVmar(offset, len, data) : vmo->CheckVmo(offset, len, data);
}

static bool check_buffer(Vmo* vmo, uint64_t offset, uint64_t len, bool check_vmar) {
  return check_vmar ? vmo->CheckVmar(offset, len) : vmo->CheckVmo(offset, len);
}

#define VMO_VMAR_TEST(fn_name)                   \
  void fn_name(bool);                            \
  TEST(Pager, fn_name##_vmar) { fn_name(true); } \
  TEST(Pager, fn_name##_vmo) { fn_name(false); } \
  void fn_name(bool check_vmar)

// Simple test that checks that a single thread can access a single page.
VMO_VMAR_TEST(SinglePageTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
}

// Test that a fault can be fulfilled with an uncommitted page.
VMO_VMAR_TEST(UncommittedSinglePageTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  std::vector<uint8_t> data(PAGE_SIZE, 0);

  TestThread t([vmo, check_vmar, &data]() -> bool {
    return check_buffer_data(vmo, 0, 1, data.data(), check_vmar);
  });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  zx::vmo empty;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &empty));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1, std::move(empty)));

  ASSERT_TRUE(t.Wait());
}

// Tests that pre-supplied pages don't result in requests.
VMO_VMAR_TEST(PresupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that supplies between the request and reading the port
// causes the request to be aborted.
VMO_VMAR_TEST(EarlySupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));

  TestThread t1([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  // Use a second thread to make sure the queue of requests is flushed.
  TestThread t2([vmo, check_vmar]() -> bool { return check_buffer(vmo, 1, 1, check_vmar); });

  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));
  ASSERT_TRUE(t1.Wait());

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 1, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));
  ASSERT_TRUE(t2.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Checks that a single thread can sequentially access multiple pages.
VMO_VMAR_TEST(SequentialMultipageTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint32_t kNumPages = 32;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, kNumPages, check_vmar); });

  ASSERT_TRUE(t.Start());

  for (unsigned i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i, 1, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
  }

  ASSERT_TRUE(t.Wait());
}

// Tests that multiple threads can concurrently access different pages.
VMO_VMAR_TEST(ConcurrentMultipageAccessTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  TestThread t2([vmo, check_vmar]() -> bool { return check_buffer(vmo, 1, 1, check_vmar); });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 1, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 2));

  ASSERT_TRUE(t.Wait());
  ASSERT_TRUE(t2.Wait());
}

// Tests that multiple threads can concurrently access a single page.
VMO_VMAR_TEST(ConcurrentOverlappingAccessTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  constexpr uint64_t kNumThreads = 32;
  std::unique_ptr<TestThread> threads[kNumThreads];
  for (unsigned i = 0; i < kNumThreads; i++) {
    threads[i] = std::make_unique<TestThread>(
        [vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

    threads[i]->Start();
    ASSERT_TRUE(threads[i]->WaitForBlocked());
  }

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  for (unsigned i = 0; i < kNumThreads; i++) {
    ASSERT_TRUE(threads[i]->Wait());
  }

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that multiple threads can concurrently access multiple pages and
// be satisfied by a single supply operation.
VMO_VMAR_TEST(BulkSingleSupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint32_t kNumPages = 8;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  std::unique_ptr<TestThread> ts[kNumPages];
  for (unsigned i = 0; i < kNumPages; i++) {
    ts[i] = std::make_unique<TestThread>(
        [vmo, i, check_vmar]() -> bool { return check_buffer(vmo, i, 1, check_vmar); });
    ASSERT_TRUE(ts[i]->Start());
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i, 1, ZX_TIME_INFINITE));
  }

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  for (unsigned i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(ts[i]->Wait());
  }
}

// Test body for odd supply tests.
void BulkOddSupplyTestInner(bool check_vmar, bool use_src_offset) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  // Interesting supply lengths that will exercise splice logic.
  constexpr uint64_t kSupplyLengths[] = {2, 3, 5, 7, 37, 5, 13, 23};
  uint64_t sum = 0;
  for (unsigned i = 0; i < std::size(kSupplyLengths); i++) {
    sum += kSupplyLengths[i];
  }

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(sum, &vmo));

  uint64_t page_idx = 0;
  for (unsigned supply_idx = 0; supply_idx < std::size(kSupplyLengths); supply_idx++) {
    uint64_t supply_len = kSupplyLengths[supply_idx];
    uint64_t offset = page_idx;

    std::unique_ptr<TestThread> ts[kSupplyLengths[supply_idx]];
    for (uint64_t j = 0; j < kSupplyLengths[supply_idx]; j++) {
      uint64_t thread_offset = offset + j;
      ts[j] = std::make_unique<TestThread>([vmo, thread_offset, check_vmar]() -> bool {
        return check_buffer(vmo, thread_offset, 1, check_vmar);
      });
      ASSERT_TRUE(ts[j]->Start());
      ASSERT_TRUE(pager.WaitForPageRead(vmo, thread_offset, 1, ZX_TIME_INFINITE));
    }

    uint64_t src_offset = use_src_offset ? offset : 0;
    ASSERT_TRUE(pager.SupplyPages(vmo, offset, supply_len, src_offset));

    for (unsigned i = 0; i < kSupplyLengths[supply_idx]; i++) {
      ASSERT_TRUE(ts[i]->Wait());
    }

    page_idx += kSupplyLengths[supply_idx];
  }
}

// Test that exercises supply logic by supplying data in chunks of unusual length.
VMO_VMAR_TEST(BulkOddLengthSupplyTest) { return BulkOddSupplyTestInner(check_vmar, false); }

// Test that exercises supply logic by supplying data in chunks of
// unusual lengths and offsets.
VMO_VMAR_TEST(BulkOddOffsetSupplyTest) { return BulkOddSupplyTestInner(check_vmar, true); }

// Tests that supply doesn't overwrite existing content.
VMO_VMAR_TEST(OverlapSupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));

  zx::vmo alt_data_vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &alt_data_vmo), ZX_OK);
  uint8_t alt_data[ZX_PAGE_SIZE];
  vmo->GenerateBufferContents(alt_data, 1, 2);
  ASSERT_EQ(alt_data_vmo.write(alt_data, 0, ZX_PAGE_SIZE), ZX_OK);

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1, std::move(alt_data_vmo)));
  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));

  TestThread t([vmo, alt_data, check_vmar]() -> bool {
    return check_buffer_data(vmo, 0, 1, alt_data, check_vmar) &&
           check_buffer(vmo, 1, 1, check_vmar);
  });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that a pager can handle lots of pending page requests.
VMO_VMAR_TEST(ManyRequestTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint32_t kNumPages = 257;  // Arbitrary large number
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  std::unique_ptr<TestThread> ts[kNumPages];
  for (unsigned i = 0; i < kNumPages; i++) {
    ts[i] = std::make_unique<TestThread>(
        [vmo, i, check_vmar]() -> bool { return check_buffer(vmo, i, 1, check_vmar); });
    ASSERT_TRUE(ts[i]->Start());
    ASSERT_TRUE(ts[i]->WaitForBlocked());
  }

  for (unsigned i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i, 1, ZX_TIME_INFINITE));
    ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
    ASSERT_TRUE(ts[i]->Wait());
  }
}

// Tests that a pager can support creating and destroying successive vmos.
TEST(Pager, SuccessiveVmoTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint32_t kNumVmos = 64;
  for (unsigned i = 0; i < kNumVmos; i++) {
    Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmo(1, &vmo));

    TestThread t([vmo]() -> bool { return check_buffer(vmo, 0, 1, true); });

    ASSERT_TRUE(t.Start());

    ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

    ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

    ASSERT_TRUE(t.Wait());

    pager.ReleaseVmo(vmo);
  }
}

// Tests that a pager can support multiple concurrent vmos.
TEST(Pager, MultipleConcurrentVmoTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint32_t kNumVmos = 8;
  Vmo* vmos[kNumVmos];
  std::unique_ptr<TestThread> ts[kNumVmos];
  for (unsigned i = 0; i < kNumVmos; i++) {
    ASSERT_TRUE(pager.CreateVmo(1, vmos + i));

    ts[i] = std::make_unique<TestThread>(
        [vmo = vmos[i]]() -> bool { return check_buffer(vmo, 0, 1, true); });

    ASSERT_TRUE(ts[i]->Start());

    ASSERT_TRUE(pager.WaitForPageRead(vmos[i], 0, 1, ZX_TIME_INFINITE));
  }

  for (unsigned i = 0; i < kNumVmos; i++) {
    ASSERT_TRUE(pager.SupplyPages(vmos[i], 0, 1));

    ASSERT_TRUE(ts[i]->Wait());
  }
}

// Tests that unmapping a vmo while threads are blocked on a pager read
// eventually results in pagefaults.
TEST(Pager, VmarUnmapTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo]() -> bool { return check_buffer(vmo, 0, 1, true); });
  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.WaitForBlocked());

  ASSERT_TRUE(pager.UnmapVmo(vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
}

// Tests that replacing a vmar mapping while threads are blocked on a
// pager read results in reads to the new mapping.
TEST(Pager, VmarRemapTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  constexpr uint32_t kNumPages = 8;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  std::unique_ptr<TestThread> ts[kNumPages];
  for (unsigned i = 0; i < kNumPages; i++) {
    ts[i] =
        std::make_unique<TestThread>([vmo, i]() -> bool { return check_buffer(vmo, i, 1, true); });
    ASSERT_TRUE(ts[i]->Start());
  }
  for (unsigned i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(ts[i]->WaitForBlocked());
  }

  zx::vmo old_vmo;
  ASSERT_TRUE(pager.ReplaceVmo(vmo, &old_vmo));

  zx::vmo tmp;
  ASSERT_EQ(zx::vmo::create(kNumPages * ZX_PAGE_SIZE, 0, &tmp), ZX_OK);
  ASSERT_EQ(tmp.op_range(ZX_VMO_OP_COMMIT, 0, kNumPages * ZX_PAGE_SIZE, nullptr, 0), ZX_OK);
  ASSERT_EQ(pager.pager().supply_pages(old_vmo, 0, kNumPages * ZX_PAGE_SIZE, tmp, 0), ZX_OK);

  for (unsigned i = 0; i < kNumPages; i++) {
    uint64_t offset, length;
    ASSERT_TRUE(pager.GetPageReadRequest(vmo, ZX_TIME_INFINITE, &offset, &length));
    ASSERT_EQ(length, 1);
    ASSERT_TRUE(pager.SupplyPages(vmo, offset, 1));
    ASSERT_TRUE(ts[offset]->Wait());
  }
}

// Tests that ZX_VM_MAP_RANGE works with pager vmos (i.e. maps in backed regions
// but doesn't try to pull in new regions).
TEST(Pager, VmarMapRangeTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  // Create a vmo with 2 pages. Supply the first page but not the second.
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  // Map the vmo. This shouldn't block or generate any new page requests.
  uint64_t ptr;
  TestThread t([vmo, &ptr]() -> bool {
    ZX_ASSERT(zx::vmar::root_self()->map(0, vmo->vmo(), 0, 2 * ZX_PAGE_SIZE,
                                         ZX_VM_PERM_READ | ZX_VM_MAP_RANGE, &ptr) == ZX_OK);
    return true;
  });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.Wait());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // Verify the buffer contents. This should generate a new request for
  // the second page, which we want to fulfill.
  TestThread t2([vmo, &ptr]() -> bool {
    uint8_t data[2 * ZX_PAGE_SIZE];
    vmo->GenerateBufferContents(data, 2, 0);

    return memcmp(data, reinterpret_cast<uint8_t*>(ptr), 2 * ZX_PAGE_SIZE) == 0;
  });

  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 1, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));

  ASSERT_TRUE(t2.Wait());

  // After the verification is done, make sure there are no unexpected
  // page requests.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // Cleanup the mapping we created.
  zx::vmar::root_self()->unmap(ptr, 2 * ZX_PAGE_SIZE);
}

// Tests that reads don't block forever if a vmo is resized out from under a read.
VMO_VMAR_TEST(ReadResizeTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(vmo->Resize(0));

  if (check_vmar) {
    ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
  } else {
    ASSERT_TRUE(t.WaitForFailure());
  }
}

// Test that suspending and resuming a thread in the middle of a read works.
VMO_VMAR_TEST(SuspendReadTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  t.SuspendSync();
  t.Resume();

  ASSERT_TRUE(t.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
}

// Tests the ZX_INFO_VMO_PAGER_BACKED flag
TEST(Pager, VmoInfoPagerTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(ZX_PAGE_SIZE, &vmo));

  // Check that the flag is set on a pager created vmo.
  zx_info_vmo_t info;
  ASSERT_EQ(ZX_OK, vmo->vmo().get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr), "");
  ASSERT_EQ(ZX_INFO_VMO_PAGER_BACKED, info.flags & ZX_INFO_VMO_PAGER_BACKED, "");

  // Check that the flag isn't set on a regular vmo.
  zx::vmo plain_vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(ZX_PAGE_SIZE, 0, &plain_vmo), "");
  ASSERT_EQ(ZX_OK, plain_vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr), "");
  ASSERT_EQ(0, info.flags & ZX_INFO_VMO_PAGER_BACKED, "");
}

// Tests that detaching results in a complete request.
TEST(Pager, DetachPageCompleteTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  ASSERT_TRUE(pager.DetachVmo(vmo));

  ASSERT_TRUE(pager.WaitForPageComplete(vmo->GetKey(), ZX_TIME_INFINITE));
}

// Tests that closing results in a complete request.
TEST(Pager, ClosePageCompleteTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  uint64_t key = vmo->GetKey();
  pager.ReleaseVmo(vmo);

  ASSERT_TRUE(pager.WaitForPageComplete(key, ZX_TIME_INFINITE));
}

// Tests that interrupting a read after receiving the request doesn't result in hanging threads.
void ReadInterruptLateTest(bool check_vmar, bool detach) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  if (detach) {
    ASSERT_TRUE(pager.DetachVmo(vmo));
  } else {
    pager.ClosePagerHandle();
  }

  if (check_vmar) {
    ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
  } else {
    ASSERT_TRUE(t.WaitForFailure());
  }

  if (detach) {
    ASSERT_TRUE(pager.WaitForPageComplete(vmo->GetKey(), ZX_TIME_INFINITE));
  }
}

VMO_VMAR_TEST(ReadCloseInterruptLateTest) { ReadInterruptLateTest(check_vmar, false); }

VMO_VMAR_TEST(ReadDetachInterruptLateTest) { ReadInterruptLateTest(check_vmar, true); }

// Tests that interrupt a read before receiving requests doesn't result in hanging threads.
void ReadInterruptEarlyTest(bool check_vmar, bool detach) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.WaitForBlocked());

  if (detach) {
    ASSERT_TRUE(pager.DetachVmo(vmo));
  } else {
    pager.ClosePagerHandle();
  }

  if (check_vmar) {
    ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
  } else {
    ASSERT_TRUE(t.WaitForFailure());
  }

  if (detach) {
    ASSERT_TRUE(pager.WaitForPageComplete(vmo->GetKey(), ZX_TIME_INFINITE));
  }
}

VMO_VMAR_TEST(ReadCloseInterruptEarlyTest) { ReadInterruptEarlyTest(check_vmar, false); }

VMO_VMAR_TEST(ReadDetachInterruptEarlyTest) { ReadInterruptEarlyTest(check_vmar, true); }

// Tests that closing a pager while a thread is accessing it doesn't cause
// problems (other than a page fault in the accessing thread).
TEST(Pager, ClosePagerTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));

  TestThread t([vmo]() -> bool { return check_buffer(vmo, 0, 1, true); });
  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.WaitForBlocked());

  pager.ClosePagerHandle();

  ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
  ASSERT_TRUE(check_buffer(vmo, 1, 1, true));
}

// Tests that closing a pager while a vmo is being detached doesn't cause problems.
TEST(Pager, DetachClosePagerTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  ASSERT_TRUE(pager.DetachVmo(vmo));

  pager.ClosePagerHandle();
}

// Tests that closing an in use port doesn't cause issues (beyond no
// longer being able to receive requests).
TEST(Pager, ClosePortTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));

  TestThread t([vmo]() -> bool { return check_buffer(vmo, 0, 1, true); });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t.WaitForBlocked());

  pager.ClosePortHandle();

  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));
  ASSERT_TRUE(check_buffer(vmo, 1, 1, true));

  ASSERT_TRUE(pager.DetachVmo(vmo));
  ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
}

// Tests that reading from a clone populates the vmo.
VMO_VMAR_TEST(CloneReadFromCloneTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([clone = clone.get(), check_vmar]() -> bool {
    return check_buffer(clone, 0, 1, check_vmar);
  });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
}

// Tests that reading from the parent populates the clone.
VMO_VMAR_TEST(CloneReadFromParentTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());

  TestThread t2([clone = clone.get(), check_vmar]() -> bool {
    return check_buffer(clone, 0, 1, check_vmar);
  });

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that overlapping reads on clone and parent work.
VMO_VMAR_TEST(CloneSimultaneousReadTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  TestThread t2([clone = clone.get(), check_vmar]() -> bool {
    return check_buffer(clone, 0, 1, check_vmar);
  });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(t2.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
  ASSERT_TRUE(t2.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that overlapping reads from two clones work.
VMO_VMAR_TEST(CloneSimultaneousChildReadTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);
  auto clone2 = vmo->Clone();
  ASSERT_NOT_NULL(clone2);

  TestThread t([clone = clone.get(), check_vmar]() -> bool {
    return check_buffer(clone, 0, 1, check_vmar);
  });
  TestThread t2([clone = clone2.get(), check_vmar]() -> bool {
    return check_buffer(clone, 0, 1, check_vmar);
  });

  ASSERT_TRUE(t.Start());
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(t2.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
  ASSERT_TRUE(t2.Wait());

  ASSERT_FALSE(pager.WaitForPageRead(vmo, 0, 1, 0));
}

// Tests that writes don't propagate to the parent.
VMO_VMAR_TEST(CloneWriteToCloneTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([clone = clone.get()]() -> bool {
    *reinterpret_cast<uint64_t*>(clone->GetBaseAddr()) = 0xdeadbeef;
    return true;
  });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());

  ASSERT_TRUE(vmo->CheckVmar(0, 1));
  ASSERT_EQ(*reinterpret_cast<uint64_t*>(clone->GetBaseAddr()), 0xdeadbeef);
  *reinterpret_cast<uint64_t*>(clone->GetBaseAddr()) = clone->GetKey();
  ASSERT_TRUE(clone->CheckVmar(0, 1));
}

// Tests that detaching the parent doesn't crash the clone.
TEST(Pager, CloneDetachTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));
  auto clone = vmo->Clone();

  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 1));

  TestThread t([clone = clone.get()]() -> bool {
    uint8_t data[ZX_PAGE_SIZE] = {};
    return check_buffer_data(clone, 0, 1, data, true) && check_buffer(clone, 1, 1, true);
  });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.DetachVmo(vmo));

  ASSERT_TRUE(pager.WaitForPageComplete(vmo->GetKey(), ZX_TIME_INFINITE));

  ASSERT_TRUE(t.Wait());
}

// Tests that commit on the clone populates things properly.
TEST(Pager, CloneCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 32;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([clone = clone.get()]() -> bool { return clone->Commit(0, kNumPages); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  ASSERT_TRUE(t.Wait());
}

// Tests that commit on the clone populates things properly if things have already been touched.
TEST(Pager, CloneSplitCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 4;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  auto clone = vmo->Clone();
  ASSERT_NOT_NULL(clone);

  TestThread t([clone = clone.get()]() -> bool { return clone->Commit(0, kNumPages); });

  // Populate pages 1 and 2 of the parent vmo, and page 1 of the clone.
  ASSERT_TRUE(pager.SupplyPages(vmo, 1, 2));
  ASSERT_TRUE(clone->CheckVmar(1, 1));

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(pager.WaitForPageRead(vmo, kNumPages - 1, 1, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, kNumPages - 1, 1));

  ASSERT_TRUE(t.Wait());
}

// Resizing a cloned VMO causes a fault.
TEST(Pager, CloneResizeCloneHazard) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  static constexpr uint64_t kSize = 2 * ZX_PAGE_SIZE;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 2));

  zx::vmo clone_vmo;
  EXPECT_EQ(ZX_OK, vmo->vmo().create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY | ZX_VMO_CHILD_RESIZABLE,
                                           0, kSize, &clone_vmo));

  uintptr_t ptr_rw;
  EXPECT_EQ(ZX_OK, zx::vmar::root_self()->map(0, clone_vmo, 0, kSize,
                                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &ptr_rw));

  auto int_arr = reinterpret_cast<int*>(ptr_rw);
  EXPECT_EQ(int_arr[1], 0);

  EXPECT_EQ(ZX_OK, clone_vmo.set_size(0u));

  EXPECT_EQ(false, probe_for_read(&int_arr[1]), "read probe");
  EXPECT_EQ(false, probe_for_write(&int_arr[1]), "write probe");

  EXPECT_EQ(ZX_OK, zx::vmar::root_self()->unmap(ptr_rw, kSize), "unmap");
}

// Resizing the parent VMO and accessing via a mapped VMO is ok.
TEST(Pager, CloneResizeParentOK) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  static constexpr uint64_t kSize = 2 * ZX_PAGE_SIZE;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(2, &vmo));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 2));

  zx::vmo clone_vmo;
  ASSERT_EQ(ZX_OK, vmo->vmo().create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY, 0, kSize, &clone_vmo));

  uintptr_t ptr_rw;
  EXPECT_EQ(ZX_OK, zx::vmar::root_self()->map(0, clone_vmo, 0, kSize,
                                              ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &ptr_rw));

  auto int_arr = reinterpret_cast<int*>(ptr_rw);
  EXPECT_EQ(int_arr[1], 0);

  EXPECT_TRUE(vmo->Resize(0u));

  EXPECT_EQ(true, probe_for_read(&int_arr[1]), "read probe");
  EXPECT_EQ(true, probe_for_write(&int_arr[1]), "write probe");

  EXPECT_EQ(ZX_OK, zx::vmar::root_self()->unmap(ptr_rw, kSize), "unmap");
}

// Pages exposed by growing the parent after shrinking it aren't visible to the child.
TEST(Pager, CloneShrinkGrowParent) {
  struct {
    uint64_t vmo_size;
    uint64_t clone_offset;
    uint64_t clone_size;
    uint64_t clone_test_offset;
    uint64_t resize_size;
  } configs[3] = {
      // Aligned, truncate to parent offset.
      {ZX_PAGE_SIZE, 0, ZX_PAGE_SIZE, 0, 0},
      // Offset, truncate to before parent offset.
      {2 * ZX_PAGE_SIZE, ZX_PAGE_SIZE, ZX_PAGE_SIZE, 0, 0},
      // Offset, truncate to partway through clone.
      {3 * ZX_PAGE_SIZE, ZX_PAGE_SIZE, 2 * ZX_PAGE_SIZE, ZX_PAGE_SIZE, 2 * ZX_PAGE_SIZE},
  };

  for (auto& config : configs) {
    UserPager pager;

    ASSERT_TRUE(pager.Init());

    Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmo(config.vmo_size / ZX_PAGE_SIZE, &vmo));

    zx::vmo aux;
    ASSERT_EQ(ZX_OK, zx::vmo::create(config.vmo_size, 0, &aux));
    ASSERT_EQ(ZX_OK, aux.op_range(ZX_VMO_OP_COMMIT, 0, config.vmo_size, nullptr, 0));
    ASSERT_TRUE(pager.SupplyPages(vmo, 0, config.vmo_size / ZX_PAGE_SIZE, std::move(aux)));

    zx::vmo clone_vmo;
    ASSERT_EQ(ZX_OK, vmo->vmo().create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY, config.clone_offset,
                                             config.vmo_size, &clone_vmo));

    uintptr_t ptr_ro;
    EXPECT_EQ(ZX_OK, zx::vmar::root_self()->map(0, clone_vmo, 0, config.clone_size, ZX_VM_PERM_READ,
                                                &ptr_ro));

    auto ptr = reinterpret_cast<int*>(ptr_ro + config.clone_test_offset);
    EXPECT_EQ(0, *ptr);

    uint32_t data = 1;
    const uint64_t vmo_offset = config.clone_offset + config.clone_test_offset;
    EXPECT_EQ(ZX_OK, vmo->vmo().write(&data, vmo_offset, sizeof(data)));

    EXPECT_EQ(1, *ptr);

    EXPECT_TRUE(vmo->Resize(0u));

    EXPECT_EQ(0, *ptr);

    EXPECT_TRUE(vmo->Resize(config.vmo_size / ZX_PAGE_SIZE));

    ASSERT_EQ(ZX_OK, zx::vmo::create(config.vmo_size, 0, &aux));
    ASSERT_EQ(ZX_OK, aux.op_range(ZX_VMO_OP_COMMIT, 0, config.vmo_size, nullptr, 0));
    ASSERT_TRUE(pager.SupplyPages(vmo, 0, config.vmo_size / ZX_PAGE_SIZE, std::move(aux)));

    data = 2;
    EXPECT_EQ(ZX_OK, vmo->vmo().write(&data, vmo_offset, sizeof(data)));

    EXPECT_EQ(0, *ptr);

    EXPECT_EQ(ZX_OK, zx::vmar::root_self()->unmap(ptr_ro, config.clone_size));
  }
}

// Tests that a commit properly populates the whole range.
TEST(Pager, SimpleCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 555;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));

  ASSERT_TRUE(t.Wait());
}

// Tests that a commit over a partially populated range is properly split.
TEST(Pager, SplitCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 33;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  ASSERT_TRUE(pager.SupplyPages(vmo, (kNumPages / 2), 1));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, (kNumPages / 2), ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, (kNumPages / 2)));

  ASSERT_TRUE(pager.WaitForPageRead(vmo, (kNumPages / 2) + 1, kNumPages / 2, ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, ((kNumPages / 2) + 1), (kNumPages / 2)));

  ASSERT_TRUE(t.Wait());
}

// Tests that overlapping commits don't result in redundant requests.
TEST(Pager, OverlapCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 32;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t1([vmo]() -> bool { return vmo->Commit((kNumPages / 4), (kNumPages / 2)); });
  TestThread t2([vmo]() -> bool { return vmo->Commit(0, kNumPages); });

  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, (kNumPages / 4), (kNumPages / 2), ZX_TIME_INFINITE));

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, (kNumPages / 4), ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, (3 * kNumPages / 4)));

  ASSERT_TRUE(pager.WaitForPageRead(vmo, (3 * kNumPages / 4), (kNumPages / 4), ZX_TIME_INFINITE));
  ASSERT_TRUE(pager.SupplyPages(vmo, (3 * kNumPages / 4), (kNumPages / 4)));

  ASSERT_TRUE(t1.Wait());
  ASSERT_TRUE(t2.Wait());
}

// Tests that overlapping commits are properly supplied.
TEST(Pager, OverlapCommitSupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kSupplyLen = 3;
  constexpr uint64_t kCommitLenA = 7;
  constexpr uint64_t kCommitLenB = 5;
  constexpr uint64_t kNumPages = kCommitLenA * kCommitLenB * kSupplyLen;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  std::unique_ptr<TestThread> tsA[kNumPages / kCommitLenA];
  for (unsigned i = 0; i < std::size(tsA); i++) {
    tsA[i] = std::make_unique<TestThread>(
        [vmo, i]() -> bool { return vmo->Commit(i * kCommitLenA, kCommitLenA); });

    ASSERT_TRUE(tsA[i]->Start());
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i * kCommitLenA, kCommitLenA, ZX_TIME_INFINITE));
  }

  std::unique_ptr<TestThread> tsB[kNumPages / kCommitLenB];
  for (unsigned i = 0; i < std::size(tsB); i++) {
    tsB[i] = std::make_unique<TestThread>(
        [vmo, i]() -> bool { return vmo->Commit(i * kCommitLenB, kCommitLenB); });

    ASSERT_TRUE(tsB[i]->Start());
    ASSERT_TRUE(tsB[i]->WaitForBlocked());
  }

  for (unsigned i = 0; i < kNumPages / kSupplyLen; i++) {
    ASSERT_TRUE(pager.SupplyPages(vmo, i * kSupplyLen, kSupplyLen));
  }

  for (unsigned i = 0; i < std::size(tsA); i++) {
    ASSERT_TRUE(tsA[i]->Wait());
  }
  for (unsigned i = 0; i < std::size(tsB); i++) {
    ASSERT_TRUE(tsB[i]->Wait());
  }
}

// Tests that a single commit can be fulfilled by multiple supplies.
TEST(Pager, MultisupplyCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 32;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  for (unsigned i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(pager.SupplyPages(vmo, i, 1));
  }

  ASSERT_TRUE(t.Wait());
}

// Tests that a single supply can fulfil multiple commits.
TEST(Pager, MulticommitSupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumCommits = 5;
  constexpr uint64_t kNumSupplies = 7;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumCommits * kNumSupplies, &vmo));

  std::unique_ptr<TestThread> ts[kNumCommits];
  for (unsigned i = 0; i < kNumCommits; i++) {
    ts[i] = std::make_unique<TestThread>(
        [vmo, i]() -> bool { return vmo->Commit(i * kNumSupplies, kNumSupplies); });
    ASSERT_TRUE(ts[i]->Start());
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i * kNumSupplies, kNumSupplies, ZX_TIME_INFINITE));
  }

  for (unsigned i = 0; i < kNumSupplies; i++) {
    ASSERT_TRUE(pager.SupplyPages(vmo, kNumCommits * i, kNumCommits));
  }

  for (unsigned i = 0; i < kNumCommits; i++) {
    ASSERT_TRUE(ts[i]->Wait());
  }
}

// Tests that redundant supplies for a single commit don't cause errors.
TEST(Pager, CommitRedundantSupplyTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 8;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  for (unsigned i = 1; i <= kNumPages; i++) {
    ASSERT_TRUE(pager.SupplyPages(vmo, 0, i));
  }

  ASSERT_TRUE(t.Wait());
}

// Test that resizing out from under a commit is handled.
TEST(Pager, ResizeCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(3, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, 3); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 3, ZX_TIME_INFINITE));

  // Supply one of the pages that will be removed.
  ASSERT_TRUE(pager.SupplyPages(vmo, 2, 1));

  // Truncate the VMO.
  ASSERT_TRUE(vmo->Resize(1));

  // Make sure the thread is still blocked (i.e. check the accounting
  // w.r.t. the page that was removed).
  ASSERT_TRUE(t.WaitForBlocked());

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());

  // Make sure there are no extra requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Test that suspending and resuming a thread in the middle of commit works.
TEST(Pager, SuspendCommitTest) {
  UserPager pager;

  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, 1); });

  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  t.SuspendSync();
  t.Resume();

  ASSERT_TRUE(t.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.SupplyPages(vmo, 0, 1));

  ASSERT_TRUE(t.Wait());
}

// Tests API violations for pager_create.
TEST(Pager, InvalidPagerCreate) {
  zx_handle_t handle;

  // bad options
  ASSERT_EQ(zx_pager_create(1, &handle), ZX_ERR_INVALID_ARGS);
}

// Tests API violations for pager_create_vmo.
TEST(Pager, InvalidPagerCreateVmo) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx_handle_t vmo;

  // bad options
  ASSERT_EQ(zx_pager_create_vmo(pager.get(), ~0u, port.get(), 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_INVALID_ARGS);

  // bad handles for pager and port
  ASSERT_EQ(zx_pager_create_vmo(ZX_HANDLE_INVALID, 0, port.get(), 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_BAD_HANDLE);
  ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, ZX_HANDLE_INVALID, 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_BAD_HANDLE);

  // missing write right on port
  zx::port ro_port;
  ASSERT_EQ(port.duplicate(ZX_DEFAULT_PORT_RIGHTS & ~ZX_RIGHT_WRITE, &ro_port), ZX_OK);
  ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, ro_port.get(), 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_ACCESS_DENIED);

  // bad handle types for pager and port
  ASSERT_EQ(zx_pager_create_vmo(port.get(), 0, port.get(), 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_WRONG_TYPE);
  zx::vmo tmp_vmo;  // writability handle 2 is checked before the type, so use a new vmo
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &tmp_vmo), ZX_OK);
  ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, tmp_vmo.get(), 0, ZX_PAGE_SIZE, &vmo),
            ZX_ERR_WRONG_TYPE);

  // invalid size
  static constexpr uint64_t kBadSize = fbl::round_down(UINT64_MAX, ZX_PAGE_SIZE) + 1;
  ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, port.get(), 0, kBadSize, &vmo),
            ZX_ERR_OUT_OF_RANGE);
}

// Tests API violations for pager_detach_vmo.
TEST(Pager, InvalidPagerDetachVmo) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(
      zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE, vmo.reset_and_get_address()),
      ZX_OK);

  // bad handles
  ASSERT_EQ(zx_pager_detach_vmo(ZX_HANDLE_INVALID, vmo.get()), ZX_ERR_BAD_HANDLE);
  ASSERT_EQ(zx_pager_detach_vmo(pager.get(), ZX_HANDLE_INVALID), ZX_ERR_BAD_HANDLE);

  // wrong handle types
  ASSERT_EQ(zx_pager_detach_vmo(vmo.get(), vmo.get()), ZX_ERR_WRONG_TYPE);
  ASSERT_EQ(zx_pager_detach_vmo(pager.get(), pager.get()), ZX_ERR_WRONG_TYPE);

  // detaching a non-paged vmo
  zx::vmo tmp_vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &tmp_vmo), ZX_OK);
  ASSERT_EQ(zx_pager_detach_vmo(pager.get(), tmp_vmo.get()), ZX_ERR_INVALID_ARGS);

  // detaching with the wrong pager
  zx::pager pager2;
  ASSERT_EQ(zx::pager::create(0, &pager2), ZX_OK);
  ASSERT_EQ(zx_pager_detach_vmo(pager2.get(), vmo.get()), ZX_ERR_INVALID_ARGS);
}

// Tests API violations for supply_pages.
TEST(Pager, InvalidPagerSupplyPages) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(
      zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE, vmo.reset_and_get_address()),
      ZX_OK);

  zx::vmo aux_vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &aux_vmo), ZX_OK);

  // bad handles
  ASSERT_EQ(zx_pager_supply_pages(ZX_HANDLE_INVALID, vmo.get(), 0, 0, aux_vmo.get(), 0),
            ZX_ERR_BAD_HANDLE);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), ZX_HANDLE_INVALID, 0, 0, aux_vmo.get(), 0),
            ZX_ERR_BAD_HANDLE);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 0, ZX_HANDLE_INVALID, 0),
            ZX_ERR_BAD_HANDLE);

  // wrong handle types
  ASSERT_EQ(zx_pager_supply_pages(vmo.get(), vmo.get(), 0, 0, aux_vmo.get(), 0), ZX_ERR_WRONG_TYPE);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), pager.get(), 0, 0, aux_vmo.get(), 0),
            ZX_ERR_WRONG_TYPE);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 0, port.get(), 0), ZX_ERR_WRONG_TYPE);

  // using a non-paged vmo
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), aux_vmo.get(), 0, 0, aux_vmo.get(), 0),
            ZX_ERR_INVALID_ARGS);

  // using a pager vmo from another pager
  zx::pager pager2;
  ASSERT_EQ(zx::pager::create(0, &pager2), ZX_OK);
  ASSERT_EQ(zx_pager_supply_pages(pager2.get(), vmo.get(), 0, 0, ZX_HANDLE_INVALID, 0),
            ZX_ERR_INVALID_ARGS);

  // missing permissions on the aux vmo
  zx::vmo ro_vmo;
  ASSERT_EQ(vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_WRITE, &ro_vmo), ZX_OK);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 0, ro_vmo.get(), 0),
            ZX_ERR_ACCESS_DENIED);
  zx::vmo wo_vmo;
  ASSERT_EQ(vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & ~ZX_RIGHT_READ, &wo_vmo), ZX_OK);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 0, wo_vmo.get(), 0),
            ZX_ERR_ACCESS_DENIED);

  // misaligned offset, size, or aux alignment
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 1, 0, aux_vmo.get(), 0),
            ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 1, aux_vmo.get(), 0),
            ZX_ERR_INVALID_ARGS);
  ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, 0, aux_vmo.get(), 1),
            ZX_ERR_INVALID_ARGS);

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  if (&get_root_resource) {
    // unsupported aux vmo type
    zx::vmo physical_vmo;
    // We're not actually going to do anything with this vmo, and since the
    // kernel doesn't do any checks with the address if you're using the
    // root resource, just use addr 0.
    // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
    ASSERT_EQ(zx_vmo_create_physical(get_root_resource(), 0, ZX_PAGE_SIZE,
                                     physical_vmo.reset_and_get_address()),
              ZX_OK);
    ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, ZX_PAGE_SIZE, physical_vmo.get(), 0),
              ZX_ERR_NOT_SUPPORTED);
  }

  // violations of conditions for taking pages from a vmo
  enum PagerViolation {
    kIsClone = 0,
    kFromPager,
    kHasMapping,
    kHasClone,
    kHasPinned,
    kViolationCount,
  };
  for (uint32_t i = 0; i < kViolationCount; i++) {
    if (i == kHasPinned && !&get_root_resource) {
      continue;
    }

    zx::vmo aux_vmo;  // aux vmo given to supply pages
    zx::vmo alt_vmo;  // alt vmo if clones are involved

    if (i == kIsClone) {
      ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &alt_vmo), ZX_OK);
      ASSERT_EQ(alt_vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, ZX_PAGE_SIZE, &aux_vmo), ZX_OK);
    } else if (i == kFromPager) {
      ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE,
                                    aux_vmo.reset_and_get_address()),
                ZX_OK);
    } else {
      ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &aux_vmo), ZX_OK);
    }

    fzl::VmoMapper mapper;
    if (i == kHasMapping) {
      ASSERT_EQ(mapper.Map(aux_vmo, 0, ZX_PAGE_SIZE, ZX_VM_PERM_READ), ZX_OK);
    }

    if (i == kHasClone) {
      ASSERT_EQ(aux_vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, ZX_PAGE_SIZE, &alt_vmo), ZX_OK);
    }

    if (i == kFromPager) {
      ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &alt_vmo), ZX_OK);
      ASSERT_EQ(alt_vmo.op_range(ZX_VMO_OP_COMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_OK);
      ASSERT_EQ(
          zx_pager_supply_pages(pager.get(), aux_vmo.get(), 0, ZX_PAGE_SIZE, alt_vmo.get(), 0),
          ZX_OK);
    } else {
      ASSERT_EQ(aux_vmo.op_range(ZX_VMO_OP_COMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_OK);
    }

    zx::iommu iommu;
    zx::bti bti;
    zx::pmt pmt;
    if (i == kHasPinned) {
      // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
      zx::unowned_resource root_res(get_root_resource());
      zx_iommu_desc_dummy_t desc;
      // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
      ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                                iommu.reset_and_get_address()),
                ZX_OK);
      ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);
      zx_paddr_t addr;
      ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, aux_vmo, 0, ZX_PAGE_SIZE, &addr, 1, &pmt), ZX_OK);
    }

    ASSERT_EQ(zx_pager_supply_pages(pager.get(), vmo.get(), 0, ZX_PAGE_SIZE, aux_vmo.get(), 0),
              ZX_ERR_BAD_STATE);

    if (pmt) {
      pmt.unpin();
    }
  }

  // out of range pager_vmo region
  ASSERT_EQ(aux_vmo.op_range(ZX_VMO_OP_COMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_OK);
  ASSERT_EQ(
      zx_pager_supply_pages(pager.get(), vmo.get(), ZX_PAGE_SIZE, ZX_PAGE_SIZE, aux_vmo.get(), 0),
      ZX_ERR_OUT_OF_RANGE);

  // out of range aux_vmo region
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &aux_vmo), ZX_OK);
  ASSERT_EQ(aux_vmo.op_range(ZX_VMO_OP_COMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_OK);
  ASSERT_EQ(
      zx_pager_supply_pages(pager.get(), vmo.get(), 0, ZX_PAGE_SIZE, aux_vmo.get(), ZX_PAGE_SIZE),
      ZX_ERR_OUT_OF_RANGE);
}

// Tests that resizing a non-resizable pager vmo fails.
TEST(Pager, ResizeNonresizableVmo) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;

  ASSERT_EQ(pager.create_vmo(0, port, 0, ZX_PAGE_SIZE, &vmo), ZX_OK);

  ASSERT_EQ(vmo.set_size(2 * ZX_PAGE_SIZE), ZX_ERR_UNAVAILABLE);
}

// Tests that decommiting a clone fails
TEST(Pager, DecommitTest) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;

  ASSERT_EQ(pager.create_vmo(0, port, 0, ZX_PAGE_SIZE, &vmo), ZX_OK);

  ASSERT_EQ(vmo.op_range(ZX_VMO_OP_DECOMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_ERR_NOT_SUPPORTED);

  zx::vmo child;
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_PRIVATE_PAGER_COPY, 0, ZX_PAGE_SIZE, &child), ZX_OK);

  ASSERT_EQ(child.op_range(ZX_VMO_OP_DECOMMIT, 0, ZX_PAGE_SIZE, nullptr, 0), ZX_ERR_NOT_SUPPORTED);
}

// Test that supplying uncommitted pages prevents faults.
TEST(Pager, UncommittedSupply) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;

  ASSERT_EQ(pager.create_vmo(0, port, 0, ZX_PAGE_SIZE, &vmo), ZX_OK);

  zx::vmo empty;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &empty));

  ASSERT_OK(pager.supply_pages(vmo, 0, ZX_PAGE_SIZE, empty, 0));

  // A read should not fault and give zeros.
  uint32_t val = 42;
  ASSERT_OK(vmo.read(&val, 0, sizeof(val)));
  ASSERT_EQ(val, 0);
}

// Tests API violations for zx_pager_op_range.
TEST(Pager, InvalidPagerOpRange) {
  constexpr uint32_t kNumValidOpCodes = 1;
  const uint32_t opcodes[kNumValidOpCodes] = {ZX_PAGER_OP_FAIL};

  for (uint32_t i = 0; i < kNumValidOpCodes; i++) {
    zx::pager pager;
    ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

    zx::port port;
    ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

    zx::vmo vmo;
    ASSERT_EQ(zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE,
                                  vmo.reset_and_get_address()),
              ZX_OK);

    // bad handles
    ASSERT_EQ(zx_pager_op_range(ZX_HANDLE_INVALID, opcodes[i], vmo.get(), 0, 0, 0),
              ZX_ERR_BAD_HANDLE);
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], ZX_HANDLE_INVALID, 0, 0, 0),
              ZX_ERR_BAD_HANDLE);

    // wrong handle types
    ASSERT_EQ(zx_pager_op_range(vmo.get(), opcodes[i], vmo.get(), 0, 0, 0), ZX_ERR_WRONG_TYPE);
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], pager.get(), 0, 0, 0), ZX_ERR_WRONG_TYPE);

    // using a non-pager-backed vmo
    zx::vmo vmo2;
    ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo2), ZX_OK);
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo2.get(), 0, 0, 0), ZX_ERR_INVALID_ARGS);

    // using a pager vmo from another pager
    zx::pager pager2;
    ASSERT_EQ(zx::pager::create(0, &pager2), ZX_OK);
    ASSERT_EQ(zx_pager_op_range(pager2.get(), opcodes[i], vmo.get(), 0, 0, 0), ZX_ERR_INVALID_ARGS);

    // misaligned offset or length
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), 1, 0, 0), ZX_ERR_INVALID_ARGS);
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), 0, 1, 0), ZX_ERR_INVALID_ARGS);

    // out of range
    ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), ZX_PAGE_SIZE, ZX_PAGE_SIZE,
                                ZX_ERR_BAD_STATE),
              ZX_ERR_OUT_OF_RANGE);

    // invalid error code
    if (opcodes[i] == ZX_PAGER_OP_FAIL) {
      ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), 0, 0, 0x11ffffffff),
                ZX_ERR_INVALID_ARGS);

      ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), 0, 0, ZX_ERR_INTERNAL),
                ZX_ERR_INVALID_ARGS);

      ASSERT_EQ(zx_pager_op_range(pager.get(), opcodes[i], vmo.get(), 0, 0, 10),
                ZX_ERR_INVALID_ARGS);
    }
  }
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(
      zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE, vmo.reset_and_get_address()),
      ZX_OK);

  // invalid opcode
  ASSERT_EQ(zx_pager_op_range(pager.get(), 0, vmo.get(), 0, 0, 0), ZX_ERR_NOT_SUPPORTED);
}

// Simple test for a ZX_PAGER_OP_FAIL on a single page, accessed from a single thread.
// Tests both cases, where the client accesses the vmo directly, and where the client has the vmo
// mapped in a vmar.
VMO_VMAR_TEST(FailSinglePage) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo));

  TestThread t([vmo, check_vmar]() -> bool { return check_buffer(vmo, 0, 1, check_vmar); });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 1, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.FailPages(vmo, 0, 1));

  if (check_vmar) {
    // Verify that the thread crashes if the page was accessed via a vmar.
    ASSERT_TRUE(t.WaitForCrash(vmo->GetBaseAddr()));
  } else {
    // Verify that the vmo read fails if the thread directly accessed the vmo.
    ASSERT_TRUE(t.WaitForFailure());
  }
  // Make sure there are no extra requests.
  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests failing the exact range requested.
TEST(Pager, FailExactRange) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages));

  // Failing the pages will cause the COMMIT to fail.
  ASSERT_TRUE(t.WaitForFailure());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that multiple page requests can be failed at once.
TEST(Pager, FailMultipleCommits) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  // Multiple threads requesting disjoint ranges.
  std::unique_ptr<TestThread> threads[kNumPages];
  for (uint64_t i = 0; i < kNumPages; i++) {
    threads[i] = std::make_unique<TestThread>([vmo, i]() -> bool { return vmo->Commit(i, 1); });
    ASSERT_TRUE(threads[i]->Start());
  }

  for (uint64_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(threads[i]->WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageRead(vmo, i, 1, ZX_TIME_INFINITE));
  }

  // Fail the entire range.
  ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages));

  for (uint64_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(threads[i]->WaitForFailure());
  }

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // Multiple threads requesting the same range.
  for (uint64_t i = 0; i < kNumPages; i++) {
    threads[i] =
        std::make_unique<TestThread>([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
    ASSERT_TRUE(threads[i]->Start());
  }

  for (uint64_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(threads[i]->WaitForBlocked());
  }

  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  // Fail the entire range.
  ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages));

  for (uint64_t i = 0; i < kNumPages; i++) {
    ASSERT_TRUE(threads[i]->WaitForFailure());
  }

  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests failing multiple vmos.
TEST(Pager, FailMultipleVmos) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  Vmo* vmo1;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo1));
  TestThread t1([vmo1]() -> bool { return vmo1->Commit(0, 1); });

  Vmo* vmo2;
  ASSERT_TRUE(pager.CreateVmo(1, &vmo2));
  TestThread t2([vmo2]() -> bool { return vmo2->Commit(0, 1); });

  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo1, 0, 1, ZX_TIME_INFINITE));

  uint64_t offset, length;
  // No page requests for vmo2 yet.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo2, 0, &offset, &length));

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());

  ASSERT_TRUE(pager.WaitForPageRead(vmo2, 0, 1, ZX_TIME_INFINITE));

  // Fail vmo1.
  ASSERT_TRUE(pager.FailPages(vmo1, 0, 1));
  ASSERT_TRUE(t1.WaitForFailure());

  // No more requests for vmo1.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo1, 0, &offset, &length));

  // Fail vmo2.
  ASSERT_TRUE(pager.FailPages(vmo2, 0, 1));
  ASSERT_TRUE(t2.WaitForFailure());

  // No more requests for either vmo1 or vmo2.
  ASSERT_FALSE(pager.GetPageReadRequest(vmo1, 0, &offset, &length));
  ASSERT_FALSE(pager.GetPageReadRequest(vmo2, 0, &offset, &length));
}

// Tests failing a range overlapping with a page request.
TEST(Pager, FailOverlappingRange) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  // End of the request range overlaps with the failed range.
  TestThread t1([vmo]() -> bool { return vmo->Commit(0, 2); });
  // The entire request range overlaps with the failed range.
  TestThread t2([vmo]() -> bool { return vmo->Commit(9, 2); });
  // The start of the request range overlaps with the failed range.
  TestThread t3([vmo]() -> bool { return vmo->Commit(5, 2); });

  ASSERT_TRUE(t1.Start());
  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, 2, ZX_TIME_INFINITE));

  ASSERT_TRUE(t2.Start());
  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 9, 2, ZX_TIME_INFINITE));

  ASSERT_TRUE(t3.Start());
  ASSERT_TRUE(t3.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 5, 2, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.FailPages(vmo, 1, 9));

  ASSERT_TRUE(t1.WaitForFailure());
  ASSERT_TRUE(t2.WaitForFailure());
  ASSERT_TRUE(t3.WaitForFailure());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests failing the requested range via multiple pager_op_range calls - after the first one, the
// rest are redundant.
TEST(Pager, FailRedundant) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  for (uint64_t i = 0; i < kNumPages; i++) {
    // The first call with i = 0 should cause the thread to cause.
    // The following calls are no-ops.
    ASSERT_TRUE(pager.FailPages(vmo, i, 1));
  }

  ASSERT_TRUE(t.WaitForFailure());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that failing a range after the vmo is detached is a no-op.
TEST(Pager, FailAfterDetach) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
  ASSERT_TRUE(t.Start());

  ASSERT_TRUE(t.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.DetachVmo(vmo));
  // Detaching the vmo should cause the COMMIT to fail.
  ASSERT_TRUE(t.WaitForFailure());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // This is a no-op.
  ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages));

  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that a supply_pages succeeds after failing i.e. a fail is not fatal.
TEST(Pager, SupplyAfterFail) {
  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 11;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  TestThread t1([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
  ASSERT_TRUE(t1.Start());

  ASSERT_TRUE(t1.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages));
  ASSERT_TRUE(t1.WaitForFailure());

  uint64_t offset, length;
  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));

  // Try to COMMIT the failed range again.
  TestThread t2([vmo]() -> bool { return vmo->Commit(0, kNumPages); });
  ASSERT_TRUE(t2.Start());

  ASSERT_TRUE(t2.WaitForBlocked());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

  // This should supply the pages as expected.
  ASSERT_TRUE(pager.SupplyPages(vmo, 0, kNumPages));
  ASSERT_TRUE(t2.Wait());

  ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
}

// Tests that the error code passed in when failing is correctly propagated.
TEST(Pager, FailErrorCode) {
  constexpr uint32_t kNumValidErrors = 3;
  zx_status_t valid_errors[kNumValidErrors] = {ZX_ERR_IO, ZX_ERR_IO_DATA_INTEGRITY,
                                               ZX_ERR_BAD_STATE};
  for (uint32_t i = 0; i < kNumValidErrors; i++) {
    UserPager pager;
    ASSERT_TRUE(pager.Init());

    constexpr uint64_t kNumPages = 11;
    Vmo* vmo;
    ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

    zx_status_t status_commit;
    TestThread t_commit([vmo, &status_commit]() -> bool {
      // |status_commit| should get set to the error code passed in via FailPages.
      status_commit =
          vmo->vmo().op_range(ZX_VMO_OP_COMMIT, 0, kNumPages * ZX_PAGE_SIZE, nullptr, 0);
      return (status_commit == ZX_OK);
    });

    zx_status_t status_read;
    TestThread t_read([vmo, &status_read]() -> bool {
      zx::vmo tmp_vmo;
      zx_vaddr_t buf = 0;
      constexpr uint64_t len = kNumPages * ZX_PAGE_SIZE;

      if (zx::vmo::create(len, ZX_VMO_RESIZABLE, &tmp_vmo) != ZX_OK) {
        return false;
      }

      if (zx::vmar::root_self()->map(0, tmp_vmo, 0, len, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                     &buf) != ZX_OK) {
        return false;
      }

      auto unmap = fbl::MakeAutoCall([&]() { zx_vmar_unmap(zx_vmar_root_self(), buf, len); });

      // |status_read| should get set to the error code passed in via FailPages.
      status_read = vmo->vmo().read(reinterpret_cast<void*>(buf), 0, len);
      return (status_read == ZX_OK);
    });

    ASSERT_TRUE(t_commit.Start());
    ASSERT_TRUE(t_commit.WaitForBlocked());
    ASSERT_TRUE(t_read.Start());
    ASSERT_TRUE(t_read.WaitForBlocked());
    ASSERT_TRUE(pager.WaitForPageRead(vmo, 0, kNumPages, ZX_TIME_INFINITE));

    // Fail with a specific valid error code.
    ASSERT_TRUE(pager.FailPages(vmo, 0, kNumPages, valid_errors[i]));

    ASSERT_TRUE(t_commit.WaitForFailure());
    // Verify that op_range(ZX_VMO_OP_COMMIT) returned the provided error code.
    ASSERT_EQ(status_commit, valid_errors[i]);

    ASSERT_TRUE(t_read.WaitForFailure());
    // Verify that vmo_read() returned the provided error code.
    ASSERT_EQ(status_read, valid_errors[i]);

    uint64_t offset, length;
    ASSERT_FALSE(pager.GetPageReadRequest(vmo, 0, &offset, &length));
  }
}

// Test that writing to a forked zero pager marker does not cause a kernel panic. This is a
// regression test for fxbug.dev/53181. Note that although writing to page backed vmo is not strictly
// supported and has no guaranteed semantics it is still currently allowed.
TEST(Pager, WritingZeroFork) {
  zx::pager pager;
  ASSERT_EQ(zx::pager::create(0, &pager), ZX_OK);

  zx::port port;
  ASSERT_EQ(zx::port::create(0, &port), ZX_OK);

  zx::vmo vmo;

  ASSERT_EQ(pager.create_vmo(0, port, 0, ZX_PAGE_SIZE, &vmo), ZX_OK);

  zx::vmo empty;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &empty));

  // Transferring the uncommitted page in empty can be implemented in the kernel by a zero page
  // marker in the pager backed vmo (and not a committed page).
  ASSERT_OK(pager.supply_pages(vmo, 0, ZX_PAGE_SIZE, empty, 0));

  // Writing to this page may cause it to be committed, and if it was a marker it will fork from
  // the zero page.
  // Do not assert that the write succeeds as we do not want to claim that it should, only that
  // doing so should not crash the kernel.
  uint64_t data = 42;
  vmo.write(&data, 0, sizeof(data));

  // Normally forking a zero page puts that page in a special list for one time zero page scanning
  // and merging. Once scanned it goes into the general unswappable page list. Both of these lists
  // are incompatible with a user pager backed vmo. To try and detect this we need to wait for the
  // zero scanner to run, since the zero fork queue looks close enough to the pager backed queue
  // that most things will 'just work'.
  constexpr char k_command[] = "scanner reclaim_all";
  if (!&get_root_resource ||
      zx_debug_send_command(get_root_resource(), k_command, strlen(k_command)) != ZX_OK) {
    // Failed to manually force the zero scanner to run, fall back to sleeping for a moment and hope
    // it runs.
    zx::nanosleep(zx::deadline_after(zx::sec(1)));
  }

  // If our page did go marker->zero fork queue->unswappable this next write will crash the kernel
  // when it attempts to update our position in the pager backed list.
  vmo.write(&data, 0, sizeof(data));
}

// Test that if we resize a vmo while it is waiting on a page to fullfill the commit for a pin
// request that neither the resize nor the pin cause a crash and fail gracefully.
TEST(Pager, ResizeBlockedPin) {
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  if (!&get_root_resource) {
    printf("Root resource not available, skipping\n");
    return;
  }

  UserPager pager;
  ASSERT_TRUE(pager.Init());

  constexpr uint64_t kNumPages = 2;
  Vmo* vmo;
  ASSERT_TRUE(pager.CreateVmo(kNumPages, &vmo));

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  // Spin up a thread to do the pin, this will block as it has to wait for pages from the user pager
  TestThread pin_thread([&bti, &pmt, &vmo]() -> bool {
    zx_paddr_t addr;
    // Pin the second page so we can resize such that there is absolutely no overlap in the ranges.
    // The pin itself is expected to ultimately fail as the resize will complete first.
    return bti.pin(ZX_BTI_PERM_READ, vmo->vmo(), ZX_PAGE_SIZE, ZX_PAGE_SIZE, &addr, 1, &pmt) ==
           ZX_ERR_OUT_OF_RANGE;
  });

  // Wait till the userpager gets the request.
  ASSERT_TRUE(pin_thread.Start());
  ASSERT_TRUE(pager.WaitForPageRead(vmo, 1, 1, ZX_TIME_INFINITE));

  // Resize the VMO down such that the pin request is completely out of bounds. This should succeed
  // as nothing has been pinned yet.
  ASSERT_TRUE(vmo->Resize(0));

  // The pin request should have been implicitly unblocked from the resize, and should have
  // ultimately failed. pin_thread returns true if it got the correct failure result from pin.
  ASSERT_TRUE(pin_thread.Wait());
}

}  // namespace pager_tests
