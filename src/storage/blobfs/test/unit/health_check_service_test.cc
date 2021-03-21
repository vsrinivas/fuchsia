// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/health_check_service.h"

#include <block-client/cpp/fake-device.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob.h"
#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/mkfs.h"
#include "src/storage/blobfs/mount.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "zircon/syscalls.h"

namespace blobfs {
namespace {

namespace fuv = ::fuchsia_update_verify;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

class HealthCheckServiceTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(loop_.StartThread("async-loop"), ZX_OK);

    auto device = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);

    auto blobfs_or = Blobfs::Create(loop_.dispatcher(), std::move(device), MountOptions{});
    ASSERT_TRUE(blobfs_or.is_ok());
    fs_ = std::move(blobfs_or.value());

    svc_ = fbl::MakeRefCounted<HealthCheckService>(loop_.dispatcher(), *fs_);
  }

  void TearDown() override {
    sync_completion_t completion;
    auto cb = [&completion](zx_status_t status) { sync_completion_signal(&completion); };
    vfs_.Shutdown(cb);
    ASSERT_EQ(sync_completion_wait(&completion, ZX_TIME_INFINITE), ZX_OK);
  }

  void InstallBlob(const BlobInfo& info) {
    auto root = OpenRoot();
    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(root->Create(info.path + 1, 0, &file), ZX_OK);
    size_t out_actual = 0;
    ASSERT_EQ(file->Truncate(info.size_data), ZX_OK);
    ASSERT_EQ(file->Write(info.data.get(), info.size_data, 0, &out_actual), ZX_OK);
    ASSERT_EQ(out_actual, info.size_data);
  }

  void CorruptBlob(const BlobInfo& info) {
    ZX_ASSERT(info.size_data);
    uint64_t block;
    {
      auto root = OpenRoot();
      fbl::RefPtr<fs::Vnode> file;
      ASSERT_EQ(root->Lookup(info.path + 1, &file), ZX_OK);
      auto blob = fbl::RefPtr<Blob>::Downcast(file);
      block = fs_->GetNode(blob->Ino())->extents[0].Start() + DataStartBlock(fs_->Info());
    }

    // Unmount.
    std::unique_ptr<block_client::BlockDevice> device = Blobfs::Destroy(std::move(fs_));

    // Read the block that contains the blob.
    storage::VmoBuffer buffer;
    ASSERT_EQ(buffer.Initialize(device.get(), 1, kBlobfsBlockSize, "test_buffer"), ZX_OK);
    block_fifo_request_t request = {
        .opcode = BLOCKIO_READ,
        .vmoid = buffer.vmoid(),
        .length = kBlobfsBlockSize / kBlockSize,
        .vmo_offset = 0,
        .dev_offset = block * kBlobfsBlockSize / kBlockSize,
    };
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

    // Flip a byte.
    uint8_t* target = static_cast<uint8_t*>(buffer.Data(0));
    *target ^= 0xff;

    // Write the block back.
    request.opcode = BLOCKIO_WRITE;
    ASSERT_EQ(device->FifoTransaction(&request, 1), ZX_OK);

    // Remount and try and read the blob.
    auto remounted_blobfs_or =
        Blobfs::Create(loop_.dispatcher(), std::move(device), MountOptions{});
    ASSERT_TRUE(remounted_blobfs_or.is_ok());
    fs_ = std::move(remounted_blobfs_or.value());
    svc_ = fbl::MakeRefCounted<HealthCheckService>(loop_.dispatcher(), *fs_);
  }

  fbl::RefPtr<fs::Vnode> OpenRoot() const {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_EQ(fs_->OpenRootNode(&root), ZX_OK);
    return root;
  }

  fuv::BlobfsVerifier::SyncClient Client() {
    auto endpoints = fidl::CreateEndpoints<fuv::BlobfsVerifier>();
    EXPECT_EQ(endpoints.status_value(), ZX_OK);
    EXPECT_EQ(vfs_.Serve(svc_, fidl::ServerEnd<fuchsia_io::Node>(endpoints->server.TakeChannel()),
                         fs::VnodeConnectionOptions::ReadWrite()),
              ZX_OK);
    return fidl::BindSyncClient(std::move(endpoints->client));
  }

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  fs::ManagedVfs vfs_{loop_.dispatcher()};

  std::unique_ptr<Blobfs> fs_;
  fbl::RefPtr<HealthCheckService> svc_;  // References fs_
};

TEST_F(HealthCheckServiceTest, EmptyFilesystemPassesChecks) {
  fuv::BlobfsVerifier::SyncClient client = Client();
  auto result = client.Verify(fuv::wire::VerifyOptions{});
  ASSERT_TRUE(result.ok()) << result.error();
}

TEST_F(HealthCheckServiceTest, PopulatedFilesystemPassesChecks) {
  auto files = [&]() -> std::vector<fbl::RefPtr<fs::Vnode>> {
    std::vector<fbl::RefPtr<fs::Vnode>> files;
    auto root = OpenRoot();
    for (int i = 0; i < 10; ++i) {
      std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 65536);
      InstallBlob(*info);

      auto& file = files.emplace_back();
      EXPECT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);
      EXPECT_EQ(file->OpenValidating(fs::VnodeConnectionOptions(), nullptr), ZX_OK);
    }
    return files;
  }();

  fuv::BlobfsVerifier::SyncClient client = Client();
  auto result = client.Verify(fuv::wire::VerifyOptions{});
  ASSERT_TRUE(result.ok()) << result.error();
  ASSERT_FALSE(result.value().result.is_err());
}

TEST_F(HealthCheckServiceTest, NullBlobPassesChecks) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 0);
  InstallBlob(*info);

  auto root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);
  ASSERT_EQ(file->OpenValidating(fs::VnodeConnectionOptions(), nullptr), ZX_OK);

  fuv::BlobfsVerifier::SyncClient client = Client();
  auto result = client.Verify(fuv::wire::VerifyOptions{});
  ASSERT_TRUE(result.ok()) << result.error();
  ASSERT_FALSE(result.value().result.is_err());
}

TEST_F(HealthCheckServiceTest, InvalidFileFailsChecks) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 65536);
  InstallBlob(*info);
  CorruptBlob(*info);

  auto root = OpenRoot();
  fbl::RefPtr<fs::Vnode> file;
  ASSERT_EQ(root->Lookup(info->path + 1, &file), ZX_OK);
  ASSERT_EQ(file->OpenValidating(fs::VnodeConnectionOptions(), nullptr), ZX_OK);

  fuv::BlobfsVerifier::SyncClient client = Client();
  auto result = client.Verify(fuv::wire::VerifyOptions{});
  ASSERT_TRUE(result.ok()) << result.error();
  ASSERT_TRUE(result.value().result.is_err());
}

TEST_F(HealthCheckServiceTest, InvalidButClosedFilePassesChecks) {
  std::unique_ptr<BlobInfo> info = GenerateRandomBlob("", 65536);
  InstallBlob(*info);
  CorruptBlob(*info);

  fuv::BlobfsVerifier::SyncClient client = Client();
  auto result = client.Verify(fuv::wire::VerifyOptions{});
  ASSERT_TRUE(result.ok()) << result.error();
  ASSERT_FALSE(result.value().result.is_err());
}

}  // namespace
}  // namespace blobfs
