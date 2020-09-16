// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/vmo_buffer.h"

#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

namespace storage {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
const unsigned kBlockSize = 8192;
constexpr char kGoldenLabel[] = "test-vmo";

class MockVmoidRegistry : public VmoidRegistry {
 public:
  bool detached() const { return detached_; }

 private:
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) override {
    *out = Vmoid(kGoldenVmoid);
    return ZX_OK;
  }

  zx_status_t BlockDetachVmo(Vmoid vmoid) final {
    EXPECT_EQ(kGoldenVmoid, vmoid.TakeId());
    EXPECT_FALSE(detached_);
    detached_ = true;
    return ZX_OK;
  }

  bool detached_ = false;
};

TEST(VmoBufferTest, EmptyTest) {
  VmoBuffer buffer;
  EXPECT_EQ(buffer.capacity(), 0ul);
  EXPECT_EQ(BLOCK_VMOID_INVALID, buffer.vmoid());
}

TEST(VmoBufferTest, TestLabel) {
  class MockRegistry : public MockVmoidRegistry {
    zx_status_t BlockAttachVmo(const zx::vmo& vmo, Vmoid* out) final {
      char name[ZX_MAX_NAME_LEN];
      EXPECT_EQ(vmo.get_property(ZX_PROP_NAME, name, sizeof(name)), ZX_OK);
      EXPECT_EQ(std::string_view(name), kGoldenLabel);
      *out = Vmoid(kGoldenVmoid);
      return ZX_OK;
    }
  } registry;

  VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);
}

TEST(VmoBufferTest, Initialization) {
  MockVmoidRegistry registry;

  VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);
  EXPECT_EQ(kCapacity, buffer.capacity());
  EXPECT_EQ(kBlockSize, buffer.BlockSize());
  EXPECT_EQ(kGoldenVmoid, buffer.vmoid());
}

TEST(VmoBufferTest, VmoidRegistration) {
  MockVmoidRegistry registry;
  {
    VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);

    EXPECT_FALSE(registry.detached());
  }
  EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveConstructorTest) {
  MockVmoidRegistry registry;

  {
    VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);

    VmoBuffer move_constructed(std::move(buffer));
    EXPECT_EQ(kCapacity, move_constructed.capacity());
    EXPECT_EQ(kBlockSize, move_constructed.BlockSize());
    EXPECT_EQ(kGoldenVmoid, move_constructed.vmoid());
    EXPECT_FALSE(registry.detached());
  }
  EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveAssignmentTest) {
  MockVmoidRegistry registry;

  {
    VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);

    auto buffer2 = std::move(buffer);
    EXPECT_EQ(kCapacity, buffer2.capacity());
    EXPECT_EQ(kBlockSize, buffer2.BlockSize());
    EXPECT_EQ(kGoldenVmoid, buffer2.vmoid());
    EXPECT_FALSE(registry.detached());
  }
  EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveToSelfTest) {
  MockVmoidRegistry registry;

  {
    VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);

    VmoBuffer* addr = &buffer;
    *addr = std::move(buffer);
    EXPECT_EQ(kCapacity, buffer.capacity());
    EXPECT_EQ(kBlockSize, buffer.BlockSize());
    EXPECT_EQ(kGoldenVmoid, buffer.vmoid());
    EXPECT_FALSE(registry.detached());
  }
  EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MappingTest) {
  MockVmoidRegistry registry;

  VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);
  char buf[kBlockSize];
  memset(buf, 'a', sizeof(buf));

  for (size_t i = 0; i < kCapacity; i++) {
    memcpy(buffer.Data(i), buf, kBlockSize);
  }
  for (size_t i = 0; i < kCapacity; i++) {
    EXPECT_EQ(0, memcmp(buf, buffer.Data(i), kBlockSize));
  }
}

TEST(VmoBufferTest, CompareVmoToMapping) {
  MockVmoidRegistry registry;
  VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel), ZX_OK);

  // Fill |buffer| with some arbitrary data via mapping.
  char buf[kBlockSize * kCapacity];
  memset(buf, 'a', sizeof(buf));
  for (size_t i = 0; i < kCapacity; i++) {
    memcpy(buffer.Data(i), buf, kBlockSize);
  }

  // Check that we can read from the VMO directly.
  ASSERT_EQ(buffer.vmo().read(buf, 0, kCapacity * kBlockSize), ZX_OK);

  // The data from the VMO is equivalent to the data from the mapping.
  EXPECT_EQ(0, memcmp(buf, buffer.Data(0), kCapacity * kBlockSize));
}

TEST(VmoBufferTest, Zero) {
  MockVmoidRegistry registry;
  constexpr unsigned kBlocks = 10;
  VmoBuffer buffer;
  ASSERT_EQ(buffer.Initialize(&registry, kBlocks, kBlockSize, kGoldenLabel), ZX_OK);
  static const uint8_t kFill = 0xaf;
  memset(buffer.Data(0), kFill, kBlocks * kBlockSize);
  constexpr unsigned kStart = 5;
  constexpr unsigned kLength = 3;
  buffer.Zero(kStart, kLength);
  uint8_t* p = reinterpret_cast<uint8_t*>(buffer.Data(0));
  for (unsigned i = 0; i < kBlocks * kBlockSize; ++i) {
    if (i < kStart * kBlockSize || i >= (kStart + kLength) * kBlockSize) {
      EXPECT_EQ(kFill, p[i]);
    } else {
      EXPECT_EQ(0, p[i]);
    }
  }
}

}  // namespace
}  // namespace storage
