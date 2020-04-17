// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/paged_vmo.h>
#include <lib/async/dispatcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/pager.h>
#include <limits.h>
#include <threads.h>

#include <array>
#include <cstdint>
#include <memory>

#include <blobfs/compression-algorithm.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

#include "blob-verifier.h"
#include "compression/blob-compressor.h"
#include "compression/chunked.h"
#include "pager/page-watcher.h"
#include "pager/user-pager.h"

namespace blobfs {
namespace {

// Relatively large blobs are used to exercise paging multi-frame compressed blobs.
constexpr uint64_t kPagedVmoSize = 100 * ZX_PAGE_SIZE;
// kBlobSize is intentionally not page-aligned to exercise edge cases.
constexpr uint64_t kBlobSize = kPagedVmoSize - 42;
constexpr uint64_t kNumReadRequests = 100;
constexpr uint64_t kNumThreads = 10;

// Like a Blob w.r.t. the pager - creates a VMO linked to the pager and issues reads on it.
class MockBlob {
 public:
  MockBlob(char identifier, zx::vmo vmo, fbl::Array<uint8_t> raw_data,
           std::unique_ptr<PageWatcher> watcher, std::unique_ptr<uint8_t[]> merkle_tree)
      : identifier_(identifier),
        vmo_(std::move(vmo)),
        raw_data_(std::move(raw_data)),
        page_watcher_(std::move(watcher)),
        merkle_tree_(std::move(merkle_tree)) {}

  ~MockBlob() { page_watcher_->DetachPagedVmoSync(); }

  void CommitRange(uint64_t offset, uint64_t length) {
    ASSERT_OK(vmo_.op_range(ZX_VMO_OP_COMMIT, offset, length, nullptr, 0));

    zx_info_vmo_t info;
    ZX_ASSERT(vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) == ZX_OK);
    ASSERT_EQ(info.committed_bytes, fbl::round_up(length, ZX_PAGE_SIZE));
  }

  void Read(uint64_t offset, uint64_t length) {
    fbl::Array<char> buf(new char[length], length);
    ASSERT_OK(vmo_.read(buf.get(), offset, length));

    length = fbl::min(length, kBlobSize - offset);
    if (length > 0) {
      fbl::Array<char> comp(new char[length], length);
      memset(comp.get(), identifier_, length);
      // Make sure we got back the expected bytes.
      ASSERT_EQ(memcmp(comp.get(), buf.get(), length), 0);
    }
  }

  // Access the data as it would be physically stored on-disk.
  const uint8_t* raw_data() const { return raw_data_.data(); }

 private:
  char identifier_;
  zx::vmo vmo_;
  fbl::Array<uint8_t> raw_data_;
  std::unique_ptr<PageWatcher> page_watcher_;
  std::unique_ptr<uint8_t[]> merkle_tree_;
};

class MockBlobFactory {
 public:
  explicit MockBlobFactory(UserPager* pager) : pager_(pager) {}

