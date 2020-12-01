// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/compression/external-decompressor.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/service/cpp/reader.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdlib>

#include <blobfs/compression-settings.h>
#include <gtest/gtest.h>

#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/zstd-plain.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/integration/fdio_test.h"

namespace blobfs {
namespace {

// These settings currently achieve about 60% compression.
constexpr int kCompressionLevel = 5;
constexpr double kDataRandomnessRatio = 0.25;

constexpr size_t kDataSize = 500 * 1024;  // 500KiB
constexpr size_t kMapSize = kDataSize * 2;

// Generates a data set of size with sequences of the same bytes and random
// values appearing with frequency kDataRandomnessRatio.
void GenerateData(size_t size, uint8_t* dst) {
  srand(testing::GTEST_FLAG(random_seed));
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

  zx::status<std::unique_ptr<ExternalDecompressorClient>> client_or =
      ExternalDecompressorClient::Create(std::move(decompressed_vmo), std::move(compressed_vmo));
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, client_or.status_value());
}

TEST(ExternalDecompressorSetUpTest, CompressedVmoMissingDuplicate) {
  zx::vmo decompressed_vmo;
  ASSERT_EQ(ZX_OK, zx::vmo::create(kMapSize, 0, &decompressed_vmo));
  zx::vmo compressed_vmo;
  ASSERT_EQ(ZX_OK, decompressed_vmo.duplicate(ZX_DEFAULT_VMO_RIGHTS & (~ZX_RIGHT_DUPLICATE),
                                              &compressed_vmo));

  zx::status<std::unique_ptr<ExternalDecompressorClient>> client_or =
      ExternalDecompressorClient::Create(std::move(decompressed_vmo), std::move(compressed_vmo));
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

    zx::status<std::unique_ptr<ExternalDecompressorClient>> client_or =
        ExternalDecompressorClient::Create(std::move(remote_decompressed_vmo),
                                           std::move(remote_compressed_vmo));
    ASSERT_EQ(ZX_OK, client_or.status_value());
    client_ = std::move(client_or.value());
  }

 protected:
  uint8_t input_data_[kDataSize];
  fzl::OwnedVmoMapper compressed_mapper_;
  fzl::OwnedVmoMapper decompressed_mapper_;
  std::unique_ptr<ExternalDecompressorClient> client_;
};

// Simple success case for full decompression
TEST_F(ExternalDecompressorTest, FullDecompression) {
  size_t compressed_size;
  std::unique_ptr<ZSTDCompressor> compressor = nullptr;
  ASSERT_EQ(ZX_OK,
            ZSTDCompressor::Create({CompressionAlgorithm::ZSTD, kCompressionLevel}, kDataSize,
                                   compressed_mapper_.start(), kMapSize, &compressor));
  CompressData(std::move(compressor), input_data_, &compressed_size);
  ExternalDecompressor decompressor(client_.get(), CompressionAlgorithm::ZSTD);
  ASSERT_EQ(ZX_OK, decompressor.Decompress(kDataSize, compressed_size));

  ASSERT_EQ(0, memcmp(input_data_, decompressed_mapper_.start(), kDataSize));
}

