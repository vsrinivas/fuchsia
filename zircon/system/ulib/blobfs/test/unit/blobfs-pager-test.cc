// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/paged_vmo.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/pager.h>
#include <limits.h>
#include <threads.h>

#include <array>
#include <cstdint>
#include <memory>

#include <blobfs/format.h>
#include <zxtest/zxtest.h>

#include "pager/page-watcher.h"
#include "pager/user-pager.h"

namespace blobfs {
namespace {

constexpr uint64_t kPagedVmoSize = 10 * PAGE_SIZE;
constexpr uint64_t kNumReadRequests = 100;
constexpr uint64_t kNumThreads = 10;

// Like a Blob w.r.t. the pager - creates a VMO linked to the pager and issues reads on it.
class MockBlob {
 public:
  MockBlob(char identifier, UserPager* pager)
      : page_watcher_(PageWatcher(pager, static_cast<uint32_t>(identifier))),
        identifier_(identifier) {
    ASSERT_OK(page_watcher_.CreatePagedVmo(kPagedVmoSize, &vmo_));

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

  ~MockBlob() { page_watcher_.DetachPagedVmoSync(); }

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
  PageWatcher page_watcher_;
  char identifier_;
};

// Mock user pager. Defines the UserPager interface such that the result of reads on distinct
// mock blobs can be verified.
class MockPager : public UserPager {
 public:
  MockPager() { InitPager(); }

 private:
  zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) override {
    vmo_ = zx::unowned_vmo(transfer_vmo);
    return ZX_OK;
  }

  zx_status_t PopulateTransferVmo(uint32_t map_index, uint32_t start_block,
                                  uint32_t block_count) override {
    // Fill the transfer buffer with the blob's identifier character, to service page requests. The
    // identifier helps us distinguish between blobs.
    char text[kBlobfsBlockSize];
    memset(text, static_cast<char>(map_index), kBlobfsBlockSize);
    for (uint32_t i = 0; i < block_count; i++) {
      zx_status_t status = vmo_->write(text, i * kBlobfsBlockSize, kBlobfsBlockSize);
      if (status != ZX_OK) {
        return status;
      }
    }
    return ZX_OK;
  }

  zx::unowned_vmo vmo_;
};

class BlobfsPagerTest : public zxtest::Test {
 public:
  void SetUp() override { pager_ = std::make_unique<MockPager>(); }

  std::unique_ptr<MockBlob> CreateBlob(char identifier = 'z') {
    return std::make_unique<MockBlob>(identifier, pager_.get());
  }

  void ResetPager() { pager_.reset(); }

 private:
  std::unique_ptr<MockPager> pager_;
};

void GetRandomOffsetAndLength(unsigned int* seed, uint64_t* offset, uint64_t* length) {
  *offset = rand_r(seed) % kPagedVmoSize;
  *length = rand_r(seed) % (kPagedVmoSize - *offset + 1);
}

struct ReadBlobFnArgs {
  MockBlob* blob;
  unsigned int seed;
};

int ReadBlobFn(void* args) {
  auto fnArgs = static_cast<ReadBlobFnArgs*>(args);

  uint64_t offset, length;
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    GetRandomOffsetAndLength(&fnArgs->seed, &offset, &length);
    fnArgs->blob->Read(offset, length);
  }

  return 0;
}

TEST_F(BlobfsPagerTest, CreateBlob) { auto blob = CreateBlob(); }

TEST_F(BlobfsPagerTest, ReadSequential) {
  auto blob = CreateBlob();
  blob->Read(0, kPagedVmoSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, ReadRandom) {
  auto blob = CreateBlob();
  uint64_t offset, length;
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    unsigned int seed = 0;
    GetRandomOffsetAndLength(&seed, &offset, &length);
    blob->Read(offset, length);
  }
}

TEST_F(BlobfsPagerTest, CreateMultipleBlobs) {
  auto blob1 = CreateBlob();
  auto blob2 = CreateBlob();
  auto blob3 = CreateBlob();
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobs) {
  std::unique_ptr<MockBlob> blobs[3] = {CreateBlob('x'), CreateBlob('y'), CreateBlob('z')};

  uint64_t offset, length;
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    uint64_t index = rand() % 3;
    unsigned int seed = 0;
    GetRandomOffsetAndLength(&seed, &offset, &length);
    blobs[index]->Read(offset, length);
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultithreaded) {
  auto blob = CreateBlob();
  std::array<thrd_t, kNumThreads> threads;
  std::array<ReadBlobFnArgs, kNumThreads> args;

  // All the threads will issue reads on the same blob.
  for (uint64_t i = 0; i < kNumThreads; i++) {
    args[i].blob = blob.get();
    args[i].seed = static_cast<unsigned int>(i);
    ASSERT_EQ(thrd_create(&threads[i], ReadBlobFn, &args[i]), thrd_success);
  }

  for (uint64_t i = 0; i < kNumThreads; i++) {
    int res;
    ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobsMultithreaded) {
  constexpr uint64_t kNumBlobs = 3;
  std::unique_ptr<MockBlob> blobs[kNumBlobs] = {CreateBlob('x'), CreateBlob('y'), CreateBlob('z')};
  std::array<thrd_t, kNumBlobs> threads;
  std::array<ReadBlobFnArgs, kNumThreads> args;

  // Each thread will issue reads on a different blob.
  for (uint64_t i = 0; i < kNumBlobs; i++) {
    args[i].blob = blobs[i].get();
    args[i].seed = static_cast<unsigned int>(i);
    ASSERT_EQ(thrd_create(&threads[i], ReadBlobFn, &args[i]), thrd_success);
  }

  for (uint64_t i = 0; i < kNumBlobs; i++) {
    int res;
    ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }
}

TEST_F(BlobfsPagerTest, AsyncLoopShutdown) {
  auto blob = CreateBlob();
  // Verify that we can exit cleanly if the UserPager (and its member async loop) is destroyed.
  ResetPager();
}

}  // namespace
}  // namespace blobfs
