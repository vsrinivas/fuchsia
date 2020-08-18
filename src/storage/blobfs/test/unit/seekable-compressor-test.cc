// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>

#include <algorithm>
#include <memory>
#include <optional>

#include <blobfs/format.h>
#include <gtest/gtest.h>

#include "compression/blob-compressor.h"
#include "compression/chunked.h"
#include "compression/compressor.h"
#include "compression/seekable-decompressor.h"
#include "compression/zstd-seekable.h"
#include "zircon/errors.h"

namespace blobfs {
namespace {

enum class DataType {
  Compressible,
  Random,
};

std::unique_ptr<char[]> GenerateInput(DataType data_type, unsigned* seed, size_t size) {
  std::unique_ptr<char[]> input(new char[size]);
  switch (data_type) {
    case DataType::Compressible: {
      size_t i = 0;
      while (i < size) {
        size_t run_length = 1 + (rand_r(seed) % (size - i));
        char value = static_cast<char>(rand_r(seed) % std::numeric_limits<char>::max());
        memset(input.get() + i, value, run_length);
        i += run_length;
      }
      break;
    }
    case DataType::Random:
      for (size_t i = 0; i < size; i++) {
        input[i] = static_cast<char>(rand_r(seed));
      }
      break;
    default:
      EXPECT_TRUE(false) << "Bad Data Type";
  }
  return input;
}

void CompressionHelper(CompressionAlgorithm algorithm, const char* input, size_t size, size_t step,
                       std::optional<BlobCompressor>* out) {
  CompressionSettings settings = {
      .compression_algorithm = algorithm,
  };
  auto compressor = BlobCompressor::Create(settings, size);
  ASSERT_TRUE(compressor);

  size_t offset = 0;
  while (offset != size) {
    const void* data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(input) + offset);
    const size_t incremental_size = std::min(step, size - offset);
    ASSERT_EQ(compressor->Update(data, incremental_size), ZX_OK);
    offset += incremental_size;
  }
  ASSERT_EQ(compressor->End(), ZX_OK);
  EXPECT_GT(compressor->Size(), 0ul);

  *out = std::move(compressor);
}

void DecompressAndVerifyMapping(SeekableDecompressor* decompressor, const uint8_t* compressed_buf,
                                size_t compressed_size, const uint8_t* expected,
                                size_t expected_size, const CompressionMapping& mapping) {
  ASSERT_LE(mapping.decompressed_offset + mapping.decompressed_length, expected_size);
  ASSERT_LE(mapping.compressed_offset + mapping.compressed_length, compressed_size);
  fbl::Array<uint8_t> buf(new uint8_t[mapping.decompressed_length], mapping.decompressed_length);
  size_t sz = mapping.decompressed_length;
  ASSERT_EQ(
      decompressor->DecompressRange(buf.get(), &sz, compressed_buf + mapping.compressed_offset,
                                    mapping.compressed_length, mapping.decompressed_offset),
      ZX_OK);
  EXPECT_EQ(mapping.decompressed_length, sz);
  EXPECT_EQ(memcmp(expected + mapping.decompressed_offset, buf.get(), sz), 0);
}

void DecompressionHelper(SeekableDecompressor* decompressor, unsigned* seed,
                         const void* compressed_buf, size_t compressed_size, const void* expected,
                         size_t expected_size) {
  // 1. Sequential decompression of each range
  size_t offset = 0;
  zx::status<CompressionMapping> mapping = zx::ok(CompressionMapping{});
  while ((mapping = decompressor->MappingForDecompressedRange(offset, 1)).is_ok()) {
    DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                               compressed_size, static_cast<const uint8_t*>(expected),
                               expected_size, mapping.value());
    offset += mapping->decompressed_length;
  }
  // 2. Random offsets
  for (int i = 0; i < 100; ++i) {
    offset = rand_r(seed) % expected_size;
    mapping = decompressor->MappingForDecompressedRange(offset, 1);
    ASSERT_TRUE(mapping.is_ok());
    DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                               compressed_size, static_cast<const uint8_t*>(expected),
                               expected_size, mapping.value());
  }
  // 3. Full range
  mapping = decompressor->MappingForDecompressedRange(0, expected_size);
  ASSERT_TRUE(mapping.is_ok());
  DecompressAndVerifyMapping(decompressor, static_cast<const uint8_t*>(compressed_buf),
                             compressed_size, static_cast<const uint8_t*>(expected), expected_size,
                             mapping.value());
}

// Tests a contained case of compression and decompression.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                               size_t step) {
  ASSERT_LE(step, size) << "Step size too large";

  unsigned seed = ::testing::UnitTest::GetInstance()->random_seed();

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, &seed, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  CompressionHelper(algorithm, input.get(), size, step, &compressor);
  ASSERT_TRUE(compressor);

  // Decompress the buffer.
  std::unique_ptr<SeekableDecompressor> decompressor;
  switch (algorithm) {
    case CompressionAlgorithm::CHUNKED: {
      ASSERT_EQ(SeekableChunkedDecompressor::CreateDecompressor(
                    compressor->Data(), compressor->Size(), compressor->Size(), &decompressor),
                ZX_OK);
      break;
    }
    default:
      ASSERT_TRUE(false);
  }
  DecompressionHelper(decompressor.get(), &seed, compressor->Data(), compressor->Size(),
                      input.get(), size);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 0, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 1, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 10, 1 << 5);
}

TEST(SeekableCompressorTest, CompressDecompressChunkCompressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 15, 1 << 10);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom1) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 0, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom2) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 1, 1 << 0);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom3) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 10, 1 << 5);
}

TEST(SeekableCompressorTest, CompressDecompressChunkRandom4) {
  RunCompressDecompressTest(CompressionAlgorithm::CHUNKED, DataType::Random, 1 << 15, 1 << 10);
}

}  // namespace
}  // namespace blobfs
