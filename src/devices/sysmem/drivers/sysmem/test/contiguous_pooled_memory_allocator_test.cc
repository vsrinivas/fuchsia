// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "contiguous_pooled_memory_allocator.h"

#include <lib/fake-bti/bti.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/zx/clock.h>
#include <lib/zx/vmar.h>

#include <vector>

#include <ddk/platform-defs.h>
#include <zxtest/zxtest.h>

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

 private:
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

}  // namespace
}  // namespace sysmem_driver
