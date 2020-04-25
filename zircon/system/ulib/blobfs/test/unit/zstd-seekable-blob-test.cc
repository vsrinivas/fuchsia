// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression/zstd-seekable-blob.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/resource.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <string.h>
#include <zircon/device/block.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <iostream>
#include <iomanip>
#include <ios>
#include <limits>
#include <memory>

#include <blobfs/common.h>
#include <blobfs/mkfs.h>
#include <block-client/cpp/fake-device.h>
#include <zxtest/base/test.h>
#include <zxtest/zxtest.h>
#include <stdlib.h>

#include "allocator/allocator.h"
#include "blob.h"
#include "blobfs.h"
#include "compression/algorithm.h"
#include "compression/zstd-seekable-blob-collection.h"
#include "compression/log-zstd-read.h"
#include "test/blob_utils.h"
#include "transaction-manager.h"
#include "iterator/block-iterator-provider.h"
#include "metrics.h"

namespace blobfs {
namespace {

using blobfs::BlobInfo;
using blobfs::GenerateBlob;

const uint32_t kNumFilesystemBlocks = 4000;

constexpr unsigned kZeroToThirtyTwoAndRandomBlobSrcFunctionRandomSeed = 9572331;

void ZeroToThirtyTwoAndRandomBlobSrcFunction(char* data, size_t length) {
  srand(kZeroToThirtyTwoAndRandomBlobSrcFunctionRandomSeed);
  for (size_t i = 0; i < length; i++) {
    if ((i / 32) % 2 == 0) {
      uint8_t value = static_cast<uint8_t>(i % 32);
      data[i] = value;
    } else {
      data[i] = (char)rand();
    }
  }
}

namespace {

using std::cerr;
using std::endl;
using std::dec;
using std::hex;
using std::setfill;
using std::setw;
using std::right;

std::string gDeviceOwner = "NONE";

void SetDeviceOwner(std::string name) { gDeviceOwner = name; }


constexpr size_t gLoggingBytesPerLine = 64;

void LogBuf(std::string name, const std::vector<uint8_t>& buf) {
  cerr << "BUF(" << name << ") :: " << buf.size();


  for (size_t i = 0; i < buf.size(); i++) {
    if ((i % gLoggingBytesPerLine) == 0) {
      cerr << endl << "BUF(" << name << ") ";
      fprintf(stderr, "%10lu", i);
      cerr << " >> ";
    }
    fprintf(stderr, "%02X", buf[i]);
  }
  cerr << endl;
}

class LoggingBlockDevice : public block_client::BlockDevice {
 public:
  LoggingBlockDevice() = delete;
  explicit LoggingBlockDevice(std::unique_ptr<block_client::BlockDevice> bd) : bd_(std::move(bd)) {}

  //
  // BlockDevice implementation.
  //
  zx_status_t ReadBlock(uint64_t block_num, uint64_t block_size, void* block) const final {
    cerr << "BLOCK_DEVICE(" << gDeviceOwner << ") :: ReadBlock(block_num=" << block_num
         << ", block_size=" << block_size << ")" << endl;
    return bd_->ReadBlock(block_num, block_size, block);
  }

  zx_status_t FifoTransaction(block_fifo_request_t* requests, size_t count) final {
    cerr << "BLOCK_DEVICE(" << gDeviceOwner << ") :: ReadBlock(count=" << count << ")" << endl;
    for (size_t i = 0; i < count; i++) {
      const block_fifo_request_t& req = requests[i];
      cerr << "BLOCK_DEVICE(" << gDeviceOwner << ") :: ReadBlock >> {opcode=" << req.opcode
           << ", length=" << req.length << ", vmo_offset=" << req.vmo_offset << ", dev_offset="
           << req.dev_offset << "}" << endl;
    }
    return bd_->FifoTransaction(requests, count);
  }

