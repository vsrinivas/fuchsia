// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "compression/compressor.h"

#include <stdlib.h>
#include <zircon/assert.h>

#include <algorithm>
#include <memory>

#include <blobfs/format.h>
#include <zxtest/zxtest.h>

#include "compression/blob-compressor.h"
#include "compression/lz4.h"
#include "compression/zstd-plain.h"
#include "compression/zstd-rac.h"
#include "zircon/errors.h"

namespace blobfs {
namespace {

enum class DataType {
  Compressible,
  Random,
};

std::unique_ptr<char[]> GenerateInput(DataType data_type, unsigned seed, size_t size) {
  std::unique_ptr<char[]> input(new char[size]);
  switch (data_type) {
    case DataType::Compressible: {
      size_t i = 0;
      while (i < size) {
        size_t run_length = 1 + (rand_r(&seed) % (size - i));
        char value = static_cast<char>(rand_r(&seed) % std::numeric_limits<char>::max());
        memset(input.get() + i, value, run_length);
        i += run_length;
      }
      break;
    }
    case DataType::Random:
      for (size_t i = 0; i < size; i++) {
        input[i] = static_cast<char>(rand_r(&seed));
      }
      break;
    default:
      ADD_FAILURE("Bad Data Type");
  }
  return input;
}

void CompressionHelper(CompressionAlgorithm algorithm, const char* input, size_t size, size_t step,
                       std::optional<BlobCompressor>* out) {
  auto compressor = BlobCompressor::Create(algorithm, size);
  ASSERT_TRUE(compressor);

  size_t offset = 0;
  while (offset != size) {
    const void* data = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(input) + offset);
    const size_t incremental_size = std::min(step, size - offset);
    ASSERT_OK(compressor->Update(data, incremental_size));
    offset += incremental_size;
  }
  ASSERT_OK(compressor->End());
  EXPECT_GT(compressor->Size(), 0);

  *out = std::move(compressor);
}

void DecompressionHelper(CompressionAlgorithm algorithm, const void* compressed,
                         size_t compressed_size, const void* expected, size_t expected_size) {
  std::unique_ptr<char[]> output(new char[expected_size]);
  size_t target_size = expected_size;
  size_t src_size = compressed_size;
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      ASSERT_OK(LZ4Decompress(output.get(), &target_size, compressed, &src_size));
      break;
    case CompressionAlgorithm::ZSTD:
      ASSERT_OK(ZSTDDecompress(output.get(), &target_size, compressed, &src_size));
      break;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      ASSERT_OK(ZSTDSeekableDecompress(output.get(), &target_size, compressed));
      break;
    default:
      FAIL("Bad algorithm");
  }
  EXPECT_EQ(expected_size, target_size);
  EXPECT_EQ(compressed_size, src_size);
  EXPECT_BYTES_EQ(expected, output.get(), expected_size);
}

// Tests a contained case of compression and decompression.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                               size_t step) {
  ASSERT_LE(step, size, "Step size too large");

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, 0, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  ASSERT_NO_FAILURES(CompressionHelper(algorithm, input.get(), size, step, &compressor));
  ASSERT_TRUE(compressor);

  // Decompress the buffer.
  ASSERT_NO_FAILURES(
      DecompressionHelper(algorithm, compressor->Data(), compressor->Size(), input.get(), size));
}

TEST(CompressorTests, CompressDecompressLZ4Random1) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressLZ4Random2) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressLZ4Random3) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressLZ4Random4) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressDecompressLZ4Compressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressLZ4Compressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressLZ4Compressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressLZ4Compressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressDecompressZSTDRandom1) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDRandom2) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDRandom3) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressZSTDRandom4) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressDecompressZSTDCompressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDCompressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDCompressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressZSTDCompressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableRandom1) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableRandom2) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableRandom3) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableRandom4) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 15,
                            1 << 10);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableCompressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableCompressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableCompressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressZSTDSeekableCompressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 15,
                            1 << 10);
}

TEST(CompressorTests, DecompressZSTDSeekableCompressiblesFailsOnNoSize) {
  size_t output_size = 512;
  size_t input_size = 512;
  size_t invalid_size = 0;
  std::unique_ptr<char[]> input(GenerateInput(DataType::Compressible, 0, input_size));
  std::unique_ptr<char[]> output(new char[output_size]);

  ASSERT_STATUS(ZSTDDecompress(output.get(), &output_size, input.get(), &invalid_size),
                ZX_ERR_INVALID_ARGS);
  invalid_size = 0;
  ASSERT_STATUS(ZSTDDecompress(output.get(), &invalid_size, input.get(), &input_size),
                ZX_ERR_INVALID_ARGS);
  invalid_size = 0;
  ASSERT_STATUS(ZSTDDecompress(output.get(), &invalid_size, input.get(), &invalid_size),
                ZX_ERR_INVALID_ARGS);
}

