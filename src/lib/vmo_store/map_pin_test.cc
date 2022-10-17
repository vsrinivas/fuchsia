// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake-bti/bti.h>
#include <lib/stdcompat/optional.h>

#include <gtest/gtest.h>

#include "src/lib/testing/predicates/status.h"
#include "src/lib/vmo_store/vmo_store.h"

namespace vmo_store {
namespace testing {

class MapPinTest : public ::testing::Test {
 public:
  static constexpr size_t kVmoPages = 4;
  static constexpr size_t kVmoSize = ZX_PAGE_SIZE * kVmoPages;
  using VmoStore = ::vmo_store::VmoStore<::vmo_store::HashTableStorage<size_t, void>>;

  static ::vmo_store::Options DefaultMapOptions() {
    return ::vmo_store::Options{
        ::vmo_store::MapOptions{ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, nullptr}, cpp17::nullopt};
  }

  ::vmo_store::Options DefaultPinOptions() {
    return ::vmo_store::Options{
        cpp17::nullopt,
        ::vmo_store::PinOptions{GetBti(), ZX_BTI_PERM_READ | ZX_BTI_PERM_WRITE, true}};
  }

  static zx::result<size_t> CreateAndRegister(VmoStore& store, size_t vmo_size = kVmoSize) {
    zx::vmo vmo;
    zx_status_t status = zx::vmo::create(vmo_size, 0, &vmo);
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return store.Register(std::move(vmo));
  }

  zx::unowned_bti GetBti() {
    // Lazily create a fake BTI.
    if (!bti_) {
      EXPECT_OK(fake_bti_create(bti_.reset_and_get_address()));
    }
    return zx::unowned_bti(bti_);
  }