  zx_status_t GetDevicePath(size_t buffer_len, char* out_name, size_t* out_len) const final {
    return bd_->GetDevicePath(buffer_len, out_name, out_len);
  }

  zx_status_t BlockGetInfo(fuchsia_hardware_block_BlockInfo* out_info) const final {
    return bd_->BlockGetInfo(out_info);
  }

  zx_status_t VolumeQuery(fuchsia_hardware_block_volume_VolumeInfo* out_info) const final {
    return bd_->VolumeQuery(out_info);
  }

  zx_status_t VolumeQuerySlices(const uint64_t* slices, size_t slices_count,
                                        fuchsia_hardware_block_volume_VsliceRange* out_ranges,
                                        size_t* out_ranges_count) const final {
    return bd_->VolumeQuerySlices(slices, slices_count, out_ranges, out_ranges_count);
  }

  zx_status_t VolumeExtend(uint64_t offset, uint64_t length) final {
    return bd_->VolumeExtend(offset, length);
  }
  zx_status_t VolumeShrink(uint64_t offset, uint64_t length) final {
    return bd_->VolumeShrink(offset, length);
  }

  //
  // storage::VmoidRegistry implementation.
  //
  zx_status_t BlockAttachVmo(const zx::vmo& vmo, storage::Vmoid* out) final {
    return bd_->BlockAttachVmo(vmo, out);
  }

  zx_status_t BlockDetachVmo(storage::Vmoid vmoid) final {
    return bd_->BlockDetachVmo(std::move(vmoid));
  }

 private:
  std::unique_ptr<block_client::BlockDevice> bd_;
};

}  // namespace

class ZSTDSeekableBlobTest : public zxtest::Test {
 public:
  static constexpr uint64_t kUncompressedBlobSize = 697048;

  void SetUp() {
    MountOptions options;
    auto device = std::make_unique<LoggingBlockDevice>(
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize));
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    ASSERT_OK(Blobfs::CreateWithWriteCompressionAlgorithm(
        loop_.dispatcher(), std::move(device), &options, CompressionAlgorithm::ZSTD_SEEKABLE,
        zx::resource(), &fs_));
    ASSERT_OK(ZSTDSeekableBlobCollection::Create(vmoid_registry(), space_manager(),
                                                 transaction_handler(), node_finder(),
                                                 &compressed_blob_collection_));
  }

  void AddCompressedBlobAndSync(std::unique_ptr<BlobInfo>* out_info) {
    AddCompressedBlob(out_info);
    ASSERT_OK(Sync());
  }

