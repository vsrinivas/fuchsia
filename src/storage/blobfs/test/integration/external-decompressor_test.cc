// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/compression-settings.h>
#include <gtest/gtest.h>
#include <lib/fzl/owned-vmo-mapper.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <cstdlib>

#include "src/storage/blobfs/compression/chunked.h"
#include "src/storage/blobfs/compression/zstd-plain.h"
#include "src/storage/blobfs/compression/external-decompressor.h"


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
  ASSERT_EQ(ZX_OK, decompressor.DecompressRange(0, kDataSize, compressed_size));

  ASSERT_EQ(0, memcmp(input_data_, decompressed_mapper_.start(), kDataSize));

  // Ensure that we're testing multiple chunks and not one large chunk.
  zx::status<CompressionMapping> mapping_or = local_decompressor->MappingForDecompressedRange(0, 1);
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();
  ASSERT_GT(kDataSize, mapping.decompressed_length);
}

// Failure case for chunked decompression due to incorrect size.
TEST_F(ExternalDecompressorTest, PartialDecompressionWithBadSize) {
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

  zx::status<CompressionMapping> mapping_or = local_decompressor->MappingForDecompressedRange(0, 1);
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());
  ASSERT_EQ(ZX_ERR_INVALID_ARGS,
            decompressor.DecompressRange(0, mapping.decompressed_length - 1, compressed_size));
}

// Failure case for chunked decompression due to incorrect alignment.
TEST_F(ExternalDecompressorTest, PartialDecompressionWithBadAlignment) {
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

  zx::status<CompressionMapping> mapping_or = local_decompressor->MappingForDecompressedRange(0, 1);
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            decompressor.DecompressRange(1, mapping.decompressed_length - 1, compressed_size));
}

// Failure case for chunked decompression due to compression buffer being too small.
TEST_F(ExternalDecompressorTest, PartialDecompressionWithSmallBuffer) {
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

  zx::status<CompressionMapping> mapping_or = local_decompressor->MappingForDecompressedRange(0, 1);
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();

  // Ensure that the chunk is larger than 1 byte so that we can fall short.
  ASSERT_LT(1ul, mapping.compressed_length);

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY,
            decompressor.DecompressRange(0, mapping.decompressed_length, 1));
}

// Get a full range mapping for a range of frames starting at some index.
zx::status<CompressionMapping> MappingForFrames(size_t starting_frame, size_t num_frames,
                                                SeekableDecompressor* decompressor) {
  size_t current = 0;
  CompressionMapping mapping = {0, 0, 0, 0};
  for (size_t i = 0; i < starting_frame; i++) {
    zx::status<CompressionMapping> mapping_or =
        decompressor->MappingForDecompressedRange(current, 1);
    if (!mapping_or.is_ok()) {
      return  zx::error(mapping_or.status_value());
    }
    current += mapping_or.value().decompressed_length;
    mapping = mapping_or.value();
  }
  for (size_t i = 1; i < num_frames; i++) {
    zx::status<CompressionMapping> mapping_or =
        decompressor->MappingForDecompressedRange(current, 1);
    if (!mapping_or.is_ok()) {
      return  zx::error(mapping_or.status_value());
    }
    current += mapping_or.value().decompressed_length;
    mapping.decompressed_length += mapping_or.value().decompressed_length;
    mapping.compressed_length += mapping_or.value().compressed_length;
  }
  return zx::ok(mapping);
}

// Failure case for 2 frames where the second is not correctly sized, then the success with the
// correct sizes.
TEST_F(ExternalDecompressorTest, PartialDecompressionMultipleFrames) {
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

  zx::status<CompressionMapping> mapping_or = MappingForFrames(0, 2, local_decompressor.get());
  // This may fail if the archive is only a single frame.
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());
  // Fail for partial 2nd frame.
  ASSERT_EQ(ZX_ERR_INVALID_ARGS, decompressor.DecompressRange(
      mapping.decompressed_offset, mapping.decompressed_length - 1, compressed_size));

  // Fail for falling short on the compressed buffer length.
  ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, decompressor.DecompressRange(
      mapping.decompressed_offset, mapping.decompressed_length, mapping.compressed_length - 1));

  // Success with two full frames.
  ASSERT_EQ(ZX_OK, decompressor.DecompressRange(
      mapping.decompressed_offset, mapping.decompressed_length, compressed_size));
  ASSERT_EQ(0, memcmp(input_data_, decompressed_mapper_.start(), mapping.decompressed_length));
}

// Decompressing onle the second frame, since most test use the first frame.
TEST_F(ExternalDecompressorTest, PartialDecompressionSecondFrame) {
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

  zx::status<CompressionMapping> mapping_or = MappingForFrames(1, 1, local_decompressor.get());
  // This may fail if the archive is only a single frame.
  ASSERT_TRUE(mapping_or.is_ok());
  CompressionMapping mapping = mapping_or.value();

  ExternalSeekableDecompressor decompressor(client_.get(), local_decompressor.get());

  ASSERT_EQ(ZX_OK, decompressor.DecompressRange(
      mapping.decompressed_offset, mapping.decompressed_length,
      mapping.compressed_offset + mapping.compressed_length));
  ASSERT_EQ(
      0, memcmp(static_cast<uint8_t*>(input_data_) + mapping.decompressed_offset,
                static_cast<uint8_t*>(decompressed_mapper_.start()) + mapping.decompressed_offset,
                mapping.decompressed_length));
}

}  // namespace
}  // namespace blobfs
