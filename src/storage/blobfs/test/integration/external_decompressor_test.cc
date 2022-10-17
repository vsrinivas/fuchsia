// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/external_decompressor.h"

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdlib>

#include <gtest/gtest.h>

#include "src/storage/blobfs/blob_layout.h"
#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression_settings.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

// These settings currently achieve about 60% compression.
constexpr int kCompressionLevel = 5;
constexpr double kDataRandomnessRatio = 0.25;

constexpr size_t kDataSize{static_cast<size_t>(500) * 1024};  // 500KiB
constexpr size_t kMapSize{kDataSize * static_cast<size_t>(2)};

// Generates a data set of size with sequences of the same bytes and random
// values appearing with frequency kDataRandomnessRatio.
void GenerateData(size_t size, uint8_t* dst) {
  srand(testing::UnitTest::GetInstance()->random_seed());
  for (size_t i = 0; i < size; i++) {
    if ((rand() % 1000) / 1000.0l >= kDataRandomnessRatio) {
      dst[i] = 12;
    } else {
      dst[i] = static_cast<uint8_t>(rand() % 256);
    }
  }
}

void CompressData(std::unique_ptr<Compressor> compressor, void* input_data, size_t* size) {
  ASSERT_EQ(ZX_OK, compressor->Update(input_data, kDataSize));
  ASSERT_EQ(ZX_OK, compressor->End());
  *size = compressor->Size();
}

TEST(ExternalDecompressorSetUpTest, DecompressedVmoMissingWrite) {
  zx::vmo compressed_vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &compressed_vmo));
  zx::vmo decompressed_vmo;
  ASSERT_EQ(ZX_OK,
            compressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_WRITE), &decompressed_vmo));

  DecompressorCreatorConnector& connector = DecompressorCreatorConnector::DefaultServiceConnector();
  zx::result<std::unique_ptr<ExternalDecompressorClient>> client_or =
      ExternalDecompressorClient::Create(&connector, decompressed_vmo, compressed_vmo);
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, client_or.status_value());
}

TEST(ExternalDecompressorSetUpTest, CompressedVmoMissingDuplicate) {
  zx::vmo decompressed_vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &decompressed_vmo));
  zx::vmo compressed_vmo;
  ASSERT_EQ(ZX_OK, decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_DUPLICATE),
                                              &compressed_vmo));

  DecompressorCreatorConnector& connector = DecompressorCreatorConnector::DefaultServiceConnector();
  zx::result<std::unique_ptr<ExternalDecompressorClient>> client_or =
      ExternalDecompressorClient::Create(&connector, decompressed_vmo, compressed_vmo);
  ASSERT_EQ(ZX_ERR_ACCESS_DENIED, client_or.status_value());
}

class ExternalDecompressorTest : public ::testing::Test {
 public:
  void SetUp() override {
    GenerateData(kDataSize, input_data_);

    zx::vmo compressed_vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &compressed_vmo));
    zx::vmo remote_compressed_vmo;
    ASSERT_EQ(ZX_OK, compressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_WRITE),
                                              &remote_compressed_vmo));
    ASSERT_EQ(ZX_OK, compressed_mapper_.Map(std::move(compressed_vmo), kMapSize));

    zx::vmo decompressed_vmo;
    ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &decompressed_vmo));
    zx::vmo remote_decompressed_vmo;
    ASSERT_EQ(ZX_OK, decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS, &remote_decompressed_vmo));
    ASSERT_EQ(ZX_OK, decompressed_mapper_.Map(std::move(decompressed_vmo), kMapSize));

    DecompressorCreatorConnector& connector =
        DecompressorCreatorConnector::DefaultServiceConnector();
    zx::result<std::unique_ptr<ExternalDecompressorClient>> client_or =
        ExternalDecompressorClient::Create(&connector, remote_decompressed_vmo,
                                           remote_compressed_vmo);
    ASSERT_EQ(ZX_OK, client_or.status_value());
    client_ = std::move(client_or.value());
  }

 protected:
  uint8_t input_data_[kDataSize];
  fzl::OwnedVmoMapper compressed_mapper_;
  fzl::OwnedVmoMapper decompressed_mapper_;
  std::unique_ptr<ExternalDecompressorClient> client_;
};

// Get a full range mapping for a SeekableDecompressor.
zx::result<std::vector<CompressionMapping>> GetMappings(SeekableDecompressor* decompressor,
                                                        size_t length) {
  std::vector<CompressionMapping> mappings;
  size_t current = 0;
  while (current < length) {
    zx::result<CompressionMapping> mapping_or =
        decompressor->MappingForDecompressedRange(current, 1, std::numeric_limits<size_t>::max());
    if (!mapping_or.is_ok()) {
      return mapping_or.take_error();
    }
    current += mapping_or.value().decompressed_length;
    mappings.push_back(mapping_or.value());
  }
  return zx::ok(std::move(mappings));
}

