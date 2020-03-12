// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob.h"

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "utils.h"

namespace blobfs {
namespace {

using block_client::FakeBlockDevice;

constexpr const char kEmptyBlobName[] =
    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

class BlobTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    MountOptions options;
    ASSERT_OK(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, &fs_));
  }

  fbl::RefPtr<fs::Vnode> OpenRoot() const {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_OK(fs_->OpenRootNode(&root));
    return root;
  }

 protected:
  std::unique_ptr<Blobfs> fs_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(BlobTest, Truncate_WouldOverflow) {
  fbl::RefPtr root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root->Create(&file, kEmptyBlobName, 0));

  EXPECT_EQ(file->Truncate(UINT64_MAX), ZX_ERR_OUT_OF_RANGE);
}

}  // namespace
}  // namespace blobfs
