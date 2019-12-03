// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/iommu.h>
#include <lib/zx/process.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/types.h>

#include <atomic>
#include <thread>
#include <vector>

#include <unittest/unittest.h>

extern "C" zx_handle_t get_root_resource(void);

namespace {

bool bti_create_test() {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  END_TEST;
}

bool bti_pin_test_helper(bool contiguous_vmo) {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  static constexpr uint64_t kPageCount = 256;
  static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
  zx::vmo vmo;
  if (contiguous_vmo) {
    ASSERT_EQ(zx::vmo::create_contiguous(bti, kVmoSize, 0, &vmo), ZX_OK);
  } else {
    ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);
  }

  zx_paddr_t paddrs[kPageCount];
  zx::pmt pmt;
  ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt), ZX_OK);

  ASSERT_EQ(pmt.unpin(), ZX_OK);

  if (contiguous_vmo) {
    for (unsigned i = 1; i < kPageCount; i++) {
      ASSERT_EQ(paddrs[i], paddrs[0] + i * ZX_PAGE_SIZE);
    }
  }

  END_TEST;
}

bool bti_pin_test() { return bti_pin_test_helper(false); }

bool bti_pin_contiguous_test() { return bti_pin_test_helper(true); }

bool bti_pin_contig_flag_test() {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  static constexpr uint64_t kPageCount = 256;
  static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create_contiguous(bti, kVmoSize, 0, &vmo), ZX_OK);

  zx_paddr_t paddr;
  zx::pmt pmt;
  ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ | ZX_BTI_CONTIGUOUS, vmo, 0, kVmoSize, &paddr, 1, &pmt),
            ZX_OK);

  ASSERT_EQ(pmt.unpin(), ZX_OK);

  END_TEST;
}

bool bti_resize_test() {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo), ZX_OK);

  zx_paddr_t paddrs;
  ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, ZX_PAGE_SIZE, &paddrs, 1, &pmt), ZX_OK);

  EXPECT_EQ(vmo.set_size(0), ZX_ERR_BAD_STATE);

  pmt.unpin();

  END_TEST;
}

bool bti_clone_test() {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  zx::vmo vmo, clone;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo), ZX_OK);
  ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, ZX_PAGE_SIZE, &clone), ZX_OK);

  zx_paddr_t paddrs;
  ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, clone, 0, ZX_PAGE_SIZE, &paddrs, 1, &pmt), ZX_OK);

  clone.reset();

  zx_signals_t o;
  EXPECT_EQ(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &o), ZX_ERR_TIMED_OUT);

  pmt.unpin();

  EXPECT_EQ(vmo.wait_one(ZX_VMO_ZERO_CHILDREN, zx::time::infinite_past(), &o), ZX_OK);

  END_TEST;
}

bool bti_no_delayed_unpin_test() {
  BEGIN_TEST;

  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  // Create the VMO we will pin+unpin
  static constexpr uint64_t kPageCount = 4;
  static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);

  // Spin up a helper that will query handle information of the process. This helper should not
  // cause our unpins to be delayed.
  std::atomic<bool> running = true;

  std::thread thread = std::thread([&running] {
    // Create a vmo and clone it a few times with a semi random hierarchy. Vmo shall have a lot of
    // pages so that we can do long running writes to it.
    static constexpr uint64_t kPageCount = 4096;
    static constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
    zx::vmo vmo;
    zx::vmo::create(kVmoSize, 0, &vmo);

    // Size clones so that our get_info call takes longer, but not too many as only the clone
    // handles that fall into the same batch (batches are currently 32 handles) as our pmt will
    // actually be useful.
    static constexpr int kClones = 16;
    zx::vmo clones[kClones];
    vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, kVmoSize, &clones[0]);
    for (int i = 1; i < kClones; i++) {
      clones[rand() % i].create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, kVmoSize, &clones[i]);
    }
    // To ensure our info querying is slow, spin up another thread to do long running operations on
    // our vmo chain. When tested this made the get_info call take around 500ms.
    std::thread thread = std::thread([&running, &vmo] {
      std::vector<uint8_t> buffer(kVmoSize);
      while (running) {
        vmo.write(buffer.data(), 0, kVmoSize);
      }
    });

    zx::unowned_process self_process{zx::process::self()};
    static constexpr int kMaxInfo = 1024;
    std::vector<zx_info_vmo_t> vmo_info(kMaxInfo);
    while (running) {
      size_t actual, avail;
      self_process->get_info(ZX_INFO_PROCESS_VMOS, vmo_info.data(),
                             kMaxInfo * sizeof(zx_info_vmo_t), &actual, &avail);
    }

    thread.join();
  });

  zx_paddr_t paddrs[kPageCount];

  // Perform pin+unpin+clone some arbitrary number of times to see if we hit the race condition.
  // This part of the test could spuriously succeed, but in my testing that never happened and
  // would typically fail around 1000 iterations in. Do 20000 iterations anyway since these
  // iterations are very fast and do not make the test take any noticeable time.
  for (int i = 0; i < 20000; i++) {
    zx::pmt pmt;
    ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt), ZX_OK);
    ASSERT_EQ(pmt.unpin(), ZX_OK);

    // After unpinning we should be able to make a clone.
    zx::vmo clone;
    ASSERT_EQ(vmo.create_child(ZX_VMO_CHILD_COPY_ON_WRITE, 0, kVmoSize, &clone), ZX_OK);
  }

  running = false;
  thread.join();

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(bti_tests)
RUN_TEST(bti_create_test);
RUN_TEST(bti_pin_test);
RUN_TEST(bti_pin_contiguous_test);
RUN_TEST(bti_pin_contig_flag_test);
RUN_TEST(bti_resize_test);
RUN_TEST(bti_clone_test);
RUN_TEST(bti_no_delayed_unpin_test);
END_TEST_CASE(bti_tests)