void RunUpdateNoDataTest(CompressionAlgorithm algorithm) {
  const size_t input_size = 1024;
  auto compressor = BlobCompressor::Create(algorithm, input_size);
  ASSERT_TRUE(compressor);

  std::unique_ptr<char[]> input(new char[input_size]);
  memset(input.get(), 'a', input_size);

  // Test that using "Update(data, 0)" acts a no-op, rather than corrupting the buffer.
  ASSERT_OK(compressor->Update(input.get(), 0));
  ASSERT_OK(compressor->Update(input.get(), input_size));
  ASSERT_OK(compressor->End());

  // Ensure that even with the addition of a zero-length buffer, we still decompress
  // to the expected output.
  ASSERT_NO_FAILURES(DecompressionHelper(algorithm, compressor->Data(), compressor->Size(),
                                         input.get(), input_size));
}

TEST(CompressorTests, UpdateNoDataLZ4) { RunUpdateNoDataTest(CompressionAlgorithm::LZ4); }

TEST(CompressorTests, UpdateNoDataZSTD) { RunUpdateNoDataTest(CompressionAlgorithm::ZSTD); }

TEST(CompressorTests, UpdateNoDataZSTDSeekable) {
  RunUpdateNoDataTest(CompressionAlgorithm::ZSTD_SEEKABLE);
}

void DecompressionRoundHelper(CompressionAlgorithm algorithm, const void* compressed,
                              size_t rounded_compressed_size, const void* expected,
                              size_t expected_size) {
  std::unique_ptr<char[]> output(new char[expected_size]);
  size_t target_size = expected_size;
  size_t src_size = rounded_compressed_size;
  switch (algorithm) {
    case CompressionAlgorithm::LZ4:
      ASSERT_OK(LZ4Decompress(output.get(), &target_size, compressed, &src_size));
      break;
    case CompressionAlgorithm::ZSTD:
      ASSERT_OK(ZSTDDecompress(output.get(), &target_size, compressed, &src_size));
      break;
    case CompressionAlgorithm::ZSTD_SEEKABLE:
      ASSERT_OK(ZSTDSeekableDecompress(output.get(), &target_size, compressed));
      break;
    default:
      FAIL("Bad algorithm");
  }
  EXPECT_EQ(expected_size, target_size);
  EXPECT_GE(rounded_compressed_size, src_size);
  EXPECT_BYTES_EQ(expected, output.get(), expected_size);
}

// Tests decompression's ability to handle receiving a compressed size that is rounded
// up to the nearest block size. This mimics blobfs' usage, where the exact compressed size
// is not stored explicitly.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressRoundDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                                    size_t step) {
  ASSERT_LE(step, size, "Step size too large");

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, 0, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  ASSERT_NO_FAILURES(CompressionHelper(algorithm, input.get(), size, step, &compressor));
  ASSERT_TRUE(compressor);

  // Round up compressed size to nearest block size;
  size_t rounded_size = fbl::round_up(compressor->Size(), kBlobfsBlockSize);

  // Decompress the buffer while giving the rounded compressed size.
  ASSERT_NO_FAILURES(
      DecompressionRoundHelper(algorithm, compressor->Data(), rounded_size, input.get(), size));
}

TEST(CompressorTests, CompressRoundDecompressLZ4Random1) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressLZ4Random2) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressLZ4Random3) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressRoundDecompressLZ4Random4) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::LZ4, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressRoundDecompressZSTDRandom1) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressZSTDRandom2) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressZSTDRandom3) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressRoundDecompressZSTDRandom4) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressRoundDecompressZSTDSeekableRandom1) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 0,
                                 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressZSTDSeekableRandom2) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 1,
                                 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressZSTDSeekableRandom3) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 10,
                                 1 << 5);
}

TEST(CompressorTests, CompressRoundDecompressZSTDSeekableRandom4) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::ZSTD_SEEKABLE, DataType::Random, 1 << 15,
                                 1 << 10);
}

}  // namespace
}  // namespace blobfs
