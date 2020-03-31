// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "blob-loader.h"

#include <lib/sync/completion.h>
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

// FakeUserPager is an implementation of UserPager that uses a static backing buffer as its
// data source (rather than a block device).
class FakeUserPager : public UserPager {
 public:
  FakeUserPager() { InitPager(); }
  FakeUserPager(const char* data, size_t len) : data_(new uint8_t[len], len) {
    memcpy(data_.get(), data, len);
    InitPager();
  }

  // HACK: We don't have a good interface for propagating failure to satisfy a page request back
  // to the requesting process. This means the test will hang if the pager thread fails.
  // To avoid this, we will close this VMO (which should be the pager-backed VMO) on failure,
  // which will bubble back down to the main test thread and cause it to fail too.
  void SetVmoToDetachOnFailure(const zx::vmo& vmo) { handle_to_close_on_failure_ = vmo.get(); }

  void AssertHasNoAddressesMapped() { ASSERT_EQ(mapped_addresses_.size(), 0); }

  void AssertHasAddressesMappedAndVerified(std::set<uint64_t> addresses) {
    for (const auto& address : addresses) {
      if (mapped_addresses_.find(address) == mapped_addresses_.end()) {
        ADD_FAILURE("Address 0x%lx not mapped", address);
      } else if (verified_addresses_.find(address) == verified_addresses_.end()) {
        ADD_FAILURE("Address 0x%lx mapped but not verified", address);
      }
    }
  }

 private:
  void AbortMainThread() { zx_pager_detach_vmo(pager_.get(), handle_to_close_on_failure_); }

  zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) override {
    vmo_ = zx::unowned_vmo(transfer_vmo);
    return ZX_OK;
  }

  zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length, UserPagerInfo* info) override {
    if (offset + length > data_.size()) {
      AbortMainThread();
      return ZX_ERR_OUT_OF_RANGE;
    }
    zx_status_t status;
    if ((status = vmo_->write(data_.get() + offset, 0, length)) != ZX_OK) {
      AbortMainThread();
      return status;
    }
    for (const auto& address : AddressRange(offset, length)) {
      mapped_addresses_.insert(address);
    }
    return ZX_OK;
  }

  zx_status_t AlignForVerification(uint64_t* offset, uint64_t* length,
                                   UserPagerInfo* info) override {
    uint64_t data_offset = *offset;
    uint64_t data_length = fbl::min(*length, info->data_length_bytes - data_offset);
    zx_status_t status;
    if ((status = info->verifier->Align(&data_offset, &data_length)) != ZX_OK) {
      AbortMainThread();
      return status;
    }
    *offset = data_offset;
    *length = data_length;
    return ZX_OK;
  }

  zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length, const zx::vmo& transfer_vmo,
                                UserPagerInfo* info) override {
    if (offset + length > data_.size()) {
      AbortMainThread();
      return ZX_ERR_OUT_OF_RANGE;
    }

    // Map the transfer VMO in order to pass the verifier a pointer to the data.
    fzl::VmoMapper mapping;
    auto unmap = fbl::MakeAutoCall([&]() { mapping.Unmap(); });
    zx_status_t status = mapping.Map(transfer_vmo, 0, length, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      AbortMainThread();
      return status;
    }
    if ((status = info->verifier->VerifyPartial(mapping.start(), length, offset)) != ZX_OK) {
      AbortMainThread();
      return status;
    }
    for (const auto& address : AddressRange(offset, length)) {
      verified_addresses_.insert(address);
    }
    return status;
  }

  fbl::Array<uint8_t> data_;
  std::set<uint64_t> mapped_addresses_;
  std::set<uint64_t> verified_addresses_;
  zx::unowned_vmo vmo_;
  zx_handle_t handle_to_close_on_failure_;
};

class BlobLoaderTest : public zxtest::Test {
 public:
  void Init(MountOptions options) {
    auto device = std::make_unique<FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_TRUE(device);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    ASSERT_OK(
        Blobfs::Create(loop_.dispatcher(), std::move(device), &options, zx::resource(), &fs_));

    // Pre-seed with some random blobs.
    for (unsigned i = 0; i < 3; i++) {
      AddRandomBlob(1024, nullptr);
    }
    ASSERT_OK(Sync());
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
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

class UncompressedBlobLoaderTest : public BlobLoaderTest {
 public:
  void SetUp() final {
    MountOptions options;
    options.write_uncompressed = true;
    Init(options);
  }
};

class CompressedBlobLoaderTest : public BlobLoaderTest {
 public:
  void SetUp() final {
    MountOptions options;
    Init(options);
  }
};

void DoTest_NullBlob(BlobLoaderTest* test) {
  size_t blob_len = 0;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager;
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;

  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), &data, &merkle));

  EXPECT_FALSE(data.vmo().is_valid());
  EXPECT_EQ(data.size(), 0);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