 private:
  zx::bti bti_;
};

TEST_F(MapPinTest, Map) {
  VmoStore store(DefaultMapOptions());
  zx::result result = CreateAndRegister(store);
  ASSERT_OK(result.status_value());
  size_t key = result.value();
  auto* stored = store.GetVmo(key);
  ASSERT_NE(stored, nullptr);
  // Check mapped data.
  auto data = stored->data();
  ASSERT_EQ(data.size(), kVmoSize);
  constexpr uint8_t kData[] = {0x01, 0x02, 0x03, 0x04, 0xAA, 0xBB, 0x55};
  ASSERT_OK(stored->vmo()->write(kData, 0, sizeof(kData)));
  ASSERT_TRUE(std::equal(data.begin(), data.begin() + sizeof(kData), kData, kData + sizeof(kData)));
}

TEST_F(MapPinTest, VmarManagerMap) {
  // Check that the VmarManager is used when mapping.
  auto vmar = fzl::VmarManager::Create(kVmoSize * 2);
  ASSERT_NE(vmar, nullptr);
  auto options = DefaultMapOptions();
  options.map->vmar = vmar;
  VmoStore store(std::move(options));
  zx::result result = CreateAndRegister(store);
  ASSERT_OK(result.status_value());
  size_t key = result.value();
  // Assert that the mapped data is within the range of the vmar.
  auto data = store.GetVmo(key)->data();
  ASSERT_GE(static_cast<void*>(data.data()), vmar->start());
  auto* vmar_end = static_cast<uint8_t*>(vmar->start()) + vmar->size();
  ASSERT_LT(data.data(), vmar_end);
}

TEST_F(MapPinTest, Pin) {
  VmoStore store(DefaultPinOptions());
  zx::result result = CreateAndRegister(store);
  ASSERT_OK(result.status_value());
  size_t key = result.value();
  auto* vmo = store.GetVmo(key);
  const auto& pinned = vmo->pinned_vmo();
  ASSERT_EQ(pinned.region_count(), kVmoPages);
  // Test region indexing.
  constexpr uint64_t kOffset = 100;
  fzl::PinnedVmo::Region regions[kVmoPages];
  size_t region_count;
  // Call with region_count zero to evaluate how many regions are necessary.
  ASSERT_STATUS(
      vmo->GetPinnedRegions(ZX_PAGE_SIZE + kOffset, ZX_PAGE_SIZE, nullptr, 0, &region_count),
      ZX_ERR_BUFFER_TOO_SMALL);
  ASSERT_EQ(region_count, 2u);

  ASSERT_OK(vmo->GetPinnedRegions(ZX_PAGE_SIZE + kOffset, ZX_PAGE_SIZE, regions, kVmoPages,
                                  &region_count));
  ASSERT_EQ(region_count, 2u);
  // Physical addresses returned by fake bti are always ZX_PAGE_SIZE.
  EXPECT_EQ(regions[0].phys_addr, ZX_PAGE_SIZE + kOffset);
  EXPECT_EQ(regions[0].size, ZX_PAGE_SIZE - kOffset);
  EXPECT_EQ(regions[1].phys_addr, ZX_PAGE_SIZE);
  EXPECT_EQ(regions[1].size, kOffset);

  // Verify error cases for out of range. It should happen even if region_count is not large enough.
  ASSERT_STATUS(vmo->GetPinnedRegions(kVmoSize, 1, regions, 0, &region_count), ZX_ERR_OUT_OF_RANGE);
  ASSERT_STATUS(vmo->GetPinnedRegions(0, kVmoSize + 1, regions, 0, &region_count),
                ZX_ERR_OUT_OF_RANGE);

  // Check hat we're able to get all the regions and they match the entirety of the pinned
  // structure.
  ASSERT_OK(vmo->GetPinnedRegions(0, kVmoSize, regions, kVmoPages, &region_count));
  ASSERT_EQ(region_count, static_cast<size_t>(vmo->pinned_vmo().region_count()));
  for (size_t i = 0; i < region_count; i++) {
    EXPECT_EQ(regions[i].size, vmo->pinned_vmo().region(i).size);
    EXPECT_EQ(regions[i].phys_addr, vmo->pinned_vmo().region(i).phys_addr);
  }
}

TEST_F(MapPinTest, PinSingleRegion) {
  auto options = DefaultPinOptions();
  // Turn off indexing. If we pin a single region it should still work.
  options.pin->index = false;
  VmoStore store(std::move(options));
  // Create and register a VMO with a single page.
  zx::result result = CreateAndRegister(store, ZX_PAGE_SIZE);
  ASSERT_OK(result.status_value());
  size_t key = result.value();
  auto* vmo = store.GetVmo(key);
  const auto& pinned = vmo->pinned_vmo();
  ASSERT_EQ(pinned.region_count(), 1u);
  // Test region indexing.
  constexpr uint64_t kOffset = 100;
  fzl::PinnedVmo::Region regions[1];
  size_t region_count;
  // Call with region_count zero to evaluate how many regions are necessary.
  ASSERT_STATUS(vmo->GetPinnedRegions(kOffset, ZX_PAGE_SIZE / 2, nullptr, 0, &region_count),
                ZX_ERR_BUFFER_TOO_SMALL);
  ASSERT_EQ(region_count, 1u);

  ASSERT_OK(vmo->GetPinnedRegions(kOffset, ZX_PAGE_SIZE / 2, regions, 1, &region_count));
  ASSERT_EQ(region_count, 1u);
  // Physical addresses returned by fake bti are always ZX_PAGE_SIZE.
  EXPECT_EQ(regions[0].phys_addr, ZX_PAGE_SIZE + kOffset);
  EXPECT_EQ(regions[0].size, ZX_PAGE_SIZE / 2);

  // Verify error cases for out of range. It should happen even if region_count is not large enough.
  ASSERT_STATUS(vmo->GetPinnedRegions(ZX_PAGE_SIZE, 1, regions, 0, &region_count),
                ZX_ERR_OUT_OF_RANGE);
  ASSERT_STATUS(vmo->GetPinnedRegions(0, ZX_PAGE_SIZE + 1, regions, 0, &region_count),
                ZX_ERR_OUT_OF_RANGE);

  // Check hat we're able to get all the regions and they match the entirety of the pinned
  // structure.
  ASSERT_OK(vmo->GetPinnedRegions(0, ZX_PAGE_SIZE, regions, 1, &region_count));
  ASSERT_EQ(region_count, static_cast<size_t>(vmo->pinned_vmo().region_count()));
  for (size_t i = 0; i < region_count; i++) {
    EXPECT_EQ(regions[i].size, vmo->pinned_vmo().region(i).size);
    EXPECT_EQ(regions[i].phys_addr, vmo->pinned_vmo().region(i).phys_addr);
  }

  // Register another larger vmo and verify that we can't get the pinned regions when indexing is
  // turned off.
  result = CreateAndRegister(store);
  ASSERT_OK(result.status_value());
  key = result.value();
  vmo = store.GetVmo(key);
  ASSERT_STATUS(vmo->GetPinnedRegions(0, kVmoSize, regions, 1, &region_count), ZX_ERR_BAD_STATE);
  ASSERT_EQ(region_count, 0u);
}

TEST_F(MapPinTest, NoMapOrPin) {
  VmoStore store(Options{cpp17::nullopt, cpp17::nullopt});
  zx::result result = CreateAndRegister(store);
  ASSERT_OK(result.status_value());
  size_t key = result.value();
  auto* vmo = store.GetVmo(key);
  ASSERT_EQ(vmo->pinned_vmo().region_count(), 0u);
  ASSERT_EQ(vmo->data().size(), 0u);
  size_t region_count;
  ASSERT_STATUS(vmo->GetPinnedRegions(0, 100, nullptr, 0, &region_count), ZX_ERR_BAD_STATE);
  ASSERT_EQ(region_count, 0u);
}

}  // namespace testing
}  // namespace vmo_store
