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

#include <zxtest/zxtest.h>

extern "C" zx_handle_t get_root_resource(void);

namespace {

TEST(Bti, Create) {
  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);
}

TEST(Bti, NameSupport) {
  zx::iommu iommu;
  zx::bti bti;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  static char name_buffer[ZX_MAX_NAME_LEN];

  // Initially, there should be no name assigned to the BTI
  ASSERT_OK(bti.get_property(ZX_PROP_NAME, name_buffer, sizeof(name_buffer)));
  ASSERT_EQ(0, strlen(name_buffer));

  // Setting the name to normal name length should succeed.
  const char normal_name[] = "Core Test BTI";
  ASSERT_LE(strlen(normal_name), (ZX_MAX_NAME_LEN - 1), "normal_name would be truncated");
  ASSERT_OK(bti.set_property(ZX_PROP_NAME, normal_name, sizeof(normal_name)));
  ASSERT_OK(bti.get_property(ZX_PROP_NAME, name_buffer, sizeof(name_buffer)));
  ASSERT_STR_EQ(normal_name, name_buffer);

  // Setting the name to long_name should succeed, but the result will be truncated.
  const char long_name[] =
      "0123456789012345678901234567890123456789"
      "0123456789012345678901234567890123456789";
  ASSERT_GT(strlen(long_name), (ZX_MAX_NAME_LEN - 1), "long_name would not be truncated");
  ASSERT_OK(bti.set_property(ZX_PROP_NAME, long_name, sizeof(long_name)));
  ASSERT_OK(bti.get_property(ZX_PROP_NAME, name_buffer, sizeof(name_buffer)));
  ASSERT_EQ(0, name_buffer[sizeof(name_buffer) - 1]);
  ASSERT_BYTES_EQ(long_name, name_buffer, sizeof(name_buffer) - 1);

  // Setting the name to an empty string should be OK.
  const char empty_name[] = "";
  ASSERT_LE(strlen(empty_name), (ZX_MAX_NAME_LEN - 1), "empty_name would be truncated");
  ASSERT_OK(bti.set_property(ZX_PROP_NAME, empty_name, sizeof(empty_name)));
  ASSERT_OK(bti.get_property(ZX_PROP_NAME, name_buffer, sizeof(name_buffer)));
  ASSERT_STR_EQ(empty_name, name_buffer);
}

void bti_pin_test_helper(bool contiguous_vmo) {
  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
}

TEST(Bti, Pin) { bti_pin_test_helper(false); }

TEST(Bti, PinContiguous) { bti_pin_test_helper(true); }

TEST(Bti, PinContigFlag) {
  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
}

TEST(Bti, Resize) {
  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
}

TEST(Bti, Clone) {
  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
}

