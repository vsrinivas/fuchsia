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

// Returns a set of page-aligned addresses in [start, start+len)
std::set<uint64_t> AddressRange(uint64_t start, uint64_t len) {
  std::set<uint64_t> addresses;
  for (uint64_t address = start; address < start + len; address += ZX_PAGE_SIZE) {
    addresses.insert(address);
  }
  return addresses;
}

// FakeTransferBuffer is an implementation of TransferBuffer that uses a static backing buffer as
// its data source (rather than a block device).
class FakeTransferBuffer : public pager::TransferBuffer {
 public:
  FakeTransferBuffer(const char* data, size_t len) : data_(new uint8_t[len], len) {
    EXPECT_EQ(zx::vmo::create(len, 0, &vmo_), ZX_OK);
    memcpy(data_.get(), data, len);
  }

  void AssertHasNoAddressesMapped() { ASSERT_EQ(mapped_addresses_.size(), 0ul); }

  void AssertHasAddressesMapped(std::set<uint64_t> addresses) {
    for (const auto& address : addresses) {
      if (mapped_addresses_.find(address) == mapped_addresses_.end()) {
        EXPECT_TRUE(false) << "Address " << address << " not mapped";
      }
    }
  }

  zx::status<> Populate(uint64_t offset, uint64_t length, const pager::UserPagerInfo& info) final {
    if (offset % kBlobfsBlockSize != 0) {
      return zx::error(ZX_ERR_INVALID_ARGS);
    }
    if (offset + length > data_.size()) {
      return zx::error(ZX_ERR_OUT_OF_RANGE);
    }
    zx_status_t status;
    if ((status = vmo_.write(data_.get() + offset, 0, length)) != ZX_OK) {
      return zx::error(status);
    }
    for (const auto& address : AddressRange(offset, length)) {
      mapped_addresses_.insert(address);
    }
    return zx::ok();
  }

  const zx::vmo& vmo() const final { return vmo_; }

 private:
  zx::vmo vmo_;
  fbl::Array<uint8_t> data_;
  std::set<uint64_t> mapped_addresses_;
};

class BlobLoaderTest : public TestWithParam<CompressionAlgorithm> {
 public:
  void SetUp() override {
    srand(testing::UnitTest::GetInstance()->random_seed());

    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_EQ(FormatFilesystem(device.get(), FilesystemOptions{}), ZX_OK);
    loop_.StartThread();

    MountOptions options = {.compression_settings = {
                                .compression_algorithm = GetParam(),
                            }};
    ASSERT_EQ(Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_),
              ZX_OK);

