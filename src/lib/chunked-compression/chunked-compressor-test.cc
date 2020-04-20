// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <zxtest/zxtest.h>

namespace chunked_compression {
namespace {

// Writes |len| bytes of random data to |data|.
void RandomFill(uint8_t* data, size_t len) {
  size_t off = 0;
  size_t rounded_len = fbl::round_down(len, sizeof(int));
  for (off = 0; off < rounded_len; off += sizeof(int)) {
    *reinterpret_cast<int*>(data + off) = rand();
  }
  ZX_ASSERT(off == rounded_len);
  for (; off < len; ++off) {
    data[off] = static_cast<uint8_t>(rand());
  }
}

}  // namespace

TEST(ChunkedCompressorTest, ComputeOutputSizeLimit_Zero) {
  CompressionParams params;
  ASSERT_EQ(params.ComputeOutputSizeLimit(0u), 0ul);
}

TEST(ChunkedCompressorTest, ComputeOutputSizeLimit_Minimum) {
  CompressionParams params;
  // There should always be enough bytes for at least the metadata and one seek table entry.
  ASSERT_GE(params.ComputeOutputSizeLimit(1u),
            kChunkArchiveSeekTableOffset + sizeof(SeekTableEntry));
}

TEST(ChunkedCompressorTest, Compress_EmptyInput) {
  void* data = nullptr;
  size_t len = 0ul;

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data, len, &compressed_data, &compressed_len),
            kStatusOk);
  EXPECT_EQ(compressed_len, 0ul);
}

TEST(ChunkedCompressorTest, Compress_Zeroes_Short) {
  size_t len = 8192ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  SeekTable seek_table;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
            kStatusOk);
  ASSERT_EQ(seek_table.Entries().size(), 1u);
  SeekTableEntry entry = seek_table.Entries()[0];
  // Chunk spans all of the input data
  EXPECT_EQ(entry.decompressed_offset, 0ul);
  EXPECT_EQ(entry.decompressed_size, len);
  // Chunk spans all of the output data, too.
  // Starts after the header.
  EXPECT_EQ(entry.compressed_offset, 64ul);
  EXPECT_EQ(entry.compressed_offset + entry.compressed_size, compressed_len);
}

TEST(ChunkedCompressorTest, Compress_Random_Short) {
  size_t len = 8192ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  SeekTable seek_table;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
            kStatusOk);
  ASSERT_EQ(seek_table.Entries().size(), 1u);
  SeekTableEntry entry = seek_table.Entries()[0];
  // Chunk spans all of the input data
  EXPECT_EQ(entry.decompressed_offset, 0ul);
  EXPECT_EQ(entry.decompressed_size, len);
  // Chunk spans all of the output data, too.
  // Starts after the header.
  EXPECT_EQ(entry.compressed_offset, 64ul);
  EXPECT_EQ(entry.compressed_offset + entry.compressed_size, compressed_len);
}

TEST(ChunkedCompressorTest, Compress_Zeroes_Long) {
  // 3 data frames, last one partial
  size_t len = (2 * CompressionParams::MinChunkSize()) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  SeekTable seek_table;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
            kStatusOk);
  ASSERT_EQ(seek_table.Entries().size(), 3u);

  size_t decompressed_size_total = 0;
  // Include metadata in compressed size
  size_t compressed_size_total =
      kChunkArchiveSeekTableOffset + seek_table.Entries().size() * sizeof(SeekTableEntry);
  for (unsigned i = 0; i < seek_table.Entries().size(); ++i) {
    SeekTableEntry entry = seek_table.Entries()[i];
    decompressed_size_total += entry.decompressed_size;
    compressed_size_total += entry.compressed_size;
  }
  EXPECT_EQ(decompressed_size_total, len);
  EXPECT_EQ(compressed_size_total, compressed_len);
}

TEST(ChunkedCompressorTest, Compress_Random_Long) {
  // 3 data frames, last one partial
  size_t len = (2 * CompressionParams::MinChunkSize()) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  SeekTable seek_table;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
            kStatusOk);
  ASSERT_EQ(seek_table.Entries().size(), 3u);

  size_t decompressed_size_total = 0;
  // Include metadata in compressed size
  size_t compressed_size_total =
      kChunkArchiveSeekTableOffset + seek_table.Entries().size() * sizeof(SeekTableEntry);
  for (unsigned i = 0; i < seek_table.Entries().size(); ++i) {
    SeekTableEntry entry = seek_table.Entries()[i];
    decompressed_size_total += entry.decompressed_size;
    compressed_size_total += entry.compressed_size;
  }
  EXPECT_EQ(decompressed_size_total, len);
  EXPECT_EQ(compressed_size_total, compressed_len);
}

TEST(ChunkedCompressorTest, Compress_ReuseCompressor) {
  CompressionParams params;
  ChunkedCompressor compressor(params);

  {
    size_t len = 8192ul;
    fbl::Array<uint8_t> data(new uint8_t[len], len);
    memset(data.get(), 0x00, len);

    size_t compressed_limit = params.ComputeOutputSizeLimit(len);
    fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
    size_t compressed_len;
    ASSERT_EQ(compressor.Compress(data.get(), len, compressed_data.get(), compressed_data.size(),
                                  &compressed_len),
              kStatusOk);
    ASSERT_GE(compressed_data.size(), compressed_len);

    SeekTable seek_table;
    HeaderReader reader;
    ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
              kStatusOk);
    ASSERT_EQ(seek_table.Entries().size(), 1u);
    SeekTableEntry entry = seek_table.Entries()[0];
    // Chunk spans all of the input data
    EXPECT_EQ(entry.decompressed_offset, 0ul);
    EXPECT_EQ(entry.decompressed_size, len);
    // Chunk spans all of the output data, too.
    // Starts after the header.
    EXPECT_EQ(entry.compressed_offset, 64ul);
    EXPECT_EQ(entry.compressed_offset + entry.compressed_size, compressed_len);
  }
  {
    size_t len = 8192ul;
    fbl::Array<uint8_t> data(new uint8_t[len], len);
    // Set with different input data.
    memset(data.get(), 0xac, len);

    size_t compressed_limit = params.ComputeOutputSizeLimit(len);
    fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
    size_t compressed_len;
    ASSERT_EQ(compressor.Compress(data.get(), len, compressed_data.get(), compressed_data.size(),
                                  &compressed_len),
              kStatusOk);
    ASSERT_GE(compressed_data.size(), compressed_len);

    SeekTable seek_table;
    HeaderReader reader;
    ASSERT_EQ(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &seek_table),
              kStatusOk);
    ASSERT_EQ(seek_table.Entries().size(), 1u);
    SeekTableEntry entry = seek_table.Entries()[0];
    // Chunk spans all of the input data
    EXPECT_EQ(entry.decompressed_offset, 0ul);
    EXPECT_EQ(entry.decompressed_size, len);
    // Starts after the header.
    EXPECT_EQ(entry.compressed_offset, 64ul);
    EXPECT_EQ(entry.compressed_offset + entry.compressed_size, compressed_len);
  }
}
}  // namespace chunked_compression
