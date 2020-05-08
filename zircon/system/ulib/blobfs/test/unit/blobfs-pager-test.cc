// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/paged_vmo.h>
#include <lib/async/dispatcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/pager.h>
#include <limits.h>

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>

#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "blob-verifier.h"
#include "pager/page-watcher.h"
#include "pager/user-pager.h"

namespace blobfs {
namespace {

constexpr size_t kPagedVmoSize = 10 * ZX_PAGE_SIZE;
// kBlobSize is intentionally not page-aligned to exercise edge cases.
constexpr size_t kBlobSize = kPagedVmoSize - 42;
constexpr int kNumReadRequests = 100;
constexpr int kNumThreads = 10;

// Like a Blob w.r.t. the pager - creates a VMO linked to the pager and issues reads on it.
class MockBlob {
 public:
  MockBlob(char identifier, UserPager* pager, BlobfsMetrics* metrics) : identifier_(identifier) {
    char data[kBlobSize];
    memset(data, identifier, kBlobSize);

    size_t tree_len;
    Digest root;
    ASSERT_OK(digest::MerkleTreeCreator::Create(data, kBlobSize, &merkle_tree_, &tree_len, &root));

    std::unique_ptr<BlobVerifier> verifier;
    ASSERT_OK(BlobVerifier::Create(std::move(root), metrics, merkle_tree_.get(), tree_len,
                                   kBlobSize, &verifier));

    UserPagerInfo pager_info;
    pager_info.verifier = std::move(verifier);
    pager_info.identifier = identifier_;
    pager_info.data_length_bytes = kBlobSize;

    page_watcher_ = std::make_unique<PageWatcher>(pager, std::move(pager_info));

    ASSERT_OK(page_watcher_->CreatePagedVmo(kPagedVmoSize, &vmo_));

    // Make sure the vmo is valid and of the desired size.
    ASSERT_TRUE(vmo_.is_valid());
    uint64_t vmo_size;
    ASSERT_OK(vmo_.get_size(&vmo_size));
    ASSERT_EQ(vmo_size, kPagedVmoSize);

    // Make sure the vmo is pager-backed.
    zx_info_vmo_t info;
    ASSERT_OK(vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    ASSERT_NE(info.flags & ZX_INFO_VMO_PAGER_BACKED, 0);
  }

  ~MockBlob() { page_watcher_->DetachPagedVmoSync(); }

  void CommitRange(uint64_t offset, uint64_t length) {
    ASSERT_OK(vmo_.op_range(ZX_VMO_OP_COMMIT, offset, length, nullptr, 0));

    zx_info_vmo_t info;
    ZX_ASSERT(vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) == ZX_OK);
    ASSERT_EQ(info.committed_bytes, fbl::round_up(length, ZX_PAGE_SIZE));
  }

  void Read(uint64_t offset, uint64_t length) {
    char buf[length];
    ASSERT_OK(vmo_.read(buf, offset, length));

    if (length > 0) {
      char comp[length];
      memset(comp, identifier_, length);
      // Make sure we got back the expected bytes.
      ASSERT_EQ(memcmp(comp, buf, length), 0);
    }
  }

 private:
  zx::vmo vmo_;
  std::unique_ptr<PageWatcher> page_watcher_;
  char identifier_;
  std::unique_ptr<uint8_t[]> merkle_tree_;
};

// Mock user pager. Defines the UserPager interface such that the result of reads on distinct
// mock blobs can be verified.
class MockPager : public UserPager {
 public:
  MockPager() { ASSERT_OK(InitPager()); }

 private:
  zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) override {
    vmo_ = zx::unowned_vmo(transfer_vmo);
    return ZX_OK;
  }

  zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length, UserPagerInfo* info) override {
    // Fill the transfer buffer with the blob's identifier character, to service page requests. The
    // identifier helps us distinguish between blobs.
    char text[kBlobfsBlockSize];
    memset(text, static_cast<char>(info->identifier), kBlobfsBlockSize);
    for (uint32_t i = 0; i < length; i += kBlobfsBlockSize) {
      zx_status_t status = vmo_->write(text, i, kBlobfsBlockSize);
      if (status != ZX_OK) {
        return status;
      }
    }
    // Zero the tail.
    memset(text, 0, kBlobfsBlockSize);
    return vmo_->write(text, length, fbl::round_up(length, kBlobfsBlockSize) - length);
  }

  zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length, size_t buffer_size,
                                const zx::vmo& transfer_vmo, UserPagerInfo* info) override {
    // buffer_size should always be rounded up to a page.
    EXPECT_EQ(fbl::round_up(length, unsigned{PAGE_SIZE}), buffer_size);
    fzl::VmoMapper mapping;
    auto unmap = fbl::MakeAutoCall([&]() { mapping.Unmap(); });

    // Map the transfer VMO in order to pass the verifier a pointer to the data.
    zx_status_t status = mapping.Map(transfer_vmo, 0, buffer_size, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      return status;
    }
    return info->verifier->VerifyPartial(mapping.start(), length, offset, buffer_size);
  }

  zx::unowned_vmo vmo_;
};

class BlobfsPagerTest : public zxtest::Test {
 public:
  void SetUp() override { pager_ = std::make_unique<MockPager>(); }

  std::unique_ptr<MockBlob> CreateBlob(char identifier = 'z') {
    return std::make_unique<MockBlob>(identifier, pager_.get(), &metrics_);
  }

  void ResetPager() { pager_.reset(); }

 private:
  std::unique_ptr<MockPager> pager_;
  BlobfsMetrics metrics_;
};

class RandomBlobReader {
 public:
  using Seed = std::default_random_engine::result_type;

  RandomBlobReader() : random_engine_(zxtest::Runner::GetInstance()->random_seed()) {}
  RandomBlobReader(Seed seed) : random_engine_(seed) {}

  RandomBlobReader(const RandomBlobReader& other) = default;
  RandomBlobReader& operator=(const RandomBlobReader& other) = default;

  void ReadOnce(MockBlob* blob) {
    auto [offset, length] = GetRandomOffsetAndLength();
    blob->Read(offset, length);
  }

  // Reads the blob kNumReadRequests times. Provided as an operator overload to make it convenient
  // to start a thread with an instance of RandomBlobReader.
  void operator()(MockBlob* blob) {
    for (int i = 0; i < kNumReadRequests; ++i) {
      ReadOnce(blob);
    }
  }

 private:
  std::pair<uint64_t, uint64_t> GetRandomOffsetAndLength() {
    uint64_t offset = std::uniform_int_distribution<uint64_t>(0, kBlobSize)(random_engine_);
    return std::make_pair(
        offset, std::uniform_int_distribution<uint64_t>(0, kBlobSize - offset)(random_engine_));
  }

  std::default_random_engine random_engine_;
};

TEST_F(BlobfsPagerTest, CreateBlob) { auto blob = CreateBlob(); }

TEST_F(BlobfsPagerTest, ReadSequential) {
  auto blob = CreateBlob();
  blob->Read(0, kBlobSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, ReadRandom) {
  auto blob = CreateBlob();
  RandomBlobReader reader;
  reader(blob.get());
}

TEST_F(BlobfsPagerTest, CreateMultipleBlobs) {
  auto blob1 = CreateBlob();
  auto blob2 = CreateBlob();
  auto blob3 = CreateBlob();
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobs) {
  constexpr int kBlobCount = 3;
  std::array<std::unique_ptr<MockBlob>, kBlobCount> blobs = {CreateBlob('x'), CreateBlob('y'),
                                                             CreateBlob('z')};
  RandomBlobReader reader;
  std::default_random_engine random_engine(zxtest::Runner::GetInstance()->random_seed());
  std::uniform_int_distribution distribution(0, kBlobCount - 1);
  for (int i = 0; i < kNumReadRequests; i++) {
    reader.ReadOnce(blobs[distribution(random_engine)].get());
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultithreaded) {
  auto blob = CreateBlob();
  std::array<std::thread, kNumThreads> threads;

  // All the threads will issue reads on the same blob.
  for (int i = 0; i < kNumThreads; i++) {
    threads[i] =
        std::thread(RandomBlobReader(zxtest::Runner::GetInstance()->random_seed() + i), blob.get());
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobsMultithreaded) {
  constexpr int kNumBlobs = 3;
  std::unique_ptr<MockBlob> blobs[kNumBlobs] = {CreateBlob('x'), CreateBlob('y'), CreateBlob('z')};
  std::array<std::thread, kNumBlobs> threads;

  // Each thread will issue reads on a different blob.
  for (int i = 0; i < kNumBlobs; i++) {
    threads[i] = std::thread(RandomBlobReader(zxtest::Runner::GetInstance()->random_seed() + i),
                             blobs[i].get());
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength) {
  auto blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength) {
  auto blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, AsyncLoopShutdown) {
  auto blob = CreateBlob();
  // Verify that we can exit cleanly if the UserPager (and its member async loop) is destroyed.
  ResetPager();
}

}  // namespace
}  // namespace blobfs