    // Pre-seed with some random blobs.
    for (unsigned i = 0; i < 3; i++) {
      AddRandomBlob(1024, nullptr);
    }
    ASSERT_EQ(Sync(), ZX_OK);
  }

  FakeTransferBuffer& InitPager(std::unique_ptr<FakeTransferBuffer> buffer) {
    FakeTransferBuffer& buffer_ref = *buffer;
    auto status_or_pager = pager::UserPager::Create(std::move(buffer), fs_->Metrics());
    EXPECT_TRUE(status_or_pager.is_ok());
    pager_ = std::move(status_or_pager).value();
    return buffer_ref;
  }

  BlobLoader CreateLoader() const {
    auto* fs_ptr = fs_.get();

    zx::status<BlobLoader> loader =
        BlobLoader::Create(fs_ptr, fs_ptr, fs_->GetNodeFinder(), pager_.get(), fs_->Metrics(),
                           fs_->zstd_seekable_blob_collection());
    EXPECT_EQ(loader.status_value(), ZX_OK);
    // TODO(jfsulliv): Pessimizing move seems to be necessary, since otherwise fitx::result::value
    // selects the const-ref variant and the (deleted) copy constructor of BlobLoader is invoked
    // instead. Remove this pessimising move if possible.
    return std::move(loader.value());
  }

  // Sync waits for blobfs to sync with the underlying block device.
  zx_status_t Sync() {
    sync_completion_t completion;
    fs_->Sync([&completion](zx_status_t status) { sync_completion_signal(&completion); });
    return sync_completion_wait(&completion, zx::duration::infinite().get());
  }

  // AddRandomBlob creates and writes a random blob to the file system.
  // |out_info| is optional and is used to retrieve the created file information.
  void AddRandomBlob(size_t sz, std::unique_ptr<BlobInfo>* out_info) {
    fbl::RefPtr<fs::Vnode> root;
    ASSERT_EQ(fs_->OpenRootNode(&root), ZX_OK);
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info;
    GenerateRandomBlob("", sz, &info);
    memmove(info->path, info->path + 1, strlen(info->path));  // Remove leading slash.

    fbl::RefPtr<fs::Vnode> file;
    ASSERT_EQ(root_node->Create(info->path, 0, &file), ZX_OK);

    size_t actual;
    EXPECT_EQ(file->Truncate(info->size_data), ZX_OK);
    EXPECT_EQ(file->Write(info->data.get(), info->size_data, 0, &actual), ZX_OK);
    EXPECT_EQ(actual, info->size_data);
    EXPECT_EQ(file->Close(), ZX_OK);

    if (out_info != nullptr) {
      *out_info = std::move(info);
    }
  }

  Blobfs* Fs() const { return fs_.get(); }

  uint32_t LookupInode(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_EQ(digest.Parse(info.path), ZX_OK);
    EXPECT_EQ(fs_->Cache().Lookup(digest, &node), ZX_OK);
    auto vnode = fbl::RefPtr<Blob>::Downcast(std::move(node));
    return vnode->Ino();
  }

 protected:
  std::unique_ptr<Blobfs> fs_;
  std::unique_ptr<FakeTransferBuffer> buffer_;
  std::unique_ptr<pager::UserPager> pager_;
  BlobLoader loader_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

// A seperate parameterized test fixture that will only be run with compression algorithms that
// support paging.
using BlobLoaderPagedTest = BlobLoaderTest;

TEST_P(BlobLoaderTest, NullBlob) {
  size_t blob_len = 0;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader.LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  EXPECT_FALSE(data.vmo().is_valid());
  EXPECT_EQ(data.size(), 0ul);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader.LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderPagedTest, SmallBlob) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = InitPager(std::move(buffer_ptr));
  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader.LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);
  buffer.AssertHasAddressesMapped({0ul});

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0ul);
}

TEST_P(BlobLoaderTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader.LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_EQ(loader.LoadBlob(LookupInode(*info), nullptr, &data, &merkle), ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_EQ(memcmp(data.start(), info->data.get(), info->size_data), 0);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderPagedTest, LargeBlob) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = InitPager(std::move(buffer_ptr));
  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader.LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);
  buffer.AssertHasAddressesMapped(AddressRange(0, 1 << 18));

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_EQ(memcmp(merkle.start(), info->merkle.get(), info->size_merkle), 0);
}

TEST_P(BlobLoaderPagedTest, LargeBlobWithNonAlignedLength) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  AddRandomBlob(blob_len, &info);
  ASSERT_EQ(Sync(), ZX_OK);

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = InitPager(std::move(buffer_ptr));
  BlobLoader loader = CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_EQ(loader.LoadBlobPaged(LookupInode(*info), nullptr, &page_watcher, &data, &merkle),
            ZX_OK);

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_EQ(data.vmo().read(buf.get(), 0, blob_len), ZX_OK);
  EXPECT_EQ(memcmp(buf.get(), info->data.get(), info->size_data), 0);
  buffer.AssertHasAddressesMapped(AddressRange(0, blob_len));

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

constexpr std::array<CompressionAlgorithm, 3> kPagingCompressionAlgorithms = {
    CompressionAlgorithm::UNCOMPRESSED,
    CompressionAlgorithm::ZSTD_SEEKABLE,
    CompressionAlgorithm::CHUNKED,
};

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderTest, ValuesIn(kCompressionAlgorithms),
                         GetTestParamName);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, BlobLoaderPagedTest, ValuesIn(kPagingCompressionAlgorithms),
                         GetTestParamName);

}  // namespace
}  // namespace blobfs
