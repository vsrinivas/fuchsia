// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <set>

#include <gtest/gtest.h>

#include "garnet/drivers/gpu/msd-qcom-adreno/src/address_space.h"
#include "garnet/drivers/gpu/msd-qcom-adreno/src/allocating_address_space.h"
#include "platform_iommu.h"

namespace {

class AddressSpaceOwner : public magma::AddressSpaceOwner {
 public:
  magma::PlatformBusMapper* GetBusMapper() override { return nullptr; }
};

class MockIommu : public magma::PlatformIommu {
 public:
  bool Map(uint64_t gpu_addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    if (mapped_addr_.find(gpu_addr) != mapped_addr_.end())
      return false;
    mapped_addr_.insert(gpu_addr);
    return true;
  }

  bool Unmap(uint64_t gpu_addr, magma::PlatformBusMapper::BusMapping* bus_mapping) override {
    if (mapped_addr_.find(gpu_addr) == mapped_addr_.end())
      return false;
    mapped_addr_.erase(gpu_addr);
    return true;
  }

  std::set<uint64_t> mapped_addr_;
};
}  // namespace

TEST(AddressSpace, Size) {
  AddressSpaceOwner owner;
  auto iommu = std::make_shared<MockIommu>();
  constexpr uint64_t kSize = 4096 * 10;
  AddressSpace address_space(&owner, kSize, iommu);
  EXPECT_EQ(kSize, address_space.Size());
}

TEST(AddressSpace, Insert) {
  AddressSpaceOwner owner;
  auto iommu = std::make_shared<MockIommu>();
  constexpr uint64_t kPageSize = 4096;
  constexpr uint64_t kSize = kPageSize * 10;
  AddressSpace address_space(&owner, kSize, iommu);
  EXPECT_FALSE(address_space.Clear(kPageSize, nullptr));
  EXPECT_TRUE(address_space.Insert(kPageSize, nullptr));
  EXPECT_FALSE(address_space.Insert(kPageSize, nullptr));
  EXPECT_TRUE(address_space.Clear(kPageSize, nullptr));
  EXPECT_FALSE(address_space.Clear(kPageSize, nullptr));
}

TEST(PartialAllocatingAddressSpace, Insert) {
  AddressSpaceOwner owner;
  auto iommu = std::make_shared<MockIommu>();
  constexpr uint64_t kPageSize = 4096;
  constexpr uint64_t kSize = kPageSize * 10;
  PartialAllocatingAddressSpace address_space(&owner, kSize, iommu);
  EXPECT_TRUE(address_space.Init(kSize / 2, kSize / 2));
  EXPECT_FALSE(address_space.Clear(kPageSize, nullptr));
  EXPECT_TRUE(address_space.Insert(kPageSize, nullptr));
  EXPECT_FALSE(address_space.Insert(kPageSize, nullptr));
  EXPECT_TRUE(address_space.Clear(kPageSize, nullptr));
  EXPECT_FALSE(address_space.Clear(kPageSize, nullptr));
}

TEST(PartialAllocatingAddressSpace, Alloc) {
  AddressSpaceOwner owner;
  auto iommu = std::make_shared<MockIommu>();
  constexpr uint64_t kPageSize = 4096;
  constexpr uint64_t kSize = kPageSize * 10;
  PartialAllocatingAddressSpace address_space(&owner, kSize, iommu);
  constexpr uint64_t kBase = kSize / 2;
  constexpr uint64_t kAlignPow2 = 12;
  EXPECT_TRUE(address_space.Init(kBase, kSize - kBase));

  uint64_t addr = 0;
  EXPECT_FALSE(address_space.Free(addr));  // Invalid

  EXPECT_TRUE(address_space.Alloc(kPageSize, kAlignPow2, &addr));
  EXPECT_EQ(addr, kBase);
  EXPECT_TRUE(address_space.Free(addr));
  EXPECT_FALSE(address_space.Free(addr));  // Double free

  EXPECT_TRUE(address_space.Alloc(kPageSize, kAlignPow2, &addr));
  EXPECT_EQ(addr, kBase);
  EXPECT_TRUE(address_space.Alloc(kPageSize, kAlignPow2, &addr));
  EXPECT_EQ(addr, kBase + kPageSize);
  EXPECT_TRUE(address_space.Alloc(kPageSize, kAlignPow2, &addr));
  EXPECT_EQ(addr, kBase + 2 * kPageSize);
}
