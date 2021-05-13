// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/tools/blobfs-compression/blobfs-compression.h"

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <gtest/gtest.h>

#include "src/lib/chunked-compression/chunked-compressor.h"
#include "src/storage/blobfs/compression/configs/chunked_compression_params.h"

namespace blobfs_compress {
namespace {
using namespace blobfs;

void BufferFill(uint8_t* data, size_t size, unsigned seed) {
  size_t i = 0;
  while (i < size) {
    size_t run_length = 1 + (rand_r(&seed) % (size - i));
    char value = static_cast<char>(rand_r(&seed) % std::numeric_limits<char>::max());
    memset(data + i, value, run_length);
    i += run_length;
  }
}
}  // namespace

TEST(BlobfsCompressionTest, CompressBufferEmpty) {
  uint8_t* data = nullptr;
  size_t len = 0ul;

  size_t compressed_len;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  ASSERT_EQ(BlobfsCompress(data, len, compressed_data.get(), &compressed_len, params, {}), ZX_OK);
  EXPECT_EQ(compressed_len, 0ul);
}

TEST(BlobfsCompressionTest, CompressBufferSmall) {
  size_t len = 1000ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);
  BufferFill(data.get(), len, 0);

  size_t compressed_len;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  ASSERT_EQ(BlobfsCompress(data.get(), len, compressed_data.get(), &compressed_len, params, {}),
            ZX_OK);
  ASSERT_GE(compressed_data.size(), compressed_len);
}

TEST(BlobfsCompressionTest, CompressBufferlarge) {
  size_t len = 1200000ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);
  BufferFill(data.get(), len, 0);

  size_t compressed_len;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);

  ASSERT_EQ(BlobfsCompress(data.get(), len, compressed_data.get(), &compressed_len, params, {}),
            ZX_OK);
  ASSERT_GE(compressed_data.size(), compressed_len);
}

TEST(BlobfsCompressionTest, CompressNoDestBuffer) {
  size_t len = 1000ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);
  BufferFill(data.get(), len, 0);

  size_t compressed_len, compressed_len_no_dest;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);

  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  ASSERT_EQ(BlobfsCompress(data.get(), len, compressed_data.get(), &compressed_len, params, {}),
            ZX_OK);
  ASSERT_EQ(BlobfsCompress(data.get(), len, nullptr, &compressed_len_no_dest, params, {}), ZX_OK);
  ASSERT_GT(compressed_len_no_dest, (size_t)0);
  ASSERT_EQ(compressed_len_no_dest, compressed_len);
}

TEST(BlobfsCompressionTest, CompressWithMerkleTree) {
  const size_t len = 10000000ul;  // Must be bigger than digest:kDefaultNodeSize.
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);
  BufferFill(data.get(), len, 0);

  // We use non-compact merkle tree for calculation.
  size_t compressed_len_with_merkle_tree;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  ASSERT_EQ(BlobfsCompress(data.get(), len, compressed_data.get(), &compressed_len_with_merkle_tree,
                           params, {}),
            ZX_OK);
  // In this scenario, it is correct. But it is not always true mathematically.
  ASSERT_GT(compressed_data.size(), compressed_len_with_merkle_tree);
}

TEST(BlobfsCompressionTest, DisableSizeAlignment) {
  const size_t len = 1000000ul;  // Must be bigger than digest:kDefaultNodeSize.
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);
  BufferFill(data.get(), len, 0);

  // We use non-compact merkle tree for calculation.
  size_t compressed_len_with_aligned_size;
  chunked_compression::CompressionParams params = blobfs::GetDefaultChunkedCompressionParams(len);
  size_t compressed_limit = params.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  ASSERT_EQ(BlobfsCompress(data.get(), len, compressed_data.get(),
                           &compressed_len_with_aligned_size, params, {}),
            ZX_OK);

  size_t compressed_len_without_size_alignment;
  ASSERT_EQ(BlobfsCompress(data.get(), len, nullptr, &compressed_len_without_size_alignment, params,
                           {.disable_size_alignment = true}),
            ZX_OK);

  // Non-compact merkle tree has a larger padding size than the compact merkle tree.
  ASSERT_GT(compressed_len_with_aligned_size, compressed_len_without_size_alignment);
}

}  // namespace blobfs_compress
