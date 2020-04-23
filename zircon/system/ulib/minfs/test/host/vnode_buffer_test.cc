// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vnode_buffer.h"

#include <fcntl.h>

#include <fbl/auto_call.h>
#include <fbl/unique_fd.h>
#include <minfs/bcache.h>
#include <zxtest/zxtest.h>

namespace minfs {
namespace {

TEST(VnodeBufferTest, Zero) {
  fbl::unique_fd file(open("/tmp/minfs_host_vnode_buffer_test.dat", O_RDWR | O_CREAT, 0555));
  ASSERT_TRUE(file);
  std::unique_ptr<Bcache> bcache;
  ASSERT_OK(minfs::Bcache::Create(std::move(file), 1, &bcache));
  VnodeBufferType buffer(4096);
  ASSERT_OK(buffer.Attach("test", bcache.get()));
  auto detach = fbl::MakeAutoCall([&]() { EXPECT_OK(buffer.Detach(bcache.get())); });
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
