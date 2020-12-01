// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/paged_vmo.h>
#include <lib/async/dispatcher.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/pager.h>
#include <limits.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <thread>

#include <blobfs/blob-layout.h>
#include <blobfs/compression-settings.h>
#include <blobfs/format.h>
#include <digest/digest.h>
#include <digest/merkle-tree.h>
#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <fbl/auto_call.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/blob-verifier.h"
#include "src/storage/blobfs/compression/blob-compressor.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/pager/page-watcher.h"
#include "src/storage/blobfs/pager/user-pager.h"

namespace blobfs {
namespace pager {
namespace {

// Relatively large blobs are used to exercise paging multi-frame compressed blobs.
constexpr size_t kDefaultPagedVmoSize = 100 * ZX_PAGE_SIZE;
// kDefaultBlobSize is intentionally not page-aligned to exercise edge cases.
constexpr size_t kDefaultBlobSize = kDefaultPagedVmoSize - 42;
constexpr int kNumReadRequests = 100;
constexpr int kNumThreads = 10;

// Like a Blob w.r.t. the pager - creates a VMO linked to the pager and issues reads on it.
class MockBlob {
 public:
  MockBlob(char identifier, zx::vmo vmo, fbl::Array<uint8_t> raw_data, size_t data_size,
           std::unique_ptr<PageWatcher> watcher, std::unique_ptr<uint8_t[]> merkle_tree)
      : identifier_(identifier),
        vmo_(std::move(vmo)),
        data_size_(data_size),
        raw_data_(std::move(raw_data)),
        page_watcher_(std::move(watcher)),
        merkle_tree_(std::move(merkle_tree)) {}

  ~MockBlob() { page_watcher_->DetachPagedVmoSync(); }

  void CommitRange(uint64_t offset, uint64_t length) {
    ASSERT_EQ(vmo_.op_range(ZX_VMO_OP_COMMIT, offset, length, nullptr, 0), ZX_OK);

    zx_info_vmo_t info;
    ZX_ASSERT(vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr) == ZX_OK);
    ASSERT_EQ(info.committed_bytes, fbl::round_up(length, ZX_PAGE_SIZE));
  }

  void Read(uint64_t offset, uint64_t length) {
    fbl::Array<char> buf(new char[length], length);
    ASSERT_EQ(vmo_.read(buf.get(), offset, length), ZX_OK);

    length = std::min(length, data_size_ - offset);
    if (length > 0) {
      fbl::Array<char> comp(new char[length], length);
      memset(comp.get(), identifier_, length);
      // Make sure we got back the expected bytes.
      ASSERT_EQ(memcmp(comp.get(), buf.get(), length), 0);
    }
  }

  const zx::vmo& vmo() const { return vmo_; }

  // Access the data as it would be physically stored on-disk.
  const uint8_t* raw_data() const { return raw_data_.data(); }
  size_t raw_data_size() const { return raw_data_.size(); }

 private:
  char identifier_;
  zx::vmo vmo_;
  size_t data_size_;
  fbl::Array<uint8_t> raw_data_;
  std::unique_ptr<PageWatcher> page_watcher_;
  std::unique_ptr<uint8_t[]> merkle_tree_;
};

class MockBlobFactory {
 public:
  MockBlobFactory(UserPager* pager, BlobfsMetrics* metrics) : pager_(pager), metrics_(metrics) {}

