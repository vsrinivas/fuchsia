// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_memory_allocator.h"

#include <lib/async-testing/test_loop.h>
#include <lib/ddk/platform-defs.h>
#include <lib/fake-bti/bti.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmar.h>

#include <vector>

#include <zxtest/zxtest.h>

#include "lib/async-loop/loop.h"

namespace sysmem_driver {
namespace {

class FakeOwner : public MemoryAllocator::Owner {
 public:
  explicit FakeOwner(inspect::Node* heap_node)
      : heap_node_(heap_node)

  {
    EXPECT_OK(fake_bti_create(bti_.reset_and_get_address()));
  }

  ~FakeOwner() {}

  const zx::bti& bti() override { return bti_; }
  zx_status_t CreatePhysicalVmo(uint64_t base, uint64_t size, zx::vmo* vmo_out) override {
    return zx::vmo::create(size, 0u, vmo_out);
  }
  inspect::Node* heap_node() override { return heap_node_; }
  TableSet& table_set() override { return table_set_; }

 private:
  TableSet table_set_;
  inspect::Node* heap_node_;
  zx::bti bti_;
};

class ContiguousPooledSystem : public zxtest::Test {
 public:
  ContiguousPooledSystem()
      : allocator_(&fake_owner_, kVmoName, &inspector_.GetRoot(), 0u, kVmoSize * kVmoCount,
                   true,    // is_cpu_accessible
                   false,   // is_ready
                   true) {  // can_be_torn_down
    // nothing else to do here
  }

 protected:
  static constexpr uint32_t kVmoSize = 4096;
  static constexpr uint32_t kVmoCount = 1024;
  static constexpr char kVmoName[] = "test-pool";

  inspect::Inspector inspector_;
  FakeOwner fake_owner_{&inspector_.GetRoot()};
  ContiguousPooledMemoryAllocator allocator_;
};

TEST_F(ContiguousPooledSystem, VmoNamesAreSet) {
  EXPECT_OK(allocator_.Init());
  allocator_.set_ready();

  char name[ZX_MAX_NAME_LEN] = {};
  EXPECT_OK(allocator_.GetPoolVmoForTest().get_property(ZX_PROP_NAME, name, sizeof(name)));
  EXPECT_EQ(0u, strcmp(kVmoName, name));

  zx::vmo vmo;
  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
  EXPECT_OK(vmo.get_property(ZX_PROP_NAME, name, sizeof(name)));
  EXPECT_EQ(0u, strcmp("test-pool-child", name));
  allocator_.Delete(std::move(vmo));
}

TEST_F(ContiguousPooledSystem, Full) {
  EXPECT_OK(allocator_.Init());
  allocator_.set_ready();

  auto hierarchy = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  auto* value = hierarchy.value().GetByPath({"test-pool"});
  ASSERT_TRUE(value);

  EXPECT_LT(
      0u,
      value->node().get_property<inspect::UintPropertyValue>("free_at_high_water_mark")->value());

  std::vector<zx::vmo> vmos;
  for (uint32_t i = 0; i < kVmoCount; ++i) {
    zx::vmo vmo;
    EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
    vmos.push_back(std::move(vmo));
  }

  EXPECT_EQ(0u, value->node()
                    .get_property<inspect::UintPropertyValue>("last_allocation_failed_timestamp_ns")
                    ->value());
  auto before_time = zx::clock::get_monotonic();
  zx::vmo vmo;
  EXPECT_NOT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));

  auto after_time = zx::clock::get_monotonic();

  hierarchy = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  value = hierarchy.value().GetByPath({"test-pool"});
  EXPECT_LE(before_time.get(),
            value->node()
                .get_property<inspect::UintPropertyValue>("last_allocation_failed_timestamp_ns")
                ->value());
  EXPECT_GE(after_time.get(),
            value->node()
                .get_property<inspect::UintPropertyValue>("last_allocation_failed_timestamp_ns")
                ->value());

