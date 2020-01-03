// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/vmo_buffer.h"

#include <lib/zx/vmo.h>

#include <zxtest/zxtest.h>

namespace storage {
namespace {

const vmoid_t kGoldenVmoid = 5;
const size_t kCapacity = 3;
const uint32_t kBlockSize = 8192;
constexpr char kGoldenLabel[] = "test-vmo";

class MockVmoidRegistry : public VmoidRegistry {
 public:
  bool detached() const { return detached_; }

 private:
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) override {
    *out = kGoldenVmoid;
    return ZX_OK;
  }

  zx_status_t DetachVmo(vmoid_t vmoid) final {
    EXPECT_EQ(kGoldenVmoid, vmoid);
    EXPECT_FALSE(detached_);
    detached_ = true;
    return ZX_OK;
  }

  bool detached_ = false;
};

TEST(VmoBufferTest, EmptyTest) {
  VmoBuffer buffer;
  EXPECT_EQ(0, buffer.capacity());
  EXPECT_EQ(BLOCK_VMOID_INVALID, buffer.vmoid());
}

TEST(VmoBufferTest, TestLabel) {
  class MockRegistry : public MockVmoidRegistry {
    zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final {
      char name[ZX_MAX_NAME_LEN];
      EXPECT_OK(vmo.get_property(ZX_PROP_NAME, name, sizeof(name)));
      EXPECT_STR_EQ(kGoldenLabel, name);
      *out = kGoldenVmoid;
      return ZX_OK;
    }
  } registry;

  VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));
}

TEST(VmoBufferTest, Initialization) {
  MockVmoidRegistry registry;

  VmoBuffer buffer;
  ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));
  EXPECT_EQ(kCapacity, buffer.capacity());
  EXPECT_EQ(kBlockSize, buffer.BlockSize());
  EXPECT_EQ(kGoldenVmoid, buffer.vmoid());
}

TEST(VmoBufferTest, VmoidRegistration) {
  MockVmoidRegistry registry;
  {
    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));

    EXPECT_FALSE(registry.detached());
  }
  EXPECT_TRUE(registry.detached());
}

TEST(VmoBufferTest, MoveConstructorTest) {
  MockVmoidRegistry registry;

  {
    VmoBuffer buffer;
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));

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
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));

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
    ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));

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
  ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));
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
  ASSERT_OK(buffer.Initialize(&registry, kCapacity, kBlockSize, kGoldenLabel));

  // Fill |buffer| with some arbitrary data via mapping.
  char buf[kBlockSize * kCapacity];
  memset(buf, 'a', sizeof(buf));
  for (size_t i = 0; i < kCapacity; i++) {
    memcpy(buffer.Data(i), buf, kBlockSize);
  }

  // Check that we can read from the VMO directly.
  ASSERT_OK(buffer.vmo().read(buf, 0, kCapacity * kBlockSize));

  // The data from the VMO is equivalent to the data from the mapping.
  EXPECT_EQ(0, memcmp(buf, buffer.Data(0), kCapacity * kBlockSize));
}

}  // namespace
}  // namespace storage