  std::unique_ptr<MockBlob> CreateBlob(char identifier, CompressionAlgorithm algorithm, size_t sz) {
    fbl::Array<uint8_t> data(new uint8_t[sz], sz);
    memset(data.get(), identifier, sz);

    // Generate the merkle tree based on the uncompressed contents (i.e. |data|).
    size_t tree_len;
    Digest root;
    std::unique_ptr<uint8_t[]> merkle_tree;

    if (data_corruption_) {
      fbl::Array<uint8_t> corrupt(new uint8_t[sz], sz);
      memset(corrupt.get(), identifier + 1, sz);
      EXPECT_EQ(
          digest::MerkleTreeCreator::Create(corrupt.get(), sz, &merkle_tree, &tree_len, &root),
          ZX_OK);
    } else {
      EXPECT_EQ(digest::MerkleTreeCreator::Create(data.get(), sz, &merkle_tree, &tree_len, &root),
                ZX_OK);
    }

    // The BlobLayoutFormat only impacts the format of the Merkle tree which is not relevant to
    // these tests.
    std::unique_ptr<BlobVerifier> verifier;
    EXPECT_EQ(
        BlobVerifier::Create(std::move(root), metrics_, merkle_tree.get(), tree_len,
                             BlobLayoutFormat::kPaddedMerkleTreeAtStart, sz, nullptr, &verifier),
        ZX_OK);

    // Generate the contents as they would be stored on disk. (This includes compression if
    // applicable)
    fbl::Array<uint8_t> raw_data = GenerateData(data.get(), sz, algorithm);

    UserPagerInfo pager_info = {
        .identifier = static_cast<uint32_t>(identifier),
        .data_length_bytes = sz,
        .verifier = std::move(verifier),
        .decompressor = CreateDecompressor(raw_data, algorithm),
    };
    auto page_watcher = std::make_unique<PageWatcher>(pager_, std::move(pager_info));

    zx::vmo vmo;
    EXPECT_EQ(page_watcher->CreatePagedVmo(fbl::round_up(sz, ZX_PAGE_SIZE), &vmo), ZX_OK);

    // Make sure the vmo is valid and of the desired size.
    EXPECT_TRUE(vmo.is_valid());
    uint64_t vmo_size;
    EXPECT_EQ(vmo.get_size(&vmo_size), ZX_OK);
    EXPECT_EQ(vmo_size, fbl::round_up(sz, ZX_PAGE_SIZE));

    // Make sure the vmo is pager-backed.
    zx_info_vmo_t info;
    EXPECT_EQ(vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr), ZX_OK);
    EXPECT_NE(info.flags & ZX_INFO_VMO_PAGER_BACKED, 0u);

    return std::make_unique<MockBlob>(identifier, std::move(vmo), std::move(raw_data), sz,
                                      std::move(page_watcher), std::move(merkle_tree));
  }

  void SetDataCorruption(bool val) { data_corruption_ = val; }

 private:
  fbl::Array<uint8_t> GenerateData(const uint8_t* input, size_t len,
                                   CompressionAlgorithm algorithm) {
    if (algorithm == CompressionAlgorithm::UNCOMPRESSED) {
      fbl::Array<uint8_t> out(new uint8_t[len], len);
      memcpy(out.data(), input, len);
      return out;
    }
    CompressionSettings settings{
        .compression_algorithm = algorithm,
    };
    std::optional<BlobCompressor> compressor = BlobCompressor::Create(settings, len);
    EXPECT_TRUE(compressor);
    EXPECT_EQ(compressor->Update(input, len), ZX_OK);
    EXPECT_EQ(compressor->End(), ZX_OK);
    size_t out_size = compressor->Size();
    fbl::Array<uint8_t> out(new uint8_t[out_size], out_size);
    memcpy(out.data(), compressor->Data(), out_size);
    return out;
  }

  std::unique_ptr<SeekableDecompressor> CreateDecompressor(const fbl::Array<uint8_t>& data,
                                                           CompressionAlgorithm algorithm) {
    if (algorithm == CompressionAlgorithm::UNCOMPRESSED) {
      return nullptr;
    } else if (algorithm != CompressionAlgorithm::CHUNKED) {
      // Other compression algorithms do not support paging.
      EXPECT_TRUE(false);
    }
    std::unique_ptr<SeekableDecompressor> decompressor;
    EXPECT_EQ(SeekableChunkedDecompressor::CreateDecompressor(data.get(), data.size(), data.size(),
                                                              &decompressor),
              ZX_OK);
    return decompressor;
  }

