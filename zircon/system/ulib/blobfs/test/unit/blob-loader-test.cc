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
#include <blobfs/format.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "blob.h"
#include "blobfs.h"
#include "test/blob_utils.h"
#include "utils.h"

namespace blobfs {
namespace {

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
    ASSERT_OK(zx::vmo::create(len, 0, &vmo_));
    memcpy(data_.get(), data, len);
  }

  void AssertHasNoAddressesMapped() { ASSERT_EQ(mapped_addresses_.size(), 0); }

  void AssertHasAddressesMapped(std::set<uint64_t> addresses) {
    for (const auto& address : addresses) {
      if (mapped_addresses_.find(address) == mapped_addresses_.end()) {
        ADD_FAILURE("Address 0x%lx not mapped", address);
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

class BlobLoaderTest : public zxtest::Test {
 public:
  void Init(CompressionAlgorithm algorithm) {
    srand(zxtest::Runner::GetInstance()->random_seed());

    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    MountOptions options = {.compression_settings = {
                                .compression_algorithm = algorithm,
                            }};
    ASSERT_OK(
        Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_));

    // Pre-seed with some random blobs.
    for (unsigned i = 0; i < 3; i++) {
      AddRandomBlob(1024, nullptr);
    }
    ASSERT_OK(Sync());
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

  Blobfs* Fs() const { return fs_.get(); }

  uint32_t LookupInode(const BlobInfo& info) {
    Digest digest;
    fbl::RefPtr<CacheNode> node;
    EXPECT_OK(digest.Parse(info.path));
    EXPECT_OK(fs_->Cache().Lookup(digest, &node));
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

template <CompressionAlgorithm A>
class BlobLoaderTestVariant : public BlobLoaderTest {
 public:
  void SetUp() final { Init(A); }
};

class UncompressedBlobLoaderTest
    : public BlobLoaderTestVariant<CompressionAlgorithm::UNCOMPRESSED> {};
class ZstdCompressedBlobLoaderTest : public BlobLoaderTestVariant<CompressionAlgorithm::ZSTD> {};
class ZstdSeekableCompressedBlobLoaderTest
    : public BlobLoaderTestVariant<CompressionAlgorithm::ZSTD_SEEKABLE> {};
class ChunkCompressedBlobLoaderTest : public BlobLoaderTestVariant<CompressionAlgorithm::CHUNKED> {
};

void DoTest_NullBlob(BlobLoaderTest* test) {
  size_t blob_len = 0;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), nullptr, &data, &merkle));

  EXPECT_FALSE(data.vmo().is_valid());
  EXPECT_EQ(data.size(), 0);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

TEST_F(ZstdCompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }
TEST_F(ZstdSeekableCompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }
TEST_F(ChunkCompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }

void DoTest_SmallBlob(BlobLoaderTest* test) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), nullptr, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

TEST_F(ZstdCompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }
TEST_F(ZstdSeekableCompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }
TEST_F(ChunkCompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }
TEST_F(UncompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }

void DoTest_Paged_SmallBlob(BlobLoaderTest* test) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = test->InitPager(std::move(buffer_ptr));
  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), nullptr, &page_watcher, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  buffer.AssertHasAddressesMapped({0ul});

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

TEST_F(ZstdSeekableCompressedBlobLoaderTest, Paged_SmallBlob) { DoTest_Paged_SmallBlob(this); }
TEST_F(ChunkCompressedBlobLoaderTest, Paged_SmallBlob) { DoTest_Paged_SmallBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Paged_SmallBlob) { DoTest_Paged_SmallBlob(this); }

void DoTest_LargeBlob(BlobLoaderTest* test) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), nullptr, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(ZstdCompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }
TEST_F(ZstdSeekableCompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }
TEST_F(ChunkCompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }
TEST_F(UncompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }

void DoTest_LargeBlob_NonAlignedLength(BlobLoaderTest* test) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), nullptr, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(ZstdCompressedBlobLoaderTest, LargeBlob_NonAlignedLength) {
  DoTest_LargeBlob_NonAlignedLength(this);
}
TEST_F(ZstdSeekableCompressedBlobLoaderTest, LargeBlob_NonAlignedLength) {
  DoTest_LargeBlob_NonAlignedLength(this);
}
TEST_F(ChunkCompressedBlobLoaderTest, LargeBlob_NonAlignedLength) {
  DoTest_LargeBlob_NonAlignedLength(this);
}
TEST_F(UncompressedBlobLoaderTest, LargeBlob_NonAlignedLength) {
  DoTest_LargeBlob_NonAlignedLength(this);
}

void DoTest_Paged_LargeBlob(BlobLoaderTest* test) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = test->InitPager(std::move(buffer_ptr));
  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), nullptr, &page_watcher, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  buffer.AssertHasAddressesMapped(AddressRange(0, 1 << 18));

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(ZstdSeekableCompressedBlobLoaderTest, Paged_LargeBlob) { DoTest_Paged_LargeBlob(this); }
TEST_F(ChunkCompressedBlobLoaderTest, Paged_LargeBlob) { DoTest_Paged_LargeBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Paged_LargeBlob) { DoTest_Paged_LargeBlob(this); }

void DoTest_Paged_LargeBlob_NonAlignedLength(BlobLoaderTest* test) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  auto buffer_ptr = std::make_unique<FakeTransferBuffer>(info->data.get(), info->size_data);
  FakeTransferBuffer& buffer = test->InitPager(std::move(buffer_ptr));
  BlobLoader loader = test->CreateLoader();

  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<pager::PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), nullptr, &page_watcher, &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  buffer.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  buffer.AssertHasAddressesMapped(AddressRange(0, blob_len));

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(ZstdSeekableCompressedBlobLoaderTest, Paged_LargeBlob_NonAlignedLength) {
  DoTest_Paged_LargeBlob_NonAlignedLength(this);
}
TEST_F(ChunkCompressedBlobLoaderTest, Paged_LargeBlob_NonAlignedLength) {
  DoTest_Paged_LargeBlob_NonAlignedLength(this);
}
TEST_F(UncompressedBlobLoaderTest, Paged_LargeBlob_NonAlignedLength) {
  DoTest_Paged_LargeBlob_NonAlignedLength(this);
}

}  // namespace
}  // namespace blobfs