  std::unique_ptr<MockBlob> CreateBlob(char identifier, CompressionAlgorithm algorithm) {
    fbl::Array<uint8_t> data(new uint8_t[kBlobSize], kBlobSize);
    memset(data.get(), identifier, kBlobSize);

    // Generate the merkle tree based on the uncompressed contents (i.e. |data|).
    size_t tree_len;
    Digest root;
    std::unique_ptr<uint8_t[]> merkle_tree;
    EXPECT_OK(
        digest::MerkleTreeCreator::Create(data.get(), kBlobSize, &merkle_tree, &tree_len, &root));

    std::unique_ptr<BlobVerifier> verifier;
    EXPECT_OK(BlobVerifier::Create(std::move(root), &metrics_, merkle_tree.get(), tree_len,
                                   kBlobSize, &verifier));

    // Generate the contents as they would be stored on disk. (This includes compression if
    // applicable)
    fbl::Array<uint8_t> raw_data = GenerateData(data.get(), kBlobSize, algorithm);

    UserPagerInfo pager_info;
    pager_info.verifier = std::move(verifier);
    pager_info.identifier = identifier;
    pager_info.data_length_bytes = kBlobSize;
    pager_info.decompressor = CreateDecompressor(raw_data, kBlobSize, algorithm);

    auto page_watcher = std::make_unique<PageWatcher>(pager_, std::move(pager_info));

    zx::vmo vmo;
    EXPECT_OK(page_watcher->CreatePagedVmo(kPagedVmoSize, &vmo));

    // Make sure the vmo is valid and of the desired size.
    EXPECT_TRUE(vmo.is_valid());
    uint64_t vmo_size;
    EXPECT_OK(vmo.get_size(&vmo_size));
    EXPECT_EQ(vmo_size, kPagedVmoSize);

    // Make sure the vmo is pager-backed.
    zx_info_vmo_t info;
    EXPECT_OK(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_NE(info.flags & ZX_INFO_VMO_PAGER_BACKED, 0);

    return std::make_unique<MockBlob>(identifier, std::move(vmo), std::move(raw_data),
                                      std::move(page_watcher), std::move(merkle_tree));
  }

 private:
  fbl::Array<uint8_t> GenerateData(const uint8_t* input, size_t len,
                                   CompressionAlgorithm algorithm) {
    if (algorithm == CompressionAlgorithm::UNCOMPRESSED) {
      fbl::Array<uint8_t> out(new uint8_t[len], len);
      memcpy(out.data(), input, len);
      return out;
    }
    std::optional<BlobCompressor> compressor = BlobCompressor::Create(algorithm, len);
    EXPECT_TRUE(compressor);
    EXPECT_OK(compressor->Update(input, len));
    EXPECT_OK(compressor->End());
    size_t out_size = compressor->Size();
    fbl::Array<uint8_t> out(new uint8_t[out_size], out_size);
    memcpy(out.data(), compressor->Data(), out_size);
    return out;
  }

  std::unique_ptr<SeekableDecompressor> CreateDecompressor(const fbl::Array<uint8_t>& data,
                                                           size_t blob_size,
                                                           CompressionAlgorithm algorithm) {
    if (algorithm == CompressionAlgorithm::UNCOMPRESSED) {
      return nullptr;
    } else if (algorithm != CompressionAlgorithm::CHUNKED) {
      // Other compression algorithms do not support paging.
      EXPECT_TRUE(false);
    }
    std::unique_ptr<SeekableDecompressor> decompressor;
    EXPECT_OK(SeekableChunkedDecompressor::CreateDecompressor(data.get(), data.size(), blob_size,
                                                              &decompressor));
    return decompressor;
  }

  BlobfsMetrics metrics_;
  UserPager* pager_;
};

// Mock user pager. Defines the UserPager interface such that the result of reads on distinct
// mock blobs can be verified.
class MockPager : public UserPager {
 public:
  MockPager() : factory_(this) { InitPager(); }

  MockBlob* CreateBlob(char identifier, CompressionAlgorithm algorithm) {
    EXPECT_EQ(blob_registry_.find(identifier), blob_registry_.end());
    blob_registry_[identifier] = factory_.CreateBlob(identifier, algorithm);
    return blob_registry_[identifier].get();
  }

 private:
  zx_status_t AttachTransferVmo(const zx::vmo& transfer_vmo) override {
    vmo_ = zx::unowned_vmo(transfer_vmo);
    return ZX_OK;
  }

  zx_status_t PopulateTransferVmo(uint64_t offset, uint64_t length, UserPagerInfo* info) override {
    char identifier = static_cast<char>(info->identifier);
    EXPECT_NE(blob_registry_.find(identifier), blob_registry_.end());
    const MockBlob& blob = *blob_registry_[identifier];
    EXPECT_EQ(offset % kBlobfsBlockSize, 0);
    EXPECT_LE(offset + length, info->data_length_bytes);
    // Fill the transfer buffer with the blob's identifier character, to service page requests. The
    // identifier helps us distinguish between blobs.
    EXPECT_OK(vmo_->write(blob.raw_data() + offset, 0, length));
    length = fbl::min(length, kBlobSize - offset);
    return ZX_OK;
  }

  zx_status_t VerifyTransferVmo(uint64_t offset, uint64_t length, const zx::vmo& transfer_vmo,
                                UserPagerInfo* info) override {
    fzl::VmoMapper mapping;
    auto unmap = fbl::MakeAutoCall([&]() { mapping.Unmap(); });

    // Map the transfer VMO in order to pass the verifier a pointer to the data.
    zx_status_t status = mapping.Map(transfer_vmo, 0, length, ZX_VM_PERM_READ);
    if (status != ZX_OK) {
      return status;
    }
    return info->verifier->VerifyPartial(mapping.start(), length, offset);
  }