  UserPager* pager_ = nullptr;
  BlobfsMetrics* metrics_ = nullptr;
  bool data_corruption_ = false;
};

using BlobRegistry = std::map<char, std::unique_ptr<MockBlob>>;

// Mock transfer buffer. Defines the |TransferBuffer| interface such that the result of reads on
// distinct mock blobs can be verified.
class MockTransferBuffer : public TransferBuffer {
 public:
  static std::unique_ptr<MockTransferBuffer> Create(size_t sz, BlobRegistry* registry) {
    EXPECT_EQ(sz % ZX_PAGE_SIZE, 0ul);
    zx::vmo vmo;
    EXPECT_EQ(zx::vmo::create(sz, 0, &vmo), ZX_OK);
    std::unique_ptr<MockTransferBuffer> buffer(new MockTransferBuffer(std::move(vmo)));
    buffer->blob_registry_ = registry;
    return buffer;
  }

  void SetFailureMode(PagerErrorStatus mode) {
    // Clear possible side effects from a previous failure mode.
    mapping_.Unmap();
    if (mode == PagerErrorStatus::kErrBadState) {
      // A mapped VMO cannot be used for supplying pages, so this will result in failed calls to
      // zx_pager_supply_pages.
      mapping_.Map(vmo_, 0, ZX_PAGE_SIZE, ZX_VM_PERM_READ);
    }
    failure_mode_ = mode;
  }

  void SetDoPartialTransfer(bool do_partial_transfer = true) {
    do_partial_transfer_ = do_partial_transfer;
  }

  // Fakes the Merkle tree being present in the last block of the data to ensure that the pager
  // removes it before verifying the blob.
  void SetDoMerkleTreeAtEndOfData(bool do_merkle_tree_at_end_of_data = true) {
    do_merkle_tree_at_end_of_data_ = do_merkle_tree_at_end_of_data;
  }

  zx::status<> Populate(uint64_t offset, uint64_t length, const UserPagerInfo& info) final {
    if (failure_mode_ == PagerErrorStatus::kErrIO) {
      return zx::error(ZX_ERR_IO_REFUSED);
    }
    // Ensure that no bytes are lingering from previous calls.
    EXPECT_EQ(CommittedBytes(), 0ul);
    char identifier = static_cast<char>(info.identifier);
    EXPECT_NE(blob_registry_->find(identifier), blob_registry_->end());
    const MockBlob& blob = *(blob_registry_->at(identifier));
    EXPECT_EQ(offset % kBlobfsBlockSize, 0ul);
    EXPECT_LE(offset + length, blob.raw_data_size());
    // Fill the transfer buffer with the blob's data, to service page requests.
    if (do_partial_transfer_) {
      // Zero the entire range, and then explicitly fill the first half.
      EXPECT_EQ(vmo_.op_range(ZX_VMO_OP_ZERO, offset, length, nullptr, 0), ZX_OK);
      EXPECT_EQ(vmo_.write(blob.raw_data() + offset, 0, length / 2), ZX_OK);
    } else {
      EXPECT_EQ(vmo_.write(blob.raw_data() + offset, 0, length), ZX_OK);
    }
    if (offset + length == blob.raw_data_size() && do_merkle_tree_at_end_of_data_) {
      constexpr static std::array<uint8_t, 64> mock_merkle_tree = {0xAB};
      uint64_t pos = offset + length;
      uint64_t vmo_size;
      EXPECT_EQ(vmo_.get_size(&vmo_size), ZX_OK);
      while (pos + mock_merkle_tree.size() <= vmo_size) {
        EXPECT_EQ(vmo_.write(mock_merkle_tree.data(), pos, mock_merkle_tree.size()), ZX_OK);
        pos += mock_merkle_tree.size();
      }
      EXPECT_EQ(vmo_.write(mock_merkle_tree.data(), pos, vmo_size - pos), ZX_OK);
    }
    return zx::ok();
  }

  size_t CommittedBytes() const {
    zx_info_vmo_t info;
    EXPECT_EQ(ZX_OK, vmo_.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    return info.committed_bytes;
  }

  const zx::vmo& vmo() const final { return vmo_; }

 private:
  explicit MockTransferBuffer(zx::vmo vmo) : vmo_(std::move(vmo)) {}

  zx::vmo vmo_;
  fzl::VmoMapper mapping_;
  BlobRegistry* blob_registry_;
  bool do_partial_transfer_ = false;
  PagerErrorStatus failure_mode_ = PagerErrorStatus::kOK;
  bool do_merkle_tree_at_end_of_data_ = false;
};

class BlobfsPagerTest : public testing::Test {
 protected:
  void SetUp() override {
    auto buffer = MockTransferBuffer::Create(kTransferBufferSize, &blob_registry_);
    auto compressed_buffer = MockTransferBuffer::Create(kTransferBufferSize, &blob_registry_);
    buffer_ = buffer.get();
    compressed_buffer_ = compressed_buffer.get();
    auto status_or_pager =
        UserPager::Create(std::move(buffer), std::move(compressed_buffer), &metrics_, false);
    ASSERT_TRUE(status_or_pager.is_ok());
    pager_ = std::move(status_or_pager).value();
    factory_ = std::make_unique<MockBlobFactory>(pager_.get(), &metrics_);
  }

