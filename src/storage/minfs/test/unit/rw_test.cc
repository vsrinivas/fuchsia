// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/minfs/bcache.h"
#include "src/storage/minfs/format.h"
#include "src/storage/minfs/lazy_buffer.h"
#include "src/storage/minfs/minfs.h"
#include "src/storage/minfs/minfs_private.h"

namespace minfs {
namespace {

using ::block_client::FakeBlockDevice;
using ReadWriteTest = testing::Test;

// This unit test verifies that minfs, without vfs, behaves as expected
// when zero-length writes are interleaved with non-zero length writes.
TEST_F(ReadWriteTest, WriteZeroLength) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  const int kNumBlocks = 1 << 20;
  auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kMinfsBlockSize);
  ASSERT_TRUE(device);
  std::unique_ptr<Bcache> bcache;
  std::unique_ptr<Minfs> fs;
  ASSERT_EQ(Bcache::Create(std::move(device), kNumBlocks, &bcache), ZX_OK);
  ASSERT_EQ(Mkfs(bcache.get()), ZX_OK);
  ASSERT_EQ(Minfs::Create(loop.dispatcher(), std::move(bcache), MountOptions(), &fs), ZX_OK);
  fbl::RefPtr<VnodeMinfs> root;
  ASSERT_EQ(fs->VnodeGet(&root, kMinfsRootIno), ZX_OK);
  fbl::RefPtr<fs::Vnode> foo;
  ASSERT_EQ(root->Create("foo", 0, &foo), ZX_OK);

  constexpr size_t kBufferSize = 65374;
  std::unique_ptr<uint8_t[]> buffer(new uint8_t[kBufferSize]());

  memset(buffer.get(), 0, kBufferSize);
  uint64_t kLargeOffset = 50ull * 1024ull * 1024ull;
  uint64_t kOffset = 11ull * 1024ull * 1024ull;

  size_t written_len = 0;

  ASSERT_EQ(foo->Write(buffer.get(), kBufferSize, kLargeOffset, &written_len), ZX_OK);
  ASSERT_EQ(written_len, kBufferSize);

  ASSERT_EQ(foo->Write(nullptr, 0, kOffset, &written_len), ZX_OK);
  ASSERT_EQ(written_len, 0ul);

  ASSERT_EQ(foo->Write(buffer.get(), kBufferSize, kLargeOffset - 8192, &written_len), ZX_OK);
  ASSERT_EQ(written_len, kBufferSize);

  foo->Close();
  foo = nullptr;
}

}  // namespace
}  // namespace minfs
