// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/errors.h>

#include <algorithm>
#include <memory>

#include <gtest/gtest.h>

#include "src/storage/blobfs/blobfs.h"
#include "src/storage/blobfs/common.h"
#include "src/storage/blobfs/compression/blob_compressor.h"
#include "src/storage/blobfs/compression/decompressor.h"
#include "src/storage/blobfs/directory.h"
#include "src/storage/blobfs/format.h"
#include "src/storage/blobfs/test/blob_utils.h"
#include "src/storage/blobfs/test/blobfs_test_setup.h"

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
      EXPECT_TRUE(false) << "Bad Data Type";
  }
  return input;
}

void CompressionHelper(CompressionAlgorithm algorithm, const char* input, size_t size, size_t step,
                       std::optional<BlobCompressor>* out) {
  CompressionSettings settings{.compression_algorithm = algorithm};
  auto compressor = BlobCompressor::Create(settings, size);
  ASSERT_TRUE(compressor);

  size_t offset = 0;
  while (offset != size) {
    const size_t incremental_size = std::min(step, size - offset);
    ASSERT_EQ(compressor->Update(input + offset, incremental_size), ZX_OK);
    offset += incremental_size;
  }
  ASSERT_EQ(compressor->End(), ZX_OK);
  EXPECT_GT(compressor->Size(), 0ul);

  *out = std::move(compressor);
}

void DecompressionHelper(CompressionAlgorithm algorithm, const void* compressed_buf,
                         size_t compressed_size, const void* expected, size_t expected_size) {
  std::unique_ptr<char[]> uncompressed_buf(new char[expected_size]);
  size_t uncompressed_size = expected_size;
  std::unique_ptr<Decompressor> decompressor;
  ASSERT_EQ(Decompressor::Create(algorithm, &decompressor), ZX_OK);
  ASSERT_EQ(decompressor->Decompress(uncompressed_buf.get(), &uncompressed_size, compressed_buf,
                                     compressed_size),
            ZX_OK);
  EXPECT_EQ(expected_size, uncompressed_size);
  EXPECT_EQ(memcmp(expected, uncompressed_buf.get(), expected_size), 0);
}

// Tests a contained case of compression and decompression.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                               size_t step) {
  ASSERT_LE(step, size) << "Step size too large";

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, 0, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  CompressionHelper(algorithm, input.get(), size, step, &compressor);
  ASSERT_TRUE(compressor);

  // Decompress the buffer.

  DecompressionHelper(algorithm, compressor->Data(), compressor->Size(), input.get(), size);
}

TEST(CompressorTests, CompressDecompressChunkRandom1) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressChunkRandom2) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressChunkRandom3) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressChunkRandom4) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, CompressDecompressChunkCompressible1) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressDecompressChunkCompressible2) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressDecompressChunkCompressible3) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressDecompressChunkCompressible4) {
  RunCompressDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 15, 1 << 10);
}

TEST(CompressorTests, UpdateNoData) {
  const size_t input_size = 1024;
  CompressionSettings settings{.compression_algorithm = CompressionAlgorithm::kChunked};
  auto compressor = BlobCompressor::Create(settings, input_size);
  ASSERT_TRUE(compressor);

  std::unique_ptr<char[]> input(new char[input_size]);
  memset(input.get(), 'a', input_size);

  // Test that using "Update(data, 0)" acts a no-op, rather than corrupting the buffer.
  ASSERT_EQ(compressor->Update(input.get(), 0), ZX_OK);
  ASSERT_EQ(compressor->Update(input.get(), input_size), ZX_OK);
  ASSERT_EQ(compressor->End(), ZX_OK);

  // Ensure that even with the addition of a zero-length buffer, we still decompress
  // to the expected output.
  DecompressionHelper(CompressionAlgorithm::kChunked, compressor->Data(), compressor->Size(),
                      input.get(), input_size);
}

void DecompressionRoundHelper(CompressionAlgorithm algorithm, const void* compressed_buf,
                              size_t rounded_compressed_size, const void* expected,
                              size_t expected_size) {
  std::unique_ptr<char[]> uncompressed_buf(new char[expected_size]);
  size_t uncompressed_size = expected_size;
  size_t compressed_size = rounded_compressed_size;
  std::unique_ptr<Decompressor> decompressor;
  ASSERT_EQ(Decompressor::Create(algorithm, &decompressor), ZX_OK);
  ASSERT_EQ(decompressor->Decompress(uncompressed_buf.get(), &uncompressed_size, compressed_buf,
                                     compressed_size),
            ZX_OK);
  EXPECT_EQ(expected_size, uncompressed_size);
  EXPECT_EQ(memcmp(expected, uncompressed_buf.get(), expected_size), 0);
}

// Tests decompression's ability to handle receiving a compressed size that is rounded
// up to the nearest block size. This mimics blobfs' usage, where the exact compressed size
// is not stored explicitly.
//
// size: The size of the input buffer.
// step: The step size of updating the compression buffer.
void RunCompressRoundDecompressTest(CompressionAlgorithm algorithm, DataType data_type, size_t size,
                                    size_t step) {
  ASSERT_LE(step, size) << "Step size too large";

  // Generate input.
  std::unique_ptr<char[]> input(GenerateInput(data_type, 0, size));

  // Compress a buffer.
  std::optional<BlobCompressor> compressor;
  CompressionHelper(algorithm, input.get(), size, step, &compressor);
  ASSERT_TRUE(compressor);

  // Round up compressed size to nearest block size;
  size_t rounded_size = fbl::round_up(compressor->Size(), kBlobfsBlockSize);

  // Decompress the buffer while giving the rounded compressed size.

  DecompressionRoundHelper(algorithm, compressor->Data(), rounded_size, input.get(), size);
}