  MockBlob* CreateBlob(char identifier = 'z',
                       CompressionAlgorithm algorithm = CompressionAlgorithm::UNCOMPRESSED,
                       size_t sz = kDefaultBlobSize) {
    std::unique_ptr<MockBlob> blob = factory_->CreateBlob(identifier, algorithm, sz);
    EXPECT_EQ(blob_registry_.find(identifier), blob_registry_.end());
    auto blob_ptr = blob.get();
    blob_registry_[identifier] = std::move(blob);
    return blob_ptr;
  }

  void ResetPager() { pager_.reset(); }

  void SetFailureMode(PagerErrorStatus mode) {
    compressed_buffer_->SetFailureMode(mode);
    buffer_->SetFailureMode(mode);
    if (mode == PagerErrorStatus::kErrDataIntegrity) {
      factory_->SetDataCorruption(true);
    } else {
      factory_->SetDataCorruption(false);
    }
  }

  BlobfsMetrics metrics_{false};
  std::unique_ptr<UserPager> pager_;
  BlobRegistry blob_registry_ = {};
  // Owned by |pager_|.
  MockTransferBuffer* buffer_ = nullptr;
  MockTransferBuffer* compressed_buffer_ = nullptr;
  std::unique_ptr<MockBlobFactory> factory_;
};

class RandomBlobReader {
 public:
  using Seed = std::default_random_engine::result_type;
  RandomBlobReader() : random_engine_(testing::UnitTest::GetInstance()->random_seed()) {}
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
    uint64_t offset = std::uniform_int_distribution<uint64_t>(0, kDefaultBlobSize)(random_engine_);
    return std::make_pair(offset, std::uniform_int_distribution<uint64_t>(
                                      0, kDefaultBlobSize - offset)(random_engine_));
  }

  std::default_random_engine random_engine_;
};

TEST_F(BlobfsPagerTest, CreateBlob) { CreateBlob(); }

TEST_F(BlobfsPagerTest, ReadSequential) {
  MockBlob* blob = CreateBlob();
  blob->Read(0, kDefaultBlobSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kDefaultBlobSize);
}

TEST_F(BlobfsPagerTest, ReadSequential_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  blob->Read(0, kDefaultPagedVmoSize);
  // Issue a repeated read on the same range.
  blob->Read(0, kDefaultPagedVmoSize);
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
  std::default_random_engine random_engine(testing::UnitTest::GetInstance()->random_seed());
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
        std::thread(RandomBlobReader(testing::UnitTest::GetInstance()->random_seed() + i), blob);
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
        std::thread(RandomBlobReader(testing::UnitTest::GetInstance()->random_seed() + i), blob);
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
    threads[i] = std::thread(RandomBlobReader(testing::UnitTest::GetInstance()->random_seed() + i),
                             blobs[i]);
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
  blob->CommitRange(0, kDefaultBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_ExactLength_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kDefaultBlobSize);
}

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength) {
  MockBlob* blob = CreateBlob();
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kDefaultPagedVmoSize);
}

TEST_F(BlobfsPagerTest, CommitRange_PageRoundedLength_ZstdChunked) {
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::CHUNKED);
  // Attempt to commit the entire blob. The zx_vmo_op_range(ZX_VMO_OP_COMMIT) call will return
  // successfully iff the entire range was mapped by the pager; it will hang if the pager only maps
  // in a subset of the range.
  blob->CommitRange(0, kDefaultPagedVmoSize);
}

