// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/bti.h>
#include <lib/zx/job.h>
#include <lib/zx/pager.h>
#include <lib/zx/vmo.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/iommu.h>
#include <zircon/syscalls/object.h>

#include <vector>

#include <zxtest/zxtest.h>

namespace object_info_test {
namespace {

extern "C" __WEAK zx_handle_t get_root_resource();

class KernelStatsGetInfoTest : public zxtest::Test {
 public:
  void SetUp() override {
    if (!get_root_resource) {
      return;
    }

    root_resource_ = zx::unowned_resource(get_root_resource());
    num_cpus_ = zx_system_get_num_cpus();
  }

 protected:
  uint32_t num_cpus_ = 0;
  zx::unowned_resource root_resource_ = zx::unowned_resource(ZX_HANDLE_INVALID);
};

TEST_F(KernelStatsGetInfoTest, KmemStats) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  // Commit (and pin) some pages in regular and pager-backed VMOs, to check for non-zero vmo counts
  // returned by zx_object_get_info().
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  uint64_t buf = 17;
  vmo.write(&buf, 0, sizeof(buf));

  zx::pager pager;
  ASSERT_OK(zx::pager::create(0, &pager));

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  zx::vmo pager_vmo;
  ASSERT_OK(zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE,
                                pager_vmo.reset_and_get_address()));

  ASSERT_OK(zx_pager_supply_pages(pager.get(), pager_vmo.get(), 0, ZX_PAGE_SIZE, vmo.get(), 0));

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  zx_iommu_desc_dummy_t desc;
  ASSERT_OK(zx_iommu_create(root_resource_->get(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()));
  ASSERT_OK(zx::bti::create(iommu, 0, 0xdeadbeef, &bti));
  zx_paddr_t addr;
  ASSERT_OK(bti.pin(ZX_BTI_PERM_READ, vmo, 0, ZX_PAGE_SIZE, &addr, 1, &pmt));

  zx_info_kmem_stats_t buffer;
  size_t actual, avail;

  ASSERT_OK(zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS, &buffer, sizeof(buffer),
                               &actual, &avail));

  EXPECT_EQ(actual, 1);
  EXPECT_EQ(avail, 1);

  // Perform some basic sanity checks.
  EXPECT_GT(buffer.total_bytes, 0);
  EXPECT_LT(buffer.free_bytes, buffer.total_bytes);
  // We pinned a page.
  EXPECT_GT(buffer.wired_bytes, 0);
  EXPECT_LT(buffer.wired_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.total_heap_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.free_heap_bytes, buffer.total_bytes);
  // We committed some pages in VMOs.
  EXPECT_GT(buffer.vmo_bytes, 0);
  EXPECT_LT(buffer.vmo_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.mmu_overhead_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.ipc_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.other_bytes, buffer.total_bytes);

  ASSERT_OK(pmt.unpin());
}

TEST_F(KernelStatsGetInfoTest, KmemStatsInvalidHandle) {
  zx_info_kmem_stats_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(ZX_HANDLE_INVALID, ZX_INFO_KMEM_STATS, &buffer, sizeof(buffer),
                               &actual, &avail),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsBadHandleType) {
  zx_info_kmem_stats_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(zx::job::default_job()->get(), ZX_INFO_KMEM_STATS, &buffer,
                               sizeof(buffer), &actual, &avail),
            ZX_ERR_WRONG_TYPE);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsNullBuffer) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS, nullptr,
                               sizeof(zx_info_kmem_stats_t), &actual, &avail),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsSmallBuffer) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  size_t actual, avail;
  zx_info_kmem_stats_t buffer;
  ASSERT_EQ(
      zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS, &buffer, 0, &actual, &avail),
      ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(actual, 0);
  EXPECT_EQ(avail, 1);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsExtended) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  // Commit (and pin) some pages in regular and pager-backed VMOs, to check for non-zero vmo counts
  // returned by zx_object_get_info().
  zx::vmo vmo;
  ASSERT_OK(zx::vmo::create(ZX_PAGE_SIZE, 0, &vmo));
  uint64_t buf = 17;
  vmo.write(&buf, 0, sizeof(buf));

  zx::pager pager;
  ASSERT_OK(zx::pager::create(0, &pager));

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  zx::vmo pager_vmo;
  ASSERT_OK(zx_pager_create_vmo(pager.get(), 0, port.get(), 0, ZX_PAGE_SIZE,
                                pager_vmo.reset_and_get_address()));

  ASSERT_OK(zx_pager_supply_pages(pager.get(), pager_vmo.get(), 0, ZX_PAGE_SIZE, vmo.get(), 0));

  zx::iommu iommu;
  zx::bti bti;
  zx::pmt pmt;
  zx_iommu_desc_dummy_t desc;
  ASSERT_OK(zx_iommu_create(root_resource_->get(), ZX_IOMMU_TYPE_DUMMY, &desc, sizeof(desc),
                            iommu.reset_and_get_address()));
  ASSERT_OK(zx::bti::create(iommu, 0, 0xdeadbeef, &bti));
  zx_paddr_t addr;
  ASSERT_OK(bti.pin(ZX_BTI_PERM_READ, vmo, 0, ZX_PAGE_SIZE, &addr, 1, &pmt));

  zx_info_kmem_stats_extended_t buffer;
  size_t actual, avail;

  ASSERT_OK(zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS_EXTENDED, &buffer,
                               sizeof(buffer), &actual, &avail));

  EXPECT_EQ(actual, 1);
  EXPECT_EQ(avail, 1);

  // Perform some basic sanity checks.
  EXPECT_GT(buffer.total_bytes, 0);
  EXPECT_LT(buffer.free_bytes, buffer.total_bytes);
  // We pinned a page.
  EXPECT_GT(buffer.wired_bytes, 0);
  EXPECT_LT(buffer.wired_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.total_heap_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.free_heap_bytes, buffer.total_bytes);
  // We committed some pages in VMOs.
  EXPECT_GT(buffer.vmo_bytes, 0);
  EXPECT_LT(buffer.vmo_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.mmu_overhead_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.ipc_bytes, buffer.total_bytes);
  EXPECT_LT(buffer.other_bytes, buffer.total_bytes);
  // We created a pager-backed VMO and committed pages.
  EXPECT_GT(buffer.vmo_pager_total_bytes, 0);
  // Pager backed VMO memory must be <= total VMO memory.
  EXPECT_LE(buffer.vmo_pager_total_bytes, buffer.vmo_bytes);
  // Newest and oldest pager-backed memory must be <= total pager-backed memory.
  EXPECT_LE(buffer.vmo_pager_newest_bytes, buffer.vmo_pager_total_bytes);
  EXPECT_LE(buffer.vmo_pager_oldest_bytes, buffer.vmo_pager_total_bytes);
  EXPECT_LE(buffer.vmo_pager_oldest_bytes + buffer.vmo_pager_newest_bytes,
            buffer.vmo_pager_total_bytes);
  // Discardable counters are currently unimplemented.
  EXPECT_EQ(buffer.vmo_discardable_locked_bytes, 0);
  EXPECT_EQ(buffer.vmo_discardable_unlocked_bytes, 0);

  ASSERT_OK(pmt.unpin());
}

