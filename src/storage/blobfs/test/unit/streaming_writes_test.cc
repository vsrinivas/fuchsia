// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <block-client/cpp/fake-device.h>
#include <fbl/vector.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/unit/utils.h"

namespace blobfs {
namespace {

TEST(StreamingWritesTest, FailEarlyTargetCompressionSizeSet) {
  constexpr uint32_t kBlockSize = 512;
  constexpr uint32_t kNumBlocks = 200 * kBlobfsBlockSize / kBlockSize;
  constexpr size_t kBlobSize = 150000;
  auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
  EXPECT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread();

  auto blobfs_or = Blobfs::Create(loop.dispatcher(), std::move(device));
  ASSERT_TRUE(blobfs_or.is_ok());

  fbl::RefPtr<fs::Vnode> root;
  ASSERT_EQ(blobfs_or->OpenRootNode(&root), ZX_OK);
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob(/*mount_path=*/"", kBlobSize);
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Create(info->path + 1, 0, &file), ZX_OK);

  auto blob = fbl::RefPtr<Blob>::Downcast(file);

  // Set an incorrect value for target_compression_size.
  blob->SetTargetCompressionSize(std::numeric_limits<uint64_t>::max());

  // Expect PrepareWrite to fail due to incorrect target_compression_size.
  EXPECT_EQ(blob->PrepareWrite(info->size_data, /*compress=*/true), ZX_ERR_INVALID_ARGS);

  EXPECT_EQ(file->Close(), ZX_OK);
}
}  // namespace
}  // namespace blobfs