TEST_F(BlobfsPagerTest, AsyncLoopShutdown) {
  CreateBlob('x');
  CreateBlob('y', CompressionAlgorithm::CHUNKED);
  // Verify that we can exit cleanly if the UserPager (and its member async loop) is destroyed.
  ResetPager();
}

void AssertNoLeaksInVmo(const zx::vmo& vmo, char leak_char) {
  char scratch[ZX_PAGE_SIZE] = {0};
  size_t vmo_size;
  ASSERT_EQ(vmo.get_size(&vmo_size), ZX_OK);
  for (size_t offset = 0; offset < vmo_size; offset += sizeof(scratch)) {
    ASSERT_EQ(vmo.read(scratch, offset, sizeof(scratch)), ZX_OK);
    for (unsigned i = 0; i < sizeof(scratch); ++i) {
      ASSERT_NE(scratch[i], leak_char);
    }
  }
}

TEST_F(BlobfsPagerTest, NoDataLeaked_Uncompressed) {
  // For each other algorithm supported, induce a fault in |first_blob| so the internal transfer
  // buffers contain its contents, and then fault in a second VMO. Verify no data from the first
  // blob is leaked in the padding.
  // Since we do not support page eviction, we need to create a new |first_blob| for each test
  // case.
  {
    MockBlob* first_blob = CreateBlob('x', CompressionAlgorithm::UNCOMPRESSED, 4096);
    MockBlob* new_blob = CreateBlob('a', CompressionAlgorithm::UNCOMPRESSED, 1);
    first_blob->CommitRange(0, 4096);
    new_blob->CommitRange(0, 1);
    AssertNoLeaksInVmo(new_blob->vmo(), 'x');
  }
  {
    MockBlob* first_blob = CreateBlob('y', CompressionAlgorithm::UNCOMPRESSED, 4096);
    MockBlob* new_blob = CreateBlob('b', CompressionAlgorithm::CHUNKED, 1);
    first_blob->CommitRange(0, 4096);
    new_blob->CommitRange(0, 1);
    AssertNoLeaksInVmo(new_blob->vmo(), 'y');
  }
}

TEST_F(BlobfsPagerTest, NoDataLeaked_ZstdChunked) {
  // For each other algorithm supported, induce a fault in |first_blob| so the internal transfer
  // buffers contain its contents, and then fault in a second VMO. Verify no data from the first
  // blob is leaked in the padding.
  // Since we do not support page eviction, we need to create a new |first_blob| for each test
  // case.
  {
    MockBlob* first_blob = CreateBlob('x', CompressionAlgorithm::CHUNKED, 4096);
    MockBlob* new_blob = CreateBlob('a', CompressionAlgorithm::UNCOMPRESSED, 1);
    first_blob->CommitRange(0, 4096);
    new_blob->CommitRange(0, 1);
    AssertNoLeaksInVmo(new_blob->vmo(), 'x');
  }
  {
    MockBlob* first_blob = CreateBlob('y', CompressionAlgorithm::CHUNKED, 4096);
    MockBlob* new_blob = CreateBlob('b', CompressionAlgorithm::CHUNKED, 1);
    first_blob->CommitRange(0, 4096);
    new_blob->CommitRange(0, 1);
    AssertNoLeaksInVmo(new_blob->vmo(), 'y');
  }
}

TEST_F(BlobfsPagerTest, PartiallyCommittedBuffer) {
  // The blob contents must be zero, since we want verification to pass but we also want the
  // data to only be half filled (the other half defaults to zero because it is decommitted.)
  MockBlob* blob = CreateBlob('\0', CompressionAlgorithm::UNCOMPRESSED);
  buffer_->SetDoPartialTransfer();
  blob->CommitRange(0, kDefaultPagedVmoSize);
}