TEST_F(CompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Test_NullBlob) { DoTest_NullBlob(this); }

void DoTest_SmallBlob(BlobLoaderTest* test) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager;
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

TEST_F(CompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }
TEST_F(UncompressedBlobLoaderTest, SmallBlob) { DoTest_SmallBlob(this); }

void DoTest_Paged_SmallBlob(BlobLoaderTest* test) {
  size_t blob_len = 1024;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager(info->data.get(), info->size_data);
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), &page_watcher, &data, &merkle));
  pager.SetVmoToDetachOnFailure(data.vmo());

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  pager.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  pager.AssertHasAddressesMappedAndVerified({0ul});

  EXPECT_FALSE(merkle.vmo().is_valid());
  EXPECT_EQ(info->size_merkle, 0);
}

// TODO(44820): Enable when compressed, pageable blobs are supported.
// TEST_F(CompressedBlobLoaderTest, Paged_SmallBlob) { DoTest_Paged_SmallBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Paged_SmallBlob) { DoTest_Paged_SmallBlob(this); }

void DoTest_LargeBlob(BlobLoaderTest* test) {
  size_t blob_len = 1 << 18;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager;
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(CompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }
TEST_F(UncompressedBlobLoaderTest, LargeBlob) { DoTest_LargeBlob(this); }

void DoTest_LargeBlob_NonAlignedLength(BlobLoaderTest* test) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager;
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  ASSERT_OK(loader.LoadBlob(test->LookupInode(*info), &data, &merkle));

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  EXPECT_BYTES_EQ(data.start(), info->data.get(), info->size_data);

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

TEST_F(CompressedBlobLoaderTest, LargeBlob_NonAlignedLength) {
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

  FakeUserPager pager(info->data.get(), info->size_data);
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), &page_watcher, &data, &merkle));
  pager.SetVmoToDetachOnFailure(data.vmo());

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  pager.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  pager.AssertHasAddressesMappedAndVerified(AddressRange(0, 1 << 18));

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

// TODO(44820): Enable when compressed, pageable blobs are supported.
// TEST_F(CompressedBlobLoaderTest, Paged_LargeBlob) { DoTest_Paged_LargeBlob(this); }
TEST_F(UncompressedBlobLoaderTest, Paged_LargeBlob) { DoTest_Paged_LargeBlob(this); }

void DoTest_Paged_LargeBlob_NonAlignedLength(BlobLoaderTest* test) {
  size_t blob_len = (1 << 18) - 1;
  std::unique_ptr<BlobInfo> info;
  test->AddRandomBlob(blob_len, &info);
  ASSERT_OK(test->Sync());

  FakeUserPager pager(info->data.get(), info->size_data);
  BlobLoader loader(test->Fs(), &pager);
  fzl::OwnedVmoMapper data, merkle;
  std::unique_ptr<PageWatcher> page_watcher;
  ASSERT_OK(loader.LoadBlobPaged(test->LookupInode(*info), &page_watcher, &data, &merkle));
  pager.SetVmoToDetachOnFailure(data.vmo());

  ASSERT_TRUE(data.vmo().is_valid());
  ASSERT_GE(data.size(), info->size_data);
  pager.AssertHasNoAddressesMapped();
  // Use vmo::read instead of direct read so that we can synchronously fail if the pager fails.
  fbl::Array<uint8_t> buf(new uint8_t[blob_len], blob_len);
  ASSERT_OK(data.vmo().read(buf.get(), 0, blob_len));
  EXPECT_BYTES_EQ(buf.get(), info->data.get(), info->size_data);
  pager.AssertHasAddressesMappedAndVerified(AddressRange(0, blob_len));

  ASSERT_TRUE(merkle.vmo().is_valid());
  ASSERT_GE(merkle.size(), info->size_merkle);
  EXPECT_BYTES_EQ(merkle.start(), info->merkle.get(), info->size_merkle);
}

// TODO(44820): Enable when compressed, pageable blobs are supported.
// TEST_F(CompressedBlobLoaderTest, Paged_LargeBlob_NonAlignedLength) {
//   DoTest_Paged_LargeBlob_NonAlignedLength(this);
// }
TEST_F(UncompressedBlobLoaderTest, Paged_LargeBlob_NonAlignedLength) {
  DoTest_Paged_LargeBlob_NonAlignedLength(this);
}

}  // namespace
}  // namespace blobfs
