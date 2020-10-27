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
  srand(zxtest::Runner::GetInstance()->random_seed());
  fbl::Array<uint8_t> buf(new uint8_t[kChunkArchiveMinHeaderSize], kChunkArchiveMinHeaderSize);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 0, &writer), kStatusOk);
  ASSERT_EQ(writer.Finalize(), kStatusOk);

  fbl::Array<uint8_t> out_buf;
  size_t decompressed_size;
  ASSERT_EQ(
      ChunkedDecompressor::DecompressBytes(buf.get(), buf.size(), &out_buf, &decompressed_size),
      kStatusOk);
  EXPECT_EQ(decompressed_size, 0ul);
}

TEST(ChunkedDecompressorTest, Decompress_SingleFrame_Zeroes) {
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  srand(zxtest::Runner::GetInstance()->random_seed());
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

TEST(ChunkedDecompressorTest, Decompress_CorruptHeader) {
  srand(zxtest::Runner::GetInstance()->random_seed());
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

  // Invert a random byte in the header.
  size_t header_len = HeaderWriter::MetadataSizeForNumFrames(2);
  compressed_data[rand() % header_len] ^= 0xff;

  fbl::Array<uint8_t> out_buf(new uint8_t[chunk_size], chunk_size);

  SeekTable table;
  HeaderReader reader;
  // We check for *inequality* with kStatusOk, because some types of corruptions may be
  // signalled differently. (For example if the number of frames reported by the header was
  // corrupted, the library can't distinguish between the client passing too small of a header,
  // or the header being corrupted).
  EXPECT_NE(reader.Parse(compressed_data.get(), compressed_len, compressed_len, &table), kStatusOk);
}

TEST(ChunkedDecompressorTest, RawDecompressFrame_WrongDecompressionLength) {
  srand(zxtest::Runner::GetInstance()->random_seed());
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
  size_t output_len = table.Entries()[0].decompressed_size;

  size_t bytes_written;
  // First frame uses the correct table outputs.
  EXPECT_EQ(decompressor.DecompressFrame(frame_start, frame_length, out_buf.get(),
                                         output_len, &bytes_written),
            kStatusOk);

  frame_start = compressed_data.get() + table.Entries()[1].compressed_offset;
  frame_length = table.Entries()[1].compressed_size;
  size_t wrong_output_len = table.Entries()[1].decompressed_size + 1;

  // Second frame takes an incorrect table output, so we cannot verify that the entire frame was
  // decompressed as expected.
  EXPECT_EQ(decompressor.DecompressFrame(frame_start, frame_length, out_buf.get(),
                                         wrong_output_len, &bytes_written),
            kStatusErrIoDataIntegrity);
}

}  // namespace chunked_compression