  std::map<char, std::unique_ptr<MockBlob>> blob_registry_;
  MockBlobFactory factory_;
  zx::unowned_vmo vmo_;
};

class BlobfsPagerTest : public zxtest::Test {
 public:
  void SetUp() override { pager_ = std::make_unique<MockPager>(); }

  MockBlob* CreateBlob(char identifier = 'z',
                       CompressionAlgorithm algorithm = CompressionAlgorithm::UNCOMPRESSED) {
    return pager_->CreateBlob(identifier, algorithm);
  }

  void ResetPager() { pager_.reset(); }

 private:
  std::unique_ptr<MockPager> pager_;
};

void GetRandomOffsetAndLength(unsigned int* seed, uint64_t* offset, uint64_t* length) {
  *offset = rand_r(seed) % kBlobSize;
  *length = 1 + (rand_r(seed) % (kBlobSize - *offset));
}

struct ReadBlobFnArgs {
  MockBlob* blob;
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
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

TEST_F(BlobfsPagerTest, CreateBlob) { CreateBlob(); }

TEST_F(BlobfsPagerTest, ReadSequential) {
  auto blob = CreateBlob();
  blob->Read(0, kPagedVmoSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, ReadRandom) {
  auto blob = CreateBlob();
  uint64_t offset, length;
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    GetRandomOffsetAndLength(&seed, &offset, &length);
    blob->Read(offset, length);
  }
}

TEST_F(BlobfsPagerTest, ReadSequential_ZstdChunked) {
  auto blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  blob->Read(0, kPagedVmoSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, ReadRandom_ZstdChunked) {
  auto blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  uint64_t offset, length;
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    GetRandomOffsetAndLength(&seed, &offset, &length);
    blob->Read(offset, length);
  }
}

TEST_F(BlobfsPagerTest, CreateMultipleBlobs) {
  CreateBlob('x');
  CreateBlob('y', CompressionAlgorithm::CHUNKED);
  CreateBlob('z');
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobs) {
  MockBlob* blobs[3] = {CreateBlob('x'), CreateBlob('y', CompressionAlgorithm::CHUNKED),
                        CreateBlob('z')};

  uint64_t offset, length;
  unsigned int seed = zxtest::Runner::GetInstance()->random_seed();
  for (uint64_t i = 0; i < kNumReadRequests; i++) {
    uint64_t index = rand() % 3;
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
    args[i].blob = blob;
    args[i].seed = static_cast<unsigned int>(i);
    ASSERT_EQ(thrd_create(&threads[i], ReadBlobFn, &args[i]), thrd_success);
  }

  for (uint64_t i = 0; i < kNumThreads; i++) {
    int res;
    ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultithreaded_ZstdChunked) {
  auto blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  std::array<thrd_t, kNumThreads> threads;
  std::array<ReadBlobFnArgs, kNumThreads> args;

  // All the threads will issue reads on the same blob.
  for (uint64_t i = 0; i < kNumThreads; i++) {
    args[i].blob = blob;
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
  MockBlob* blobs[3] = {CreateBlob('x'), CreateBlob('y', CompressionAlgorithm::CHUNKED),
                        CreateBlob('z')};
  std::array<thrd_t, kNumBlobs> threads;
  std::array<ReadBlobFnArgs, kNumThreads> args;

  // Each thread will issue reads on a different blob.
  for (uint64_t i = 0; i < kNumBlobs; i++) {
    args[i].blob = blobs[i];
    args[i].seed = static_cast<unsigned int>(i);
    ASSERT_EQ(thrd_create(&threads[i], ReadBlobFn, &args[i]), thrd_success);
  }

  for (uint64_t i = 0; i < kNumBlobs; i++) {
    int res;
    ASSERT_EQ(thrd_join(threads[i], &res), thrd_success);
    ASSERT_EQ(res, 0);
  }
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength) {
  auto blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength_ZstdChunked) {
  auto blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
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

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength_ZstdChunked) {
  auto blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, AsyncLoopShutdown) {
  CreateBlob('x');
  CreateBlob('y', CompressionAlgorithm::CHUNKED);
  // Verify that we can exit cleanly if the UserPager (and its member async loop) is destroyed.
  ResetPager();
}

}  // namespace
}  // namespace blobfs