  void CheckRead(uint32_t node_index, std::vector<uint8_t>* buf, std::vector<uint8_t>* expected_buf,
                 uint64_t data_byte_offset, uint64_t num_bytes) {
    uint8_t* expected = expected_buf->data() + data_byte_offset;
    ASSERT_OK(compressed_blob_collection()->Read(node_index, buf->data(), data_byte_offset,
                                                 num_bytes));
    ASSERT_BYTES_EQ(expected, buf->data(), num_bytes);
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

  void AddCompressedBlob(std::unique_ptr<BlobInfo>* out_info) {
    fbl::RefPtr<fs::Vnode> root;
    ASSERT_OK(fs_->OpenRootNode(&root));
    fs::Vnode* root_node = root.get();

    std::unique_ptr<BlobInfo> info;
    GenerateBlob(ZeroToThirtyTwoAndRandomBlobSrcFunction, "", kUncompressedBlobSize, &info);
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

  BlockIteratorProvider* block_iter_provider() { return fs_.get(); }
  TransactionManager* txn_manager() { return fs_.get(); }
  BlobfsMetrics* metrics() { return fs_->Metrics(); }
  SpaceManager* space_manager() { return fs_.get(); }
  virtual NodeFinder* node_finder() { return fs_->GetNodeFinder(); }
  fs::LegacyTransactionHandler* transaction_handler() { return fs_.get(); }
  storage::VmoidRegistry* vmoid_registry() { return fs_.get(); }
  ZSTDSeekableBlobCollection* compressed_blob_collection() {
    return compressed_blob_collection_.get();
  }

  std::unique_ptr<Blobfs> fs_;
  std::unique_ptr<ZSTDSeekableBlobCollection> compressed_blob_collection_;
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

class ZSTDSeekableBlobWrongAlgorithmTest : public ZSTDSeekableBlobTest {
 public:
  void SetUp() {
    MountOptions options;
    auto device =
        std::make_unique<block_client::FakeBlockDevice>(kNumFilesystemBlocks, kBlobfsBlockSize);
    ASSERT_OK(FormatFilesystem(device.get()));
    loop_.StartThread();

    // Construct BlobFS with non-seekable ZSTD algorithm. This should cause errors in the seekable
    // read path.
    ASSERT_OK(Blobfs::CreateWithWriteCompressionAlgorithm(loop_.dispatcher(), std::move(device),
                                                          &options, CompressionAlgorithm::ZSTD,
                                                          zx::resource(), &fs_));

    ASSERT_OK(ZSTDSeekableBlobCollection::Create(vmoid_registry(), space_manager(),
                                                 transaction_handler(), node_finder(),
                                                 &compressed_blob_collection_));
  }
};

class NullNodeFinder : public NodeFinder {
 public:
  Inode* GetNode(uint32_t node_index) final { return nullptr; }
};

class ZSTDSeekableBlobNullNodeFinderTest : public ZSTDSeekableBlobTest {
 protected:
  NodeFinder* node_finder() final { return &node_finder_; }

  NullNodeFinder node_finder_;
};

TEST_F(ZSTDSeekableBlobTest, CompleteRead) {
  std::unique_ptr<BlobInfo> blob_info;
  AddCompressedBlobAndSync(&blob_info);
  uint32_t node_index = LookupInode(*blob_info);

  // EnableZSTDReadLogging();

  // Read whole blob all at once via |BlobLoader|.
  SetDeviceOwner("ALL");
  {
    fzl::OwnedVmoMapper data_mapper;
    fzl::OwnedVmoMapper merkle_mapper;
    BlobLoader loader(txn_manager(), block_iter_provider(), node_finder(), nullptr, metrics());
    loader.LoadBlob(node_index, &data_mapper, &merkle_mapper);
  }

  // Read whole blob at once via |blob_info->size_data|-sized read from
  // |ZSTDSeekableBlobCollection|.
  SetDeviceOwner("RAC");
  {
    std::vector<uint8_t> buf(blob_info->size_data);
    std::vector<uint8_t> expected(blob_info->size_data);
    ZeroToThirtyTwoAndRandomBlobSrcFunction(reinterpret_cast<char*>(expected.data()), blob_info->size_data);
    ASSERT_OK(compressed_blob_collection()->Read(node_index, buf.data(), 0, blob_info->size_data));

    LogBuf("EXPECTED", expected);
    LogBuf("WAS_READ", buf);

    const size_t incrementSize = kBlobfsBlockSize / 4;
    for (size_t i = 0; i < blob_info->size_data; i += incrementSize) {
      EXPECT_BYTES_EQ(&(expected.data()[i]), &(buf.data()[i]), std::min(i + incrementSize, blob_info->size_data) - i);
    }
    // Note: Data size is too large for stack allocation in |ASSERT_BYTES_EQ()|. That's why
    // |memcmp()| is used instead.
    // ASSERT_EQ(0, memcmp(expected.data(), buf.data(), blob_info->size_data));
  }

  // DisableZSTDReadLogging();
}

// TEST_F(ZSTDSeekableBlobTest, PartialRead) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);
//   std::vector<uint8_t> buf(blob_info->size_data);

//   // Load whole blob contents (because it's less error-prone). Only some will be used for
//   // verification.
//   std::vector<uint8_t> expected_buf(blob_info->size_data);
//   ZeroToThirtyTwoAndRandomBlobSrcFunction(reinterpret_cast<char*>(expected_buf.data()), blob_info->size_data);

//   // Use some small primes to choose "near the end, but not at the end" read of a prime number of
//   // bytes.
//   uint64_t data_byte_offset = blob_info->size_data - 29;
//   uint64_t num_bytes = 19;

//   CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
// }

// TEST_F(ZSTDSeekableBlobTest, MultipleReads) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);
//   std::vector<uint8_t> buf(blob_info->size_data);

//   // Load whole blob contents (because it's less error-prone). Only some will be used for
//   // verification.
//   std::vector<uint8_t> expected_buf(blob_info->size_data);
//   ZeroToThirtyTwoAndRandomBlobSrcFunction(reinterpret_cast<char*>(expected_buf.data()), blob_info->size_data);

//   // Use some small primes to choose "near the end, but not at the end" read of a prime number of
//   // bytes.
//   {
//     uint64_t data_byte_offset = blob_info->size_data - 29;
//     uint64_t num_bytes = 19;

//     CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
//   }
//   {
//     uint64_t data_byte_offset = blob_info->size_data - 89;
//     uint64_t num_bytes = 61;

//     CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
//   }
//   {
//     uint64_t data_byte_offset = blob_info->size_data - 53;
//     uint64_t num_bytes = 37;

//     CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
//   }
// }

// TEST_F(ZSTDSeekableBlobTest, LeftoverRead) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);
//   std::vector<uint8_t> buf(blob_info->size_data);

//   // Load whole blob contents (because it's less error-prone). Only some will be used for
//   // verification.
//   std::vector<uint8_t> expected_buf(blob_info->size_data);
//   ZeroToThirtyTwoAndRandomBlobSrcFunction(reinterpret_cast<char*>(expected_buf.data()), blob_info->size_data);

//   static_assert(ZSTDSeekableBlobTest::kUncompressedBlobSize % kBlobfsBlockSize != 0);
//   uint64_t data_byte_offset = fbl::round_down(blob_info->size_data, kBlobfsBlockSize);
//   uint64_t num_bytes = blob_info->size_data - data_byte_offset;
//   CheckRead(node_index, &buf, &expected_buf, data_byte_offset, num_bytes);
// }

// TEST_F(ZSTDSeekableBlobTest, BadOffset) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);

//   // Attempt to read one byte passed the end of the blob.
//   std::vector<uint8_t> buf(1);
//   ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
//             compressed_blob_collection()->Read(node_index, buf.data(), blob_info->size_data, 1));
// }

// TEST_F(ZSTDSeekableBlobTest, BadSize) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);

//   // Attempt to read two bytes: the last byte in the blob, and one byte passed the end.
//   std::vector<uint8_t> buf(2);
//   ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, compressed_blob_collection()->Read(
//                                           node_index, buf.data(), blob_info->size_data - 1, 2));
// }

// TEST_F(ZSTDSeekableBlobNullNodeFinderTest, BadNode) {
//   std::vector<uint8_t> buf(1);

//   // Attempt to read a byte from a node that doesn't exist.
//   ASSERT_EQ(ZX_ERR_INVALID_ARGS, compressed_blob_collection()->Read(42, buf.data(), 0, 1));
// }

// TEST_F(ZSTDSeekableBlobWrongAlgorithmTest, BadFlags) {
//   std::unique_ptr<BlobInfo> blob_info;
//   AddCompressedBlobAndSync(&blob_info);
//   uint32_t node_index = LookupInode(*blob_info);
//   std::vector<uint8_t> buf(1);

//   // Attempt to read a byte from a blob that is not zstd-seekable.
//   ASSERT_EQ(ZX_ERR_NOT_SUPPORTED, compressed_blob_collection()->Read(node_index, buf.data(), 0, 1));
// }

}  // namespace
}  // namespace blobfs
