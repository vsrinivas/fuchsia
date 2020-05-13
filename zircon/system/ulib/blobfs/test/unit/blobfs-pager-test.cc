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
constexpr size_t kPagedVmoSize = 100 * ZX_PAGE_SIZE;
// kBlobSize is intentionally not page-aligned to exercise edge cases.
constexpr size_t kBlobSize = kPagedVmoSize - 42;
constexpr int kNumReadRequests = 100;
constexpr int kNumThreads = 10;

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
  MockPager() : factory_(this) { ASSERT_OK(InitPager()); }

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
    // Fill the transfer buffer with the blob's data, to service page requests.
    EXPECT_OK(vmo_->write(blob.raw_data() + offset, 0, length));
    length = fbl::min(length, kBlobSize - offset);
    return ZX_OK;
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

TEST_F(BlobfsPagerTest, CreateBlob) { CreateBlob(); }

TEST_F(BlobfsPagerTest, ReadSequential) {
  MockBlob* blob = CreateBlob();
  blob->Read(0, kBlobSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, ReadSequential_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  blob->Read(0, kPagedVmoSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, ReadRandom) {
  MockBlob* blob = CreateBlob();
  RandomBlobReader reader;
  reader(blob);
}

TEST_F(BlobfsPagerTest, ReadRandom_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  RandomBlobReader reader;
  reader(blob);
}

TEST_F(BlobfsPagerTest, CreateMultipleBlobs) {
  CreateBlob('x');
  CreateBlob('y', CompressionAlgorithm::CHUNKED);
  CreateBlob('z');
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobs) {
  constexpr int kBlobCount = 3;
  MockBlob* blobs[3] = {CreateBlob('x'), CreateBlob('y', CompressionAlgorithm::CHUNKED),
                        CreateBlob('z')};
  RandomBlobReader reader;
  std::default_random_engine random_engine(zxtest::Runner::GetInstance()->random_seed());
  std::uniform_int_distribution distribution(0, kBlobCount - 1);
  for (int i = 0; i < kNumReadRequests; i++) {
    reader.ReadOnce(blobs[distribution(random_engine)]);
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultithreaded) {
  MockBlob* blob = CreateBlob();
  std::array<std::thread, kNumThreads> threads;

  // All the threads will issue reads on the same blob.
  for (int i = 0; i < kNumThreads; i++) {
    threads[i] =
        std::thread(RandomBlobReader(zxtest::Runner::GetInstance()->random_seed() + i), blob);
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultithreaded_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  std::array<std::thread, kNumThreads> threads;

  // All the threads will issue reads on the same blob.
  for (int i = 0; i < kNumThreads; i++) {
    threads[i] =
        std::thread(RandomBlobReader(zxtest::Runner::GetInstance()->random_seed() + i), blob);
  }
  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(BlobfsPagerTest, ReadRandomMultipleBlobsMultithreaded) {
  constexpr int kNumBlobs = 3;
  MockBlob* blobs[kNumBlobs] = {CreateBlob('x'), CreateBlob('y', CompressionAlgorithm::CHUNKED),
                                CreateBlob('z')};
  std::array<std::thread, kNumBlobs> threads;

  // Each thread will issue reads on a different blob.
  for (int i = 0; i < kNumBlobs; i++) {
    threads[i] =
        std::thread(RandomBlobReader(zxtest::Runner::GetInstance()->random_seed() + i), blobs[i]);
  }

  for (auto& thread : threads) {
    thread.join();
  }
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength) {
  MockBlob* blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength) {
  MockBlob* blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kPagedVmoSize);
}

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
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