TEST_F(BlobfsPagerTest, PagerErrorCode_Uncompressed) {
  fbl::Array<uint8_t> buf(new uint8_t[ZX_PAGE_SIZE], ZX_PAGE_SIZE);

  // No failure by default.
  MockBlob* blob = CreateBlob('a');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);

  // Failure while populating pages.
  SetFailureMode(PagerErrorStatus::kErrIO);
  blob = CreateBlob('b');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO);
  SetFailureMode(PagerErrorStatus::kOK);

  // Failure while verifying pages.
  SetFailureMode(PagerErrorStatus::kErrDataIntegrity);
  blob = CreateBlob('c');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO_DATA_INTEGRITY);
  SetFailureMode(PagerErrorStatus::kOK);

  // No failure while populating or verifying. Applies to any other type of failure - simulated here
  // by leaving the transfer buffer mapped before the call to supply_pages() is made.
  SetFailureMode(PagerErrorStatus::kErrBadState);
  blob = CreateBlob('d');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_BAD_STATE);
  SetFailureMode(PagerErrorStatus::kOK);

  // Failure mode has been cleared. No further failures expected.
  blob = CreateBlob('e');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);
}

TEST_F(BlobfsPagerTest, PagerErrorCode_ZstdChunked) {
  fbl::Array<uint8_t> buf(new uint8_t[ZX_PAGE_SIZE], ZX_PAGE_SIZE);

  // No failure by default.
  MockBlob* blob = CreateBlob('a', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);

  // Failure while populating pages.
  SetFailureMode(PagerErrorStatus::kErrIO);
  blob = CreateBlob('b', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO);
  SetFailureMode(PagerErrorStatus::kOK);

  // Failure while verifying pages.
  SetFailureMode(PagerErrorStatus::kErrDataIntegrity);
  blob = CreateBlob('c', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO_DATA_INTEGRITY);
  SetFailureMode(PagerErrorStatus::kOK);

  // Failure mode has been cleared. No further failures expected.
  blob = CreateBlob('e', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);
}

TEST_F(BlobfsPagerTest, FailAfterPagerError_Uncompressed) {
  fbl::Array<uint8_t> buf(new uint8_t[ZX_PAGE_SIZE], ZX_PAGE_SIZE);

  // Failure while populating pages.
  SetFailureMode(PagerErrorStatus::kErrIO);
  MockBlob* blob = CreateBlob('a');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO);
  SetFailureMode(PagerErrorStatus::kOK);

  // This should succeed now as the failure mode has been cleared. An IO error is not fatal.
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);

  // Failure while verifying pages.
  SetFailureMode(PagerErrorStatus::kErrDataIntegrity);
  blob = CreateBlob('b');
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO_DATA_INTEGRITY);
  SetFailureMode(PagerErrorStatus::kOK);

  // A verification error is fatal. Further requests should fail as well.
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsPagerTest, FailAfterPagerError_ZstdChunked) {
  fbl::Array<uint8_t> buf(new uint8_t[ZX_PAGE_SIZE], ZX_PAGE_SIZE);

  // Failure while populating pages.
  SetFailureMode(PagerErrorStatus::kErrIO);
  MockBlob* blob = CreateBlob('a', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO);
  SetFailureMode(PagerErrorStatus::kOK);

  // This should succeed now as the failure mode has been cleared. An IO error is not fatal.
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_OK);

  // Failure while verifying pages.
  SetFailureMode(PagerErrorStatus::kErrDataIntegrity);
  blob = CreateBlob('b', CompressionAlgorithm::CHUNKED);
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_IO_DATA_INTEGRITY);
  SetFailureMode(PagerErrorStatus::kOK);

  // A verification error is fatal. Further requests should fail as well.
  ASSERT_EQ(blob->vmo().read(buf.get(), 0, ZX_PAGE_SIZE), ZX_ERR_BAD_STATE);
}

TEST_F(BlobfsPagerTest, ReadWithMerkleTreeSharingTheLastBlockWithData) {
  // The blob size should not be a multiple of the page size.
  uint64_t blob_size = 24480;
  ASSERT_NE(blob_size % ZX_PAGE_SIZE, 0ul);
  MockBlob* blob = CreateBlob('x', CompressionAlgorithm::UNCOMPRESSED, blob_size);
  // The blob verifier checks that the end of the blob is zeroed.  The pager needs to remove the
  // Merkle tree from the last block of the data before trying to verify the blob or verification
  // will fail.
  buffer_->SetDoMerkleTreeAtEndOfData();
  blob->Read(0, blob_size);
}

}  // namespace
}  // namespace pager
}  // namespace blobfs
