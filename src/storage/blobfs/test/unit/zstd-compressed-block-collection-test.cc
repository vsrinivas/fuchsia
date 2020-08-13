// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression/zstd-compressed-block-collection.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <string.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <memory>

#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/base/test.h>
#include <zxtest/zxtest.h>

#include "allocator/allocator.h"
#include "blob.h"
#include "blobfs.h"
#include "test/blob_utils.h"

namespace blobfs {
namespace {

const uint32_t kNumFilesystemBlocks = 400;

class ZSTDCompressedBlockCollectionTest : public zxtest::Test {
 public:
  void SetUp() final {
    // Write API used to put desired bytes on block device (uncompressed), not exercise compression
    // code paths.
    MountOptions options = {
        .compression_settings = { .compression_algorithm = CompressionAlgorithm::UNCOMPRESSED, }
    };

    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    ASSERT_OK(
        Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_));
  }

  void AddRandomBlobAndSync(size_t sz, std::unique_ptr<BlobInfo>* out_info) {
    AddRandomBlob(sz, out_info);
    ASSERT_OK(Sync());
  }

  void InitCollection(const BlobInfo& blob_info, uint64_t num_vmo_bytes,
                      std::unique_ptr<ZSTDCompressedBlockCollectionImpl>* out_coll) {
    ASSERT_EQ(0, blob_info.size_merkle % kBlobfsBlockSize);
    uint64_t num_merkle_blocks64 = blob_info.size_merkle / kBlobfsBlockSize;
    ASSERT_LE(num_merkle_blocks64, std::numeric_limits<uint32_t>::max());
    uint32_t num_merkle_blocks = static_cast<uint32_t>(num_merkle_blocks64);
    zx_vm_option_t map_options = ZX_VM_PERM_READ | ZX_VM_PERM_WRITE;
    uint64_t num_vmo_blocks64 = num_vmo_bytes / kBlobfsBlockSize;
    ASSERT_LE(num_vmo_blocks64, std::numeric_limits<uint32_t>::max());
    uint32_t num_vmo_blocks = static_cast<uint32_t>(num_vmo_blocks64);
    ASSERT_OK(mapper_.CreateAndMap(num_vmo_bytes, map_options, nullptr, &vmo_));
    ASSERT_OK(fs_->BlockAttachVmo(vmo_, &vmoid_.GetReference(fs_.get())));

    *out_coll = std::make_unique<ZSTDCompressedBlockCollectionImpl>(
        &vmoid_, num_vmo_blocks, SpaceManager(), TransactionHandler(), NodeFinder(),
        LookupInode(blob_info), num_merkle_blocks);
  }

 protected:
  uint32_t LookupInode(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_OK(digest.Parse(info.path));
    EXPECT_OK(fs_->Cache().Lookup(digest, &node));
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    return vnode->Ino();
  }

  void AddRandomBlob(size_t sz, std::unique_ptr<BlobInfo>* out_info) {
    fbl::RefPtr<fs::Vnode> root;
    ASSERT_OK(fs_->OpenRootNode(&root));
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info;
    ASSERT_NO_FAILURES(GenerateRandomBlob("", sz, &info));
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    ASSERT_OK(root_node->Create(&file, info->path, 0));

    size_t actual;
    EXPECT_OK(file->Truncate(info->size_data));
    EXPECT_OK(file->Write(info->data.get(), info->size_data, 0, &actual));
    EXPECT_EQ(actual, info->size_data);
    EXPECT_OK(file->Close());

    if (out_info != nullptr) {
      *out_info = std::move(info);
    }
  }

  zx_status_t Sync() {
    sync_completion_t completion;
    fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    return sync_completion_wait(&completion, zx::duration::infinite().get());
  }

  Blobfs* Filesystem() { return fs_.get(); }
  SpaceManager* SpaceManager() { return fs_.get(); }
  NodeFinder* NodeFinder() { return fs_->GetNodeFinder(); }
  fs::LegacyTransactionHandler* TransactionHandler() { return fs_.get(); }
  storage::VmoidRegistry* VmoidRegistry() { return fs_.get(); }

