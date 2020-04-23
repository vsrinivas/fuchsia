// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vnode_buffer.h"

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace minfs {
namespace {

TEST(VnodeBufferTest, Zero) {
  class Device : public storage::VmoidRegistry {
   public:
    zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* handle) override {
      *handle = storage::Vmoid(17);
      return ZX_OK;
    }
    zx_status_t BlockDetachVmo(storage::Vmoid handle) override {
      EXPECT_EQ(17, handle.TakeId());
      return ZX_OK;
    }
  } device;

  VnodeBufferType buffer(4096);
  ASSERT_OK(buffer.Attach("test", &device));
  auto detach = fbl::MakeAutoCall([&]() { EXPECT_OK(buffer.Detach(&device)); });
  static const int kBufSize = 65536;
  ASSERT_OK(buffer.Grow(kBufSize));
  static const uint8_t kFill = 0xaf;
  memset(buffer.Data(0), kFill, kBufSize);
  const int start = 10017;
  const int length = 9005;
  buffer.Zero(start, length);
  uint8_t* p = reinterpret_cast<uint8_t*>(buffer.Data(0));
  for (int i = 0; i < kBufSize; ++i) {
    if (i < start || i >= start + length) {
      EXPECT_EQ(kFill, p[i]);
    } else {
      EXPECT_EQ(0, p[i]);
    }
  }
}

}  // namespace
}  // namespace minfs
