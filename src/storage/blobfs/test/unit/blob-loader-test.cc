// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-loader.h"

#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>

#include <set>

#include <blobfs/common.h>
#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "blob.h"
#include "blobfs.h"
#include "test/blob_utils.h"
#include "utils.h"

namespace blobfs {
namespace {

using ::testing::TestParamInfo;
using ::testing::TestWithParam;
using ::testing::ValuesIn;

using block_client::FakeBlockDevice;

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 400 * kBlobfsBlockSize / kBlockSize;

class BlobLoaderTest : public TestWithParam<CompressionAlgorithm> {
 public:
  void SetUp() override {
    srand(testing::UnitTest::GetInstance()->random_seed());

    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
    loop_.StartThread();

    options_ = {.compression_settings = {
                    .compression_algorithm = GetParam(),
                }};
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), options_, zx::resource(), &fs_),
              ZX_OK);

    // Pre-seed with some random blobs.
    for (unsigned i = 0; i < 3; i++) {
      AddBlob(1024);
    }
    ASSERT_EQ(Remount(), ZX_OK);
  }

  // Remounts the filesystem, which ensures writes are flushed and caches are wiped.
  zx_status_t Remount() {
    return Blobfs::Create(loop_.dispatcher(), Blobfs::Destroy(std::move(fs_)), options_,
                          zx::resource(), &fs_);
  }

  // AddBlob creates and writes a blob of a specified size to the file system.
  // The contents of the blob are compressible at a realistic level for a typical ELF binary.
  // The returned BlobInfo describes the created blob, but its lifetime is unrelated to the lifetime
  // of the on-disk blob.
  [[maybe_unused]] std::unique_ptr<BlobInfo> AddBlob(size_t sz) {
    fbl::RefPtr<fs::Vnode> root;
    EXPECT_EQ(fs_->OpenRootNode(&root), ZX_OK);
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info;
    GenerateRealisticBlob("", sz, &info);
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    EXPECT_EQ(root_node->Create(info->path, 0, &file), ZX_OK);

    size_t actual;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(actual, info->size_data);
    EXPECT_EQ(file->Close(), ZX_OK);

    return info;
  }

  BlobLoader& loader() { return fs_->loader(); }

  Blobfs* Fs() const { return fs_.get(); }

  CompressionAlgorithm ExpectedAlgorithm() const {
    return options_.compression_settings.compression_algorithm;
  }

  uint32_t LookupInode(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(fs_->Cache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    return vnode->Ino();
  }

  CompressionAlgorithm LookupCompression(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(fs_->Cache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    auto algorithm_or = AlgorithmForInode(vnode->GetNode());
    EXPECT_TRUE(algorithm_or.is_ok());
    return algorithm_or.value();
  }

 protected:
  std::unique_ptr<Blobfs> fs_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  MountOptions options_;
};

// A seperate parameterized test fixture that will only be run with compression algorithms that
// support paging.
using BlobLoaderPagedTest = BlobLoaderTest;

TEST_P(BlobLoaderTest, NullBlob) {
  size_t blob_len = 0;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader().LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  EXPECT_FALSE(data.vmo().is_valid());
  EXPECT_EQ(data.size(), 0ul);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  // We explicitly don't check the compression algorithm was respected here, since files this small
  // don't need to be compressed.

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader().LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderPagedTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  // We explicitly don't check the compression algorithm was respected here, since files this small
  // don't need to be compressed.

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader().LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader().LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader().LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderPagedTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader().LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderPagedTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info = AddBlob(blob_len);
  ASSERT_EQ(Remount(), ZX_OK);
  ASSERT_EQ(LookupCompression(*info), ExpectedAlgorithm());

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader().LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

std::string GetCompressionAlgorithmName(CompressionAlgorithm compression_algorithm) {
  // CompressionAlgorithmToString can't be used because it contains underscores which aren't
  // allowed in test names.
  switch (compression_algorithm) {
    case CompressionAlgorithm::UNCOMPRESSED:
      return "Uncompressed";
    case CompressionAlgorithm::LZ4:
      return "Lz4";
    case CompressionAlgorithm::ZSTD:
      return "Zstd";
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      return "ZstdSeekable";
    case CompressionAlgorithm::CHUNKED:
      return "Chunked";
  }
}

std::string GetTestParamName(const TestParamInfo<CompressionAlgorithm>& param) {
  return GetCompressionAlgorithmName(param.param);
}

constexpr std::array<CompressionAlgorithm, 4> kCompressionAlgorithms = {
    CompressionAlgorithm::UNCOMPRESSED,
    CompressionAlgorithm::ZSTD,
    CompressionAlgorithm::ZSTD_SEEKABLE,
    CompressionAlgorithm::CHUNKED,
};

constexpr std::array<CompressionAlgorithm, 2> kPagingCompressionAlgorithms = {
    CompressionAlgorithm::UNCOMPRESSED,
    CompressionAlgorithm::CHUNKED,
};

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderTest, ValuesIn(kCompressionAlgorithms),
                         GetTestParamName);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderPagedTest, ValuesIn(kPagingCompressionAlgorithms),
                         GetTestParamName);

}  // namespace
}  // namespace blobfs