  allocator_.Delete(std::move(vmos[0]));

  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmos[0]));

  // Destroy half of all vmos.
  for (uint32_t i = 0; i < kVmoCount; i += 2) {
    ZX_DEBUG_ASSERT(vmos[i]);
    allocator_.Delete(std::move(vmos[i]));
  }

  // There shouldn't be enough contiguous address space for even 1 extra byte.
  // This check relies on sequential Allocate() calls to a brand-new allocator
  // being laid out sequentially, so isn't a fundamental check - if the
  // allocator's layout strategy changes this check might start to fail
  // without there necessarily being a real problem.
  EXPECT_NOT_OK(allocator_.Allocate(kVmoSize + 1, {}, &vmo));

  // This allocation should fail because there's not enough space in the pool, with or without
  // fragmentation.:
  EXPECT_NOT_OK(allocator_.Allocate(kVmoSize * kVmoCount - 1, {}, &vmo));

  hierarchy = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  value = hierarchy.value().GetByPath({"test-pool"});
  EXPECT_EQ(3u,
            value->node().get_property<inspect::UintPropertyValue>("allocations_failed")->value());
  EXPECT_EQ(1u, value->node()
                    .get_property<inspect::UintPropertyValue>("allocations_failed_fragmentation")
                    ->value());
  // All memory was used at high water.
  EXPECT_EQ(
      0u,
      value->node().get_property<inspect::UintPropertyValue>("max_free_at_high_water")->value());
  EXPECT_EQ(
      0u,
      value->node().get_property<inspect::UintPropertyValue>("free_at_high_water_mark")->value());
  for (auto& vmo : vmos) {
    if (vmo)
      allocator_.Delete(std::move(vmo));
  }
}

TEST_F(ContiguousPooledSystem, GetPhysicalMemoryInfo) {
  EXPECT_OK(allocator_.Init());
  allocator_.set_ready();

  zx_paddr_t base;
  size_t size;
  ASSERT_OK(allocator_.GetPhysicalMemoryInfo(&base, &size));
  EXPECT_EQ(base, FAKE_BTI_PHYS_ADDR);
  EXPECT_EQ(size, kVmoSize * kVmoCount);
}

TEST_F(ContiguousPooledSystem, InitPhysical) {
  // Using fake-bti and the FakeOwner above, it won't be a real physical VMO anyway.
  EXPECT_OK(allocator_.InitPhysical(FAKE_BTI_PHYS_ADDR));
  allocator_.set_ready();

  zx_paddr_t base;
  size_t size;
  ASSERT_OK(allocator_.GetPhysicalMemoryInfo(&base, &size));
  EXPECT_EQ(base, FAKE_BTI_PHYS_ADDR);
  EXPECT_EQ(size, kVmoSize * kVmoCount);

  zx::vmo vmo;
  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
  allocator_.Delete(std::move(vmo));
}

TEST_F(ContiguousPooledSystem, SetReady) {
  EXPECT_OK(allocator_.Init());
  EXPECT_FALSE(allocator_.is_ready());
  zx::vmo vmo;
  EXPECT_EQ(ZX_ERR_BAD_STATE, allocator_.Allocate(kVmoSize, {}, &vmo));
  allocator_.set_ready();
  EXPECT_TRUE(allocator_.is_ready());
  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
  allocator_.Delete(std::move(vmo));
}

TEST_F(ContiguousPooledSystem, GuardPages) {
  async::TestLoop loop;
  const uint32_t kGuardRegionSize = zx_system_get_page_size();
  EXPECT_OK(allocator_.Init());
  allocator_.InitGuardRegion(kGuardRegionSize, true, false, loop.dispatcher());
  allocator_.set_ready();

  zx::vmo vmo;
  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
  EXPECT_EQ(0u, allocator_.failed_guard_region_checks());

  // The guard check happens every 5 seconds, so run for 6 seconds to ensure one
  // happens. We're using a test loop, so it's guaranteed that it runs exactly this length of time.
  constexpr uint32_t kLoopTimeSeconds = 6;
  loop.RunFor(zx::sec(kLoopTimeSeconds));

  EXPECT_EQ(0u, allocator_.failed_guard_region_checks());

  uint8_t data_to_write = 1;
  uint64_t guard_offset = allocator_.GetVmoRegionOffsetForTest(vmo) - 1;
  allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));

  guard_offset = allocator_.GetVmoRegionOffsetForTest(vmo) + kVmoSize + kGuardRegionSize - 1;
  allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));

  loop.RunFor(zx::sec(kLoopTimeSeconds));

  // One each for beginning and end.
  EXPECT_EQ(2u, allocator_.failed_guard_region_checks());
  allocator_.Delete(std::move(vmo));
  // Two more.
  EXPECT_EQ(4u, allocator_.failed_guard_region_checks());
}