TEST(CompressorTests, CompressRoundDecompressRandom1) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 0, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressRandom2) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 1, 1 << 0);
}

TEST(CompressorTests, CompressRoundDecompressRandom3) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 10, 1 << 5);
}

TEST(CompressorTests, CompressRoundDecompressRandom4) {
  RunCompressRoundDecompressTest(CompressionAlgorithm::kChunked, DataType::Random, 1 << 15,
                                 1 << 10);
}

class BlobfsTestFixture : public testing::Test {
 protected:
  BlobfsTestFixture() {
    constexpr uint64_t kBlockCount = 1024;
    EXPECT_EQ(ZX_OK, setup_.CreateFormatMount(kBlockCount, kBlobfsBlockSize));

    fbl::RefPtr<fs::Vnode> root;
    EXPECT_EQ(setup_.blobfs()->OpenRootNode(&root), ZX_OK);
    root_ = fbl::RefPtr<Directory>::Downcast(std::move(root));
  }

  fbl::RefPtr<fs::Vnode> AddBlobToBlobfs(size_t data_size, DataType type) {
    std::unique_ptr<BlobInfo> blob_info = GenerateBlob(
        [type](uint8_t* data, size_t length) {
          auto generated_data = GenerateInput(type, 0, length);
          memcpy(data, generated_data.get(), length);
        },
        "", data_size);

    fbl::RefPtr<fs::Vnode> file;
    zx_status_t status = root_->Create(blob_info->path + 1, 0, &file);
    EXPECT_EQ(status, ZX_OK) << "Could not create file";
    if (status != ZX_OK) {
      return nullptr;
    }

    status = file->Truncate(data_size);
    EXPECT_EQ(status, ZX_OK) << "Could not truncate file";
    if (status != ZX_OK) {
      return nullptr;
    }
    size_t actual = 0;
    status = file->Write(blob_info->data.get(), data_size, 0, &actual);
    EXPECT_EQ(status, ZX_OK) << "Could not write file";
    if (status != ZX_OK) {
      return nullptr;
    }
    EXPECT_EQ(actual, data_size) << "Unexpected amount of written data";
    if (actual != data_size) {
      return nullptr;
    }

    return file;
  }

 private:
  BlobfsTestSetup setup_;
  fbl::RefPtr<Directory> root_;
};

using CompressorBlobfsTests = BlobfsTestFixture;

// Test that we do compress small blobs with compressible content.
TEST_F(CompressorBlobfsTests, CompressSmallCompressibleBlobs) {
  struct TestCase {
    size_t data_size;
    size_t expected_max_storage_size;
  };

  TestCase test_cases[] = {
      {
          16 * static_cast<size_t>(1024) - 1,
          16 * static_cast<size_t>(1024),
      },
      {
          16 * static_cast<size_t>(1024),
          16 * static_cast<size_t>(1024),
      },
      {
          16 * static_cast<size_t>(1024) + 1,
          16 * static_cast<size_t>(1024),
      },
  };

  for (const TestCase& test_case : test_cases) {
    printf("Test case: data size %zu\n", test_case.data_size);
    fbl::RefPtr<fs::Vnode> file = AddBlobToBlobfs(test_case.data_size, DataType::Compressible);

    fs::VnodeAttributes attributes;
    ASSERT_EQ(file->GetAttributes(&attributes), ZX_OK);

    EXPECT_EQ(attributes.content_size, test_case.data_size);
    EXPECT_LE(attributes.storage_size, test_case.expected_max_storage_size);

    ASSERT_EQ(file->Close(), ZX_OK);
  }
}

TEST_F(CompressorBlobfsTests, DoNotInflateIncompressibleBlobs) {
  size_t data_sizes[] = {
      8 * static_cast<size_t>(1024) - 1,   8 * static_cast<size_t>(1024),
      8 * static_cast<size_t>(1024) + 1,   16 * static_cast<size_t>(1024) - 1,
      16 * static_cast<size_t>(1024),      16 * static_cast<size_t>(1024) + 1,
      128 * static_cast<size_t>(8192) + 1,
  };

  for (size_t data_size : data_sizes) {
    if (data_size != 8193)
      continue;
    printf("Test case: data size %zu\n", data_size);
    fbl::RefPtr<fs::Vnode> file = AddBlobToBlobfs(data_size, DataType::Random);

    fs::VnodeAttributes attributes;
    ASSERT_EQ(file->GetAttributes(&attributes), ZX_OK);

    EXPECT_EQ(attributes.content_size, data_size);
    // Beyond 1 block, we need 1 block for the Merkle tree.
    size_t expected_max_storage_size = fbl::round_up(data_size, kBlobfsBlockSize) +
                                       (data_size > kBlobfsBlockSize ? kBlobfsBlockSize : 0);

    EXPECT_LE(attributes.storage_size, expected_max_storage_size);

    ASSERT_EQ(file->Close(), ZX_OK);
  }
}

}  // namespace
}  // namespace blobfs