TEST_F(KernelStatsGetInfoTest, KmemStatsExtendedInvalidHandle) {
  zx_info_kmem_stats_extended_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(ZX_HANDLE_INVALID, ZX_INFO_KMEM_STATS_EXTENDED, &buffer,
                               sizeof(buffer), &actual, &avail),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsExtendedBadHandleType) {
  zx_info_kmem_stats_extended_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(zx::job::default_job()->get(), ZX_INFO_KMEM_STATS_EXTENDED, &buffer,
                               sizeof(buffer), &actual, &avail),
            ZX_ERR_WRONG_TYPE);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsExtendedNullBuffer) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS_EXTENDED, nullptr,
                               sizeof(zx_info_kmem_stats_extended_t), &actual, &avail),
            ZX_ERR_INVALID_ARGS);
}

TEST_F(KernelStatsGetInfoTest, KmemStatsExtendedSmallBuffer) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  size_t actual, avail;
  zx_info_kmem_stats_extended_t buffer;
  ASSERT_EQ(zx_object_get_info(root_resource_->get(), ZX_INFO_KMEM_STATS_EXTENDED, &buffer, 0,
                               &actual, &avail),
            ZX_ERR_BUFFER_TOO_SMALL);
  EXPECT_EQ(actual, 0);
  EXPECT_EQ(avail, 1);
}

TEST_F(KernelStatsGetInfoTest, CpuStats) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  zx_info_cpu_stats_t buffer;
  size_t actual, avail;
  // Read a single record.
  ASSERT_OK(zx_object_get_info(root_resource_->get(), ZX_INFO_CPU_STATS, &buffer, sizeof(buffer),
                               &actual, &avail));

  EXPECT_EQ(actual, 1);
  EXPECT_EQ(avail, num_cpus_);

  std::vector<zx_info_cpu_stats_t> buf(num_cpus_);
  // Read all records.
  ASSERT_OK(zx_object_get_info(root_resource_->get(), ZX_INFO_CPU_STATS, buf.data(),
                               buf.size() * sizeof(zx_info_cpu_stats_t), &actual, &avail));

  EXPECT_EQ(actual, num_cpus_);
  EXPECT_EQ(avail, num_cpus_);

  for (uint32_t i = 0; i < num_cpus_; i++) {
    EXPECT_EQ(buf[i].cpu_number, i);
  }
}

TEST_F(KernelStatsGetInfoTest, CpuStatsInvalidHandle) {
  zx_info_cpu_stats_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(ZX_HANDLE_INVALID, ZX_INFO_CPU_STATS, &buffer, sizeof(buffer),
                               &actual, &avail),
            ZX_ERR_BAD_HANDLE);
}

TEST_F(KernelStatsGetInfoTest, CpuStatsBadHandleType) {
  zx_info_cpu_stats_t buffer;
  size_t actual, avail;
  ASSERT_EQ(zx_object_get_info(zx::job::default_job()->get(), ZX_INFO_CPU_STATS, &buffer,
                               sizeof(buffer), &actual, &avail),
            ZX_ERR_WRONG_TYPE);
}

TEST_F(KernelStatsGetInfoTest, CpuStatsNullBuffer) {
  if (!root_resource_->is_valid()) {
    printf("Root resource not available, skipping\n");
    return;
  }

  size_t actual, avail;
  ASSERT_OK(zx_object_get_info(root_resource_->get(), ZX_INFO_CPU_STATS, nullptr,
                               sizeof(zx_info_kmem_stats_t), &actual, &avail));
  EXPECT_EQ(actual, 0);
  EXPECT_EQ(avail, num_cpus_);
}

}  // namespace
}  // namespace object_info_test