TEST_F(ContiguousPooledSystem, ExternalGuardPages) {
  async::TestLoop loop;
  const uint32_t kGuardRegionSize = zx_system_get_page_size();
  EXPECT_OK(allocator_.Init());
  allocator_.InitGuardRegion(kGuardRegionSize, false, false, loop.dispatcher());
  allocator_.set_ready();

  zx::vmo vmo;
  EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
  EXPECT_EQ(0u, allocator_.failed_guard_region_checks());
  // The guard check happens every 5 seconds, so run for 6 seconds to ensure one
  // happens. We're using a test loop, so it's guaranteed that it runs exactly this length of time.
  constexpr uint32_t kLoopTimeSeconds = 6;

  loop.RunFor(zx::sec(kLoopTimeSeconds));

  EXPECT_EQ(0u, allocator_.failed_guard_region_checks());

  uint8_t data_to_write = 1;
  uint64_t guard_offset = 1;
  allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));

  guard_offset = kVmoSize * kVmoCount - 1;
  allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));

  {
    // Write into what would be the internal guard region, to check that it isn't caught.
    uint8_t data_to_write = 1;
    uint64_t guard_offset = allocator_.GetVmoRegionOffsetForTest(vmo) - 1;
    allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));

    guard_offset = allocator_.GetVmoRegionOffsetForTest(vmo) + kVmoSize + kGuardRegionSize - 1;
    allocator_.GetPoolVmoForTest().write(&data_to_write, guard_offset, sizeof(data_to_write));
  }

  loop.RunFor(zx::sec(kLoopTimeSeconds));

  // One each for beginning and end.
  EXPECT_EQ(2u, allocator_.failed_guard_region_checks());
  allocator_.Delete(std::move(vmo));
  // Deleting the allocator won't cause an external guard region check, so the count should be the
  // same.
  EXPECT_EQ(2u, allocator_.failed_guard_region_checks());
}

TEST_F(ContiguousPooledSystem, FreeRegionReporting) {
  EXPECT_OK(allocator_.Init());
  allocator_.set_ready();

  std::vector<zx::vmo> vmos;
  for (uint32_t i = 0; i < kVmoCount; ++i) {
    zx::vmo vmo;
    EXPECT_OK(allocator_.Allocate(kVmoSize, {}, &vmo));
    vmos.push_back(std::move(vmo));
  }

  // We want this pattern: blank filled blank blank filled ...
  for (uint32_t i = 0; i < kVmoCount - 5; i += 5) {
    allocator_.Delete(std::move(vmos[i]));
    allocator_.Delete(std::move(vmos[i + 2]));
    allocator_.Delete(std::move(vmos[i + 3]));
  }

  auto hierarchy = inspect::ReadFromVmo(inspector_.DuplicateVmo());
  auto* value = hierarchy.value().GetByPath({"test-pool"});
  ASSERT_TRUE(value);

  // There should be at least 10 regions each with 2 adjacent VMOs free.
  EXPECT_EQ(10 * 2u * kVmoSize,
            value->node()
                .get_property<inspect::UintPropertyValue>("large_contiguous_region_sum")
                ->value());
  for (auto& vmo : vmos) {
    if (vmo)
      allocator_.Delete(std::move(vmo));
  }
}

}  // namespace
}  // namespace sysmem_driver
