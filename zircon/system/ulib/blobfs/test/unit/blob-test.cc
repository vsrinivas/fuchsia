// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob.h"

#include <condition_variable>

#include <blobfs/common.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "blobfs.h"
#include "test/blob_utils.h"
#include "utils.h"

namespace blobfs {
namespace {

constexpr const char kEmptyBlobName[] =
    "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b";

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

class BlobTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    device_ = device.get();
    ASSERT_OK(FormatFilesystem(device.get()));

    MountOptions options;
    ASSERT_OK(
        Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_));
  }

  void TearDown() override { device_ = nullptr; }

  fbl::RefPtr<fs::Vnode> OpenRoot() const {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_OK(fs_->OpenRootNode(&root));
    return root;
  }

 protected:
  async::Loop loop_{&kAsyncLoopConfigAttachToCurrentThread};

  block_client::FakeBlockDevice* device_;
  std::unique_ptr<Blobfs> fs_;
};

TEST_F(BlobTest, Truncate_WouldOverflow) {
  fbl::RefPtr root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root->Create(&file, kEmptyBlobName, 0));

  EXPECT_EQ(file->Truncate(UINT64_MAX), ZX_ERR_OUT_OF_RANGE);
}

// Tests that Blob::Sync issues the callback in the right way in the right cases. This does not
// currently test that the data was actually written to the block device.
TEST_F(BlobTest, SyncBehavior) {
  auto root = OpenRoot();

  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FAILURES(GenerateRandomBlob("", 64, &info));
  memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

  fbl::RefPtr<fs::Vnode> file;
  ASSERT_OK(root->Create(&file, info->path, 0));

  size_t out_actual = 0;
  EXPECT_OK(file->Truncate(info->size_data));

  // PHASE 1: Incomplete data.
  //
  // Try syncing before the data has been written. This currently issues an error synchronously but
  // we accept either synchronous or asynchronous callbacks.
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_EQ(ZX_ERR_BAD_STATE, status);
    loop->Quit();
  });
  loop_.Run();

  // PHASE 2: Complete data, not yet synced.
  device_->Pause();  // Don't let it sync yet.
  EXPECT_OK(file->Write(info->data.get(), info->size_data, 0, &out_actual));
  EXPECT_EQ(info->size_data, out_actual);

  loop_.ResetQuit();
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_STATUS(ZX_OK, status);
    loop->Quit();
  });

  // Allow the Sync to continue and wait for the reply. The system may issue this callback
  // asynchronously. RunUntilIdle can't be used because the backend posts work to another thread and
  // then back here.
  device_->Resume();
  loop_.Run();

  // PHASE 3: Data previously synced.
  //
  // Once the blob is in a fully synced state, calling Sync on it will complete with success.
  loop_.ResetQuit();
  file->Sync([loop = &loop_](zx_status_t status) {
    EXPECT_EQ(ZX_OK, status);
    loop->Quit();
  });
}

}  // namespace
}  // namespace blobfs
