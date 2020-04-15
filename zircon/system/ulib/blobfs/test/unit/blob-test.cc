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

// This block device allows Fifo transactions to be delayed to test intermediate states, and to
// wait until a Fifo transaction is complete.
class DelayedFakeBlockDevice : public block_client::FakeBlockDevice {
 public:
  DelayedFakeBlockDevice(uint64_t block_count, uint32_t block_size)
      : FakeBlockDevice(block_count, block_size) {}

  // By default transactions are allowed which means FifoTransaction() will process requests. When
  // set to false, requests will be held until this function is called to release.
  //
  // This should be called on the main thread.
  void SetAllowTransactions(bool allow) {
    std::unique_lock<std::mutex> lock(mutex_);

    allow_fifo_ = allow;
    cond_var_.notify_one();
  }

  // BlockDevice override.
  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) override {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_var_.wait(lock, [this] { return allow_fifo_; });

    return FakeBlockDevice::FifoTransaction(requests, count);
  }

 private:
  std::mutex mutex_;
  std::condition_variable cond_var_;

  // Whether FifoTransaction should run. Protected by the mutex_.
  bool allow_fifo_ = true;
};

class BlobTest : public zxtest::Test {
 public:
  void SetUp() override {
    auto device = std::make_unique<DelayedFakeBlockDevice>(kNumBlocks, kBlockSize);
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

  DelayedFakeBlockDevice* device_;
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
  device_->SetAllowTransactions(false);  // Don't let it sync yet.
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
  device_->SetAllowTransactions(true);
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
