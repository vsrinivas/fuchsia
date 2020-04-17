// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <src/lib/chunked-compression/chunked-compressor.h>
#include <src/lib/chunked-compression/chunked-decompressor.h>
#include <src/lib/chunked-compression/status.h>
#include <zxtest/zxtest.h>

namespace chunked_compression {
namespace {

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

TEST(ChunkedDecompressorTest, Decompress_EmptyArchive) {
  fbl::Array<uint8_t> buf(new uint8_t[16], 16);
  HeaderWriter writer(buf.get(), buf.size(), 0);
  ASSERT_EQ(writer.Finalize(), kStatusOk);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(
      ChunkedDecompressor::DecompressBytes(buf.get(), buf.size(), &out_buf, &decompressed_size),
      kStatusOk);
  EXPECT_EQ(decompressed_size, 0ul);
}

TEST(ChunkedDecompressorTest, Decompress_SingleFrame_Zeroes) {
  size_t len = 8192ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(ChunkedDecompressor::DecompressBytes(compressed_data.get(), compressed_len, &out_buf,
                                                 &decompressed_size),
            kStatusOk);
  EXPECT_EQ(decompressed_size, 8192l);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), len);
}

TEST(ChunkedDecompressorTest, Decompress_SingleFrame_Random) {
  size_t len = 8192ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(ChunkedDecompressor::DecompressBytes(compressed_data.get(), compressed_len, &out_buf,
                                                 &decompressed_size),
            kStatusOk);
  EXPECT_EQ(decompressed_size, 8192l);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), len);
}

TEST(ChunkedDecompressorTest, Decompress_MultiFrame_Zeroes) {
  // 3 data frames, last one partial
  size_t len = (2 * CompressionParams::MinChunkSize()) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(ChunkedDecompressor::DecompressBytes(compressed_data.get(), compressed_len, &out_buf,
                                                 &decompressed_size),
            kStatusOk);
  EXPECT_EQ(decompressed_size, len);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), len);
}

TEST(ChunkedDecompressorTest, Decompress_MultiFrame_Random) {
  // 3 data frames, last one partial
  size_t len = (2 * CompressionParams::MinChunkSize()) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(ChunkedDecompressor::DecompressBytes(compressed_data.get(), compressed_len, &out_buf,
                                                 &decompressed_size),
            kStatusOk);
  EXPECT_EQ(decompressed_size, len);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), len);
}

TEST(ChunkedDecompressorTest, DecompressFrame_MultiFrame_Zeroes) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 3 data frames, last one partial
  size_t len = (2 * chunk_size) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  memset(data.get(), 0x00, len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  // Frame 0
  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  size_t bytes_written;
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, chunk_size);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), bytes_written);

  // Frame 1
  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;
  EXPECT_EQ(decompressor.DecompressFrame(table, 1, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, chunk_size);
  EXPECT_BYTES_EQ(data.get() + chunk_size, out_buf.get(), bytes_written);

  // Frame 2
  frame_start = compressed_data.get() + table.Entries()[2].compressed_offset;
  frame_length = table.Entries()[2].compressed_size;
  EXPECT_EQ(decompressor.DecompressFrame(table, 2, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, 42ul);
  EXPECT_BYTES_EQ(data.get() + (2 * chunk_size), out_buf.get(), bytes_written);
}

TEST(ChunkedDecompressorTest, DecompressFrame_MultiFrame_Random) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 3 data frames, last one partial
  size_t len = (2 * chunk_size) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  // Frame 0
  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  size_t bytes_written;
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, chunk_size);
  EXPECT_BYTES_EQ(data.get(), out_buf.get(), bytes_written);

  // Frame 1
  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;
  EXPECT_EQ(decompressor.DecompressFrame(table, 1, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, chunk_size);
  EXPECT_BYTES_EQ(data.get() + chunk_size, out_buf.get(), bytes_written);

  // Frame 2
  frame_start = compressed_data.get() + table.Entries()[2].compressed_offset;
  frame_length = table.Entries()[2].compressed_size;
  EXPECT_EQ(decompressor.DecompressFrame(table, 2, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);

  EXPECT_EQ(bytes_written, 42ul);
  EXPECT_BYTES_EQ(data.get() + (2 * chunk_size), out_buf.get(), bytes_written);
}

TEST(ChunkedDecompressorTest, DecompressFrame_BadFrameNumber) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 3 data frames, last one partial
  size_t len = (2 * chunk_size) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  size_t bytes_written;
  EXPECT_EQ(decompressor.DecompressFrame(table, 3, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrInvalidArgs);
}

TEST(ChunkedDecompressorTest, DecompressFrame_BadOffset) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 3 data frames, last one partial
  size_t len = (2 * chunk_size) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  size_t bytes_written;
  // Starting at 1 byte past the actual frame start.
  // This looks like a corrupt frame to the decompressor.
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start + 1, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrIoDataIntegrity);
  // Starting at 1 byte before the actual frame start.
  // This looks like a corrupt frame to the decompressor.
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start - 1, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrIoDataIntegrity);
}