  std::unique_ptr<Blobfs> fs_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  zx::vmo vmo_;
  fzl::VmoMapper mapper_;
  storage::OwnedVmoid vmoid_;
};

TEST_F(ZSTDCompressedBlockCollectionTest, SmallBlobRead) {
  constexpr uint32_t kNumDataBlocks = 1;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(false, blob_info->size_merkle > 0);

  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Read only data block in blob.
  constexpr uint32_t kDataBlockOffset = 0;
  constexpr uint32_t kNumReadDataBlocks = kNumDataBlocks;
  ASSERT_OK(coll->Read(kDataBlockOffset, kNumReadDataBlocks));
  EXPECT_EQ(0,
            memcmp(mapper_.start(), blob_info->data.get() + (kDataBlockOffset * kBlobfsBlockSize),
                   kNumReadDataBlocks * kBlobfsBlockSize));
}

TEST_F(ZSTDCompressedBlockCollectionTest, SmallBlobBadOffset) {
  constexpr uint32_t kNumDataBlocks = 1;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(false, blob_info->size_merkle > 0);

  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Attempt to read second data block of one-data-block blob.
  constexpr uint32_t kDataBlockOffset = 1;
  constexpr uint32_t kNumReadDataBlocks = kNumDataBlocks;
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, coll->Read(kDataBlockOffset, kNumReadDataBlocks));
}

TEST_F(ZSTDCompressedBlockCollectionTest, SmallBlobBadNumDataBlocks) {
  constexpr uint32_t kNumDataBlocks = 1;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(false, blob_info->size_merkle > 0);

  // Make VMO large enough for two-block read (even though blob is not large enough).
  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks + 1;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Attempt to read two data blocks of one-data-block-blob.
  constexpr uint32_t kDataBlockOffset = 0;
  constexpr uint32_t kNumReadDataBlocks = kNumVMOBlocks;
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, coll->Read(kDataBlockOffset, kNumReadDataBlocks));
}

TEST_F(ZSTDCompressedBlockCollectionTest, BlobRead) {
  constexpr uint32_t kNumDataBlocks = 4;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(true, blob_info->size_merkle > 0);

  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Read only data block in blob.
  constexpr uint32_t kDataBlockOffset = 0;
  constexpr uint32_t kNumReadDataBlocks = kNumDataBlocks;
  ASSERT_OK(coll->Read(kDataBlockOffset, kNumReadDataBlocks));
  EXPECT_EQ(0,
            memcmp(mapper_.start(), blob_info->data.get() + (kDataBlockOffset * kBlobfsBlockSize),
                   kNumReadDataBlocks * kBlobfsBlockSize));
}

TEST_F(ZSTDCompressedBlockCollectionTest, BadOffset) {
  constexpr uint32_t kNumDataBlocks = 4;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(true, blob_info->size_merkle > 0);

  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Attempt to read second data block of one-data-block blob.
  constexpr uint32_t kDataBlockOffset = 4;
  constexpr uint32_t kNumReadDataBlocks = kNumDataBlocks;
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, coll->Read(kDataBlockOffset, kNumReadDataBlocks));
}

TEST_F(ZSTDCompressedBlockCollectionTest, BadNumDataBlocks) {
  constexpr uint32_t kNumDataBlocks = 4;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(true, blob_info->size_merkle > 0);

  // Make VMO large enough for two-block read (even though blob is not large enough).
  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks + 1;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Attempt to read two data blocks of one-data-block-blob.
  constexpr uint32_t kDataBlockOffset = 0;
  constexpr uint32_t kNumReadDataBlocks = kNumVMOBlocks;
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, coll->Read(kDataBlockOffset, kNumReadDataBlocks));
}

TEST_F(ZSTDCompressedBlockCollectionTest, VMOTooSmall) {
  constexpr uint32_t kNumDataBlocks = 2;
  std::unique_ptr<BlobInfo> blob_info;
  AddRandomBlobAndSync(kNumDataBlocks * kBlobfsBlockSize, &blob_info);
  ASSERT_EQ(true, blob_info->size_merkle > 0);

  // Make VMO too small for read.
  constexpr uint32_t kNumVMOBlocks = kNumDataBlocks - 1;
  std::unique_ptr<ZSTDCompressedBlockCollectionImpl> coll;
  InitCollection(*blob_info, kNumVMOBlocks * kBlobfsBlockSize, &coll);

  // Attempt to read two data blocks of one-data-block-blob.
  constexpr uint32_t kDataBlockOffset = 0;
  constexpr uint32_t kNumReadDataBlocks = kNumDataBlocks;
  ASSERT_EQ(ZX_ERR_OUT_OF_RANGE, coll->Read(kDataBlockOffset, kNumReadDataBlocks));
}

}  // namespace
}  // namespace blobfs
