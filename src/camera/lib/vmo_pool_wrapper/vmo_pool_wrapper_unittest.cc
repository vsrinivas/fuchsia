// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/vmo_pool_wrapper/vmo_pool_wrapper.h"

#include <lib/fake-bti/bti.h>
#include <lib/zx/vmo.h>

#include <gtest/gtest.h>

namespace camera {
namespace {

constexpr size_t kVmoTestSize = 512 << 10;  // 512KB
constexpr uint32_t kNumVmos = 20;

class VmoPoolWrapperTest : public testing::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(ZX_OK, fake_bti_create(bti_.reset_and_get_address()));
    for (zx::vmo& vmo : vmo_handles_) {
      ASSERT_EQ(ZX_OK, zx::vmo::create(kVmoTestSize, 0, &vmo));
    }
    zx::unowned_vmo vmos[kNumVmos];
    for (size_t i = 0; i < kNumVmos; ++i) {
      vmos[i] = vmo_handles_[i].borrow();
    }
    ASSERT_EQ(ZX_OK, wrapper_.Init(cpp20::span(vmos, kNumVmos)));
    ASSERT_EQ(kNumVmos, wrapper_.total_buffers());
    ASSERT_EQ(kVmoTestSize, wrapper_.buffer_size());
  }

 protected:
  zx::vmo vmo_handles_[kNumVmos];
  camera::VmoPoolWrapper wrapper_;
  zx::bti bti_;
};

TEST_F(VmoPoolWrapperTest, PinVmosTest) {
  auto buffer = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());

  // The VMOs haven't been pinned yet, so attempting to retrieve
  // their physical address should fail.
  ASSERT_DEATH_IF_SUPPORTED(buffer->physical_address(), "unpinned");
  buffer->Release();

  ASSERT_EQ(ZX_OK, wrapper_.PinVmos(bti_, fzl::VmoPool::RequireContig::No,
                                    fzl::VmoPool::RequireLowMem::Yes));
  buffer = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());

  // After pinning the VMOs, physical address retrieval should pass.
  ASSERT_NO_FATAL_FAILURE(buffer->physical_address());
}

TEST_F(VmoPoolWrapperTest, MapVmosTest) {
  auto buffer = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());

  // The VMOs haven't been mapped yet, so attempting to retrieve
  // their virtual address should fail.
  ASSERT_DEATH_IF_SUPPORTED(buffer->virtual_address(), "unmapped");
  buffer->Release();

  wrapper_.MapVmos();
  buffer = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());

  // After mapping the VMOs, virtual address retrival should pass.
  ASSERT_NO_FATAL_FAILURE(buffer->virtual_address());
}

TEST_F(VmoPoolWrapperTest, ResetTest) {
  auto buffer1 = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer1.has_value());
  auto buffer2 = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer2.has_value());
  auto buffer3 = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer3.has_value());

  ASSERT_EQ(kNumVmos - 3, wrapper_.free_buffers());

  wrapper_.Reset();

  ASSERT_EQ(kNumVmos, wrapper_.free_buffers());
}

TEST_F(VmoPoolWrapperTest, ReleaseBufferTest) {
  auto buffer = wrapper_.LockBufferForWrite();
  ASSERT_TRUE(buffer.has_value());
  ASSERT_EQ(kNumVmos - 1, wrapper_.free_buffers());
  uint32_t buffer_index = buffer->ReleaseWriteLockAndGetIndex();

  wrapper_.ReleaseBuffer(buffer_index);

  ASSERT_EQ(kNumVmos, wrapper_.free_buffers());
}

}  // namespace
}  // namespace camera