TEST(Bti, GetInfoTest) {
  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);
  // Query the info on the bti. It should have no pmos, and no quarantines:
  zx_info_bti_t bti_info;
  EXPECT_EQ(bti.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(bti_info.pmo_count, 0);
  EXPECT_EQ(bti_info.quarantine_count, 0);

  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(ZX_PAGE_SIZE, ZX_VMO_RESIZABLE, &vmo), ZX_OK);

  zx_paddr_t paddrs;
  ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, ZX_PAGE_SIZE, &paddrs, 1, &pmt), ZX_OK);

  // Now our bti should have one pmo, and no quarantines:
  EXPECT_EQ(bti.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(bti_info.pmo_count, 1);
  EXPECT_EQ(bti_info.quarantine_count, 0);

  // Delete pmt without unpinning. This should trigger a quarantine.
  pmt.reset();

  // Now our bti should have one pmo, and one quarantines:
  EXPECT_EQ(bti.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(bti_info.pmo_count, 1);
  EXPECT_EQ(bti_info.quarantine_count, 1);

  EXPECT_EQ(bti.release_quarantine(), ZX_OK);
  // Now our bti should have no pmo, and no quarantines:
  EXPECT_EQ(bti.get_info(ZX_INFO_BTI, &bti_info, sizeof(bti_info), nullptr, nullptr), ZX_OK);
  EXPECT_EQ(bti_info.pmo_count, 0);
  EXPECT_EQ(bti_info.quarantine_count, 0);
}

TEST(Bti, NoDelayedUnpin) {
  zx::iommu iommu;
  zx::bti bti;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  zx::unowned_resource root_res(get_root_resource());
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
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
}

TEST(Bti, DecommitRace) {
  zx::iommu iommu;
  zx::bti bti;
  zx_iommu_desc_dummy_t desc;
  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  // Create the VMO we will pin/decommit.
  constexpr uint64_t kPageCount = 64;
  constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
  zx::vmo vmo;
  ASSERT_EQ(zx::vmo::create(kVmoSize, 0, &vmo), ZX_OK);

  // Spin up a helper that will perform the decommits.
  std::atomic<bool> running = true;

  // Flag that indicates the helper thread is up and running in case it takes a bit.
  std::atomic<bool> done_one_iteration = false;
  std::thread thread = std::thread([&running, &done_one_iteration, &vmo] {
    while (running) {
      vmo.op_range(ZX_VMO_OP_DECOMMIT, 0, kVmoSize, nullptr, 0);
      done_one_iteration = true;
    }
  });

  zx_paddr_t paddrs[kPageCount];

  // Wait until at least one iteration of the helper thread is done. Shouldn't take long so no need
  // to yield or sleep.
  while (!done_one_iteration)
    ;

  // Perform pin+unpin some arbitrary number of times to see if we hit the race condition.
  for (int i = 0; i < 20000; i++) {
    zx::pmt pmt;
    ASSERT_EQ(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt), ZX_OK);
    ASSERT_EQ(pmt.unpin(), ZX_OK);
  }

  running = false;
  thread.join();
}

// TODO(fxbug.dev/56205): Re-enable this test when enforcement of the "no pinning
// while there are quarantined pages" rule has been turned on in the kernel.
#if 0
TEST(Bti, QuarantineDisallowsPin) {
  zx::iommu iommu;
  zx::bti bti;
  zx_iommu_desc_dummy_t desc;

  // Please do not use get_root_resource() in new code. See fxbug.dev/31358.
  ASSERT_EQ(zx_iommu_create(get_root_resource(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()),
            ZX_OK);
  ASSERT_EQ(zx::bti::create(iommu, 0, 0xdeadbeef, &bti), ZX_OK);

  // Create and pin a VMO, then allow the pinned VMO to leak while still pinned.
  // Its pages will be added to the quarantine list for the BTI.
  constexpr uint64_t kPageCount = 4;
  constexpr uint64_t kVmoSize = ZX_PAGE_SIZE * kPageCount;
  zx_paddr_t paddrs[kPageCount];
  {
    zx::vmo vmo;
    zx::pmt pmt;
    EXPECT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
    EXPECT_OK(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt));
  }

  // Now that our BTI has a non-empty quarantine list, new pin operations should
  // fail with ZX_ERR_BAD_STATE.
  {
    zx::vmo vmo;
    zx::pmt pmt;
    EXPECT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
    EXPECT_STATUS(ZX_ERR_BAD_STATE,
                  bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt));
  }

  // Release the quarantine on our BTI, sending the quarantined pages back to
  // the page pool
  EXPECT_OK(bti.release_quarantine());

  // Try to pin some pages again.  Now that the quarantine list is clear, this
  // should be allowed again.  Don't forget to unpin the pages we had pinned.
  {
    zx::vmo vmo;
    zx::pmt pmt;
    EXPECT_OK(zx::vmo::create(kVmoSize, 0, &vmo));
    EXPECT_OK(bti.pin(ZX_BTI_PERM_READ, vmo, 0, kVmoSize, paddrs, kPageCount, &pmt));
    EXPECT_OK(pmt.unpin());
  }
}
#endif

}  // namespace