// Simple success case for chunked decompression, but done on each chunk just
// to verify success.
TEST_F(ExternalDecompressorTest, ChunkedPartialDecompression) {
  size_t compressed_size;
  std::unique_ptr<ChunkedCompressor> compressor = nullptr;
  ASSERT_EQ(ZX_OK, ChunkedCompressor::Create({CompressionAlgorithm::kChunked, kCompressionLevel},
                                             kDataSize, &compressed_size, &compressor));
  ASSERT_EQ(ZX_OK, compressor->SetOutput(compressed_mapper_.start(), kMapSize));
  CompressData(std::move(compressor), input_data_, &compressed_size);

  std::unique_ptr<SeekableDecompressor> local_decompressor;
  ASSERT_EQ(ZX_OK, SeekableChunkedDecompressor::CreateDecompressor(
                       cpp20::span(static_cast<const uint8_t*>(compressed_mapper_.start()),
                                   compressed_size),
                       compressed_size, &local_decompressor));

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor->algorithm());

  auto mappings_or = GetMappings(local_decompressor.get(), kDataSize);
  ASSERT_TRUE(mappings_or.is_ok());
  std::vector<CompressionMapping> mappings = mappings_or.value();
  // Ensure that we're testing multiple chunks and not one large chunk.
  ASSERT_GT(mappings.size(), 1ul);
  for (CompressionMapping mapping : mappings) {
    ASSERT_EQ(ZX_OK,
              decompressor.DecompressRange(mapping.compressed_offset, mapping.compressed_length,
                                           mapping.decompressed_length));
    ASSERT_EQ(0, memcmp(static_cast<uint8_t*>(input_data_) + mapping.decompressed_offset,
                        decompressed_mapper_.start(), mapping.decompressed_length));
  }
}

class ExternalDecompressorE2ePagedTest : public FdioTest {
 public:
  ExternalDecompressorE2ePagedTest() {
    MountOptions options;
    // Chunked files will be paged in.
    options.pager_backed_cache_policy = CachePolicy::EvictImmediately;
    options.compression_settings = {CompressionAlgorithm::kChunked, 14};
    options.sandbox_decompression = true;
    set_mount_options(options);
  }
};

TEST_F(ExternalDecompressorE2ePagedTest, VerifyRemoteDecompression) {
  // Create a new blob on the mounted filesystem.
  std::unique_ptr<BlobInfo> info = GenerateRealisticBlob(".", kDataSize);
  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  uint64_t before_decompressions;
  ASSERT_NO_FATAL_FAILURE(
      GetUintMetric({"paged_read_stats"}, "remote_decompressions", &before_decompressions));

  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  }

  uint64_t after_decompressions;
  ASSERT_NO_FATAL_FAILURE(
      GetUintMetric({"paged_read_stats"}, "remote_decompressions", &after_decompressions));
  ASSERT_GT(after_decompressions, before_decompressions);
}

TEST_F(ExternalDecompressorE2ePagedTest, MultiframeDecompression) {
  std::unique_ptr<BlobInfo> info = GenerateRealisticBlob(".", kDataSize);
  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  uint64_t decompressions;
  ASSERT_NO_FATAL_FAILURE(
      GetUintMetric({"paged_read_stats"}, "remote_decompressions", &decompressions));
  ASSERT_EQ(decompressions, 0ul);

  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    // Retrieve a read-only COW child of the pager-backed VMO. No way I know of
    // to get a writable one.
    zx_handle_t handle;
    ASSERT_EQ(fdio_get_vmo_clone(fd.get(), &handle), ZX_OK);
    zx::vmo parent(handle);
    ASSERT_TRUE(parent.is_valid());

    // Can't call ZX_VMO_OP_COMMIT on a readonly vmo. Creating a writeable COW
    // child of the COW child.
    zx::vmo vmo;
    ASSERT_EQ(parent.create_child(ZX_VMO_CHILD_SNAPSHOT_AT_LEAST_ON_WRITE, 0, kDataSize, &vmo),
              ZX_OK);
    ASSERT_TRUE(vmo.is_valid());

    ASSERT_EQ(vmo.op_range(ZX_VMO_OP_COMMIT, 0, kDataSize, nullptr, 0), ZX_OK);
  }

  // Decompressed it all in a single decompression instead of many 32K chunks.
  ASSERT_NO_FATAL_FAILURE(
      GetUintMetric({"paged_read_stats"}, "remote_decompressions", &decompressions));
  ASSERT_EQ(decompressions, 1ul);
}

}  // namespace
}  // namespace blobfs
