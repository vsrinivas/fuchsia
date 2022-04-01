// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/vmm/device/phys_mem.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {

const uint32_t kPageSize = zx_system_get_page_size();

using ::testing::ElementsAreArray;

class PhysMemTest : public ::testing::Test {
 public:
  zx_status_t InitializeVmoWithRandomData(uint64_t size) {
    zx_status_t status = zx::vmo::create(size, 0, &vmo_);
    if (status != ZX_OK) {
      return status;
    }

    data_.resize(size);
    for (uint32_t i = 0; i < size; i++) {
      data_[i] = static_cast<uint8_t>(rand() % std::numeric_limits<uint8_t>::max());
    }

    status = vmo_.write(data_.data(), 0, size);
    return status;
  }

 protected:
  zx::vmo vmo_;
  std::vector<uint8_t> data_;
};

using PhysMemDeathTest = PhysMemTest;

TEST_F(PhysMemDeathTest, GetPointerOutsideRange) {
  ASSERT_EQ(ZX_OK, InitializeVmoWithRandomData(static_cast<uint64_t>(kPageSize * 4)));

  PhysMem physmem;
  ASSERT_EQ(ZX_OK, physmem.Init(std::move(vmo_)));

  // VMO is only four pages long, but five pages were requested.
  EXPECT_DEATH(physmem.ptr(0, static_cast<size_t>(kPageSize * 5)), "");
}

TEST_F(PhysMemTest, InitWithoutMemoryLayoutInformation) {
  ASSERT_EQ(ZX_OK, InitializeVmoWithRandomData(static_cast<uint64_t>(kPageSize * 4)));

  PhysMem physmem;
  ASSERT_EQ(ZX_OK, physmem.Init(std::move(vmo_)));

  std::vector<uint8_t> buffer(static_cast<size_t>(kPageSize) * 4);
  uint8_t* mem = reinterpret_cast<uint8_t*>(physmem.ptr(0, static_cast<size_t>(kPageSize) * 4));

  // PhysMem wasn't provided any guest layout information, so the entire guest VMO can be read.
  // Devices doing this still need to only read valid memory, but it will not be enforced.
  memcpy(buffer.data(), mem, static_cast<size_t>(kPageSize) * 4);
  EXPECT_THAT(buffer, ElementsAreArray(data_));
}

TEST_F(PhysMemDeathTest, InitWithMemoryLayoutInformation) {
  ASSERT_EQ(ZX_OK, InitializeVmoWithRandomData(static_cast<uint64_t>(kPageSize * 4)));

  std::vector<GuestMemoryRegion> guest_mem = {
      {.base = 0, .size = kPageSize},
      {.base = static_cast<zx_gpaddr_t>(kPageSize * 3), .size = kPageSize}};
  PhysMem physmem;
  ASSERT_EQ(ZX_OK, physmem.Init(guest_mem, std::move(vmo_)));

  std::vector<uint8_t> buffer(static_cast<size_t>(kPageSize));
  uint8_t* mem = reinterpret_cast<uint8_t*>(physmem.ptr(0, static_cast<size_t>(kPageSize) * 4));

  // These regions are within valid guest memory, and thus can be read without faulting.
  memcpy(buffer.data(), mem, kPageSize);
  EXPECT_THAT(buffer, ElementsAreArray(data_.begin(), data_.begin() + kPageSize));

  memcpy(buffer.data(), mem + static_cast<uint64_t>(kPageSize * 3), kPageSize);
  EXPECT_THAT(buffer,
              ElementsAreArray(data_.begin() + static_cast<uint64_t>(kPageSize * 3), data_.end()));

  // Providing guest memory layout information provides extra protection against coding mistakes,
  // just like acquiring the pointer FX_CHECKs if the length is within the VMO range.
  //
  // Note that we have to write *into* the VMO or the compiler will optimize away this memcpy.
  EXPECT_DEATH(memcpy(mem + kPageSize, buffer.data(), kPageSize), "");
  EXPECT_DEATH(memcpy(mem + static_cast<uint64_t>(kPageSize * 2), buffer.data(), kPageSize), "");
}

TEST_F(PhysMemDeathTest, InitializeWithoutPageAlignment) {
  PhysMem physmem;
  zx::vmo vmo1, vmo2;

  std::vector<GuestMemoryRegion> guest_mem_unaligned_start = {
      {.base = 0 + kPageSize / 2, .size = kPageSize / 2}};
  std::vector<GuestMemoryRegion> guest_mem_unaligned_end = {{.base = 0, .size = kPageSize / 2},
                                                            {.base = kPageSize, .size = kPageSize}};

  EXPECT_DEATH(physmem.Init(guest_mem_unaligned_start, std::move(vmo1)),
               "Guest memory region must start at a page aligned address");
  EXPECT_DEATH(physmem.Init(guest_mem_unaligned_end, std::move(vmo2)),
               "Guest memory region must end at a page aligned address");
}

}  // namespace