// Get a full range mapping for a SeekableDecompressor.
zx::status<std::vector<CompressionMapping>> GetMappings(SeekableDecompressor* decompressor,
                                                        size_t length) {
  std::vector<CompressionMapping> mappings;
  size_t current = 0;
  while (current < length) {
    zx::status<CompressionMapping> mapping_or =
        decompressor->MappingForDecompressedRange(current, 1);
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
  ASSERT_EQ(ZX_OK, ChunkedCompressor::Create({CompressionAlgorithm::CHUNKED, kCompressionLevel},
                                             kDataSize, &compressed_size, &compressor));
  ASSERT_EQ(ZX_OK, compressor->SetOutput(compressed_mapper_.start(), kMapSize));
  CompressData(std::move(compressor), input_data_, &compressed_size);

  std::unique_ptr<SeekableDecompressor> local_decompressor;
  ASSERT_EQ(ZX_OK,
            SeekableChunkedDecompressor::CreateDecompressor(
                compressed_mapper_.start(), compressed_size, compressed_size, &local_decompressor));

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());

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

fit::result<inspect::Hierarchy> TakeSnapshot(fuchsia::inspect::TreePtr tree,
                                             async::Executor* executor) {
  std::condition_variable cv;
  std::mutex m;
  bool done = false;
  fit::result<inspect::Hierarchy> hierarchy_or_error;

  auto promise =
      inspect::ReadFromTree(std::move(tree)).then([&](fit::result<inspect::Hierarchy>& result) {
        {
          std::unique_lock<std::mutex> lock(m);
          hierarchy_or_error = std::move(result);
          done = true;
        }
        cv.notify_all();
      });

  executor->schedule_task(std::move(promise));

  std::unique_lock<std::mutex> lock(m);
  cv.wait(lock, [&done]() { return done; });

  return hierarchy_or_error;
}

void GetRemoteDecompressions(async::Executor* executor, zx_handle_t diagnostics_dir,
                             const std::string& node_name, uint64_t* value) {
  ASSERT_NE(executor, nullptr);
  ASSERT_NE(value, nullptr);

  fuchsia::inspect::TreePtr tree;
  async_dispatcher_t* dispatcher = executor->dispatcher();
  ASSERT_EQ(fdio_service_connect_at(diagnostics_dir, "fuchsia.inspect.Tree",
                                    tree.NewRequest(dispatcher).TakeChannel().release()),
            ZX_OK);

  fit::result<inspect::Hierarchy> hierarchy_or_error = TakeSnapshot(std::move(tree), executor);
  ASSERT_TRUE(hierarchy_or_error.is_ok());
  inspect::Hierarchy hierarchy = std::move(hierarchy_or_error.value());

  const inspect::Hierarchy* direct_parent = &hierarchy;
  direct_parent = direct_parent->GetByPath({node_name});
  ASSERT_NE(direct_parent, nullptr);

  const inspect::UintPropertyValue* node =
      direct_parent->node().get_property<inspect::UintPropertyValue>("remote_decompressions");
  ASSERT_NE(node, nullptr);

  *value = node->value();
}

class ExternalDecompressorE2ePagedTest : public FdioTest {
 public:
  ExternalDecompressorE2ePagedTest() {
    MountOptions options;
    // Chunked files will be paged in.
    options.pager_backed_cache_policy = CachePolicy::EvictImmediately;
    options.compression_settings = {CompressionAlgorithm::CHUNKED, 14};
    options.sandbox_decompression = true;
    set_mount_options(options);
  }
};

TEST_F(ExternalDecompressorE2ePagedTest, VerifyRemoteDecompression) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("metrics-thread");
  async::Executor executor(loop.dispatcher());

  // Create a new blob on the mounted filesystem.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(
      GenerateRealisticBlob(".", kDataSize, BlobLayoutFormat::kPaddedMerkleTreeAtStart, &info));
  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  uint64_t before_decompressions;
  ASSERT_NO_FATAL_FAILURE(GetRemoteDecompressions(&executor, diagnostics_dir(), "paged_read_stats",
                                                  &before_decompressions));

  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  }

  uint64_t after_decompressions;
  ASSERT_NO_FATAL_FAILURE(GetRemoteDecompressions(&executor, diagnostics_dir(), "paged_read_stats",
                                                  &after_decompressions));
  ASSERT_GT(after_decompressions, before_decompressions);

  loop.Quit();
  loop.JoinThreads();
}

class ExternalDecompressorE2eUnpagedTest : public FdioTest {
 public:
  ExternalDecompressorE2eUnpagedTest() {
    MountOptions options;
    // ZSTD files will be done all at once.
    options.compression_settings = {CompressionAlgorithm::ZSTD, 14};
    options.sandbox_decompression = true;
    set_mount_options(options);
  }
};

TEST_F(ExternalDecompressorE2eUnpagedTest, VerifyRemoteDecompression) {
  async::Loop loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  loop.StartThread("metrics-thread");
  async::Executor executor(loop.dispatcher());

  // Create a new blob on the mounted filesystem.
  std::unique_ptr<BlobInfo> info;
  ASSERT_NO_FATAL_FAILURE(
      GenerateRealisticBlob(".", kDataSize, BlobLayoutFormat::kPaddedMerkleTreeAtStart, &info));
  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_CREAT | O_RDWR));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_EQ(ftruncate(fd.get(), info->size_data), 0);
    ASSERT_EQ(StreamAll(write, fd.get(), info->data.get(), info->size_data), 0)
        << "Failed to write Data";
  }

  uint64_t before_decompressions;
  ASSERT_NO_FATAL_FAILURE(GetRemoteDecompressions(&executor, diagnostics_dir(),
                                                  "unpaged_read_stats", &before_decompressions));

  {
    fbl::unique_fd fd(openat(root_fd(), info->path, O_RDONLY));
    ASSERT_TRUE(fd.is_valid());
    ASSERT_NO_FATAL_FAILURE(VerifyContents(fd.get(), info->data.get(), info->size_data));
  }

  uint64_t after_decompressions;
  ASSERT_NO_FATAL_FAILURE(GetRemoteDecompressions(&executor, diagnostics_dir(),
                                                  "unpaged_read_stats", &after_decompressions));
  ASSERT_GT(after_decompressions, before_decompressions);

  loop.Quit();
  loop.JoinThreads();
}

}  // namespace
}  // namespace blobfs