TEST(ChunkedDecompressorTest, DecompressFrame_SmallBuffer) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 3 data frames, last one partial
  size_t len = (2 * chunk_size) + 42ul;
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  fbl::Array<uint8_t> compressed_data;
  size_t compressed_len;
  ASSERT_EQ(ChunkedCompressor::CompressBytes(data.get(), len, &compressed_data, &compressed_len),
            kStatusOk);
  ASSERT_GE(compressed_data.size(), compressed_len);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  size_t bytes_written;
  // Compressed buffer 1 byte too small
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length - 1, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrBufferTooSmall);
  // Decompressed buffer 1 byte too small
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size() - 1, &bytes_written),
            kStatusErrBufferTooSmall);
}

TEST(ChunkedDecompressorTest, DecompressFrame_CorruptFirstByteInFrame_Checksum) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 2 data frames
  size_t len = (2 * chunk_size);
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  CompressionParams params;
  params.frame_checksum = true;
  ChunkedCompressor compressor(params);

  size_t compressed_limit = compressor.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  size_t compressed_len;
  ASSERT_EQ(compressor.Compress(data.get(), len, compressed_data.get(), compressed_limit,
                                &compressed_len),
            kStatusOk);
  ASSERT_LE(compressed_len, compressed_limit);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  // Invert the first byte of the first frame.
  frame_start[0] ^= 0xff;

  size_t bytes_written;
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrIoDataIntegrity);

  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;

  // Second frame still decompresses.
  EXPECT_EQ(decompressor.DecompressFrame(table, 1, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);
}

TEST(ChunkedDecompressorTest, DecompressFrame_CorruptLastByteInFrame_Checksum) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 2 data frames
  size_t len = (2 * chunk_size);
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  CompressionParams params;
  params.frame_checksum = true;
  ChunkedCompressor compressor(params);

  size_t compressed_limit = compressor.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  size_t compressed_len;
  ASSERT_EQ(compressor.Compress(data.get(), len, compressed_data.get(), compressed_limit,
                                &compressed_len),
            kStatusOk);
  ASSERT_LE(compressed_len, compressed_limit);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  // Invert the last byte of the first frame.
  frame_start[frame_length - 1] ^= 0xff;

  size_t bytes_written;
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrIoDataIntegrity);

  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;

  // Second frame still decompresses.
  EXPECT_EQ(decompressor.DecompressFrame(table, 1, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);
}

TEST(ChunkedDecompressorTest, DecompressFrame_CorruptFirstByteInFrame_NoChecksum) {
  size_t chunk_size = CompressionParams::MinChunkSize();
  // 2 data frames
  size_t len = (2 * chunk_size);
  fbl::Array<uint8_t> data(new uint8_t[len], len);
  RandomFill(data.get(), len);

  CompressionParams params;
  ChunkedCompressor compressor(params);

  size_t compressed_limit = compressor.ComputeOutputSizeLimit(len);
  fbl::Array<uint8_t> compressed_data(new uint8_t[compressed_limit], compressed_limit);
  size_t compressed_len;
  ASSERT_EQ(compressor.Compress(data.get(), len, compressed_data.get(), compressed_limit,
                                &compressed_len),
            kStatusOk);
  ASSERT_LE(compressed_len, compressed_limit);

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  ASSERT_OK(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table));

  ChunkedDecompressor decompressor;

  uint8_t* frame_start = compressed_data.get() + table.Entries()[0].compressed_offset;
  size_t frame_length = table.Entries()[0].compressed_size;

  // Invert the first byte of the first frame.
  frame_start[0] ^= 0xff;

  size_t bytes_written;
  // Even though we have no checksum, this should always be detected as corruption, because
  // ZSTD will not be able to interpret the frame header.
  EXPECT_EQ(decompressor.DecompressFrame(table, 0, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusErrIoDataIntegrity);

  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;

  // Second frame still decompresses.
  EXPECT_EQ(decompressor.DecompressFrame(table, 1, frame_start, frame_length, out_buf.get(),
                                         out_buf.size(), &bytes_written),
            kStatusOk);
}

}  // namespace chunked_compression
