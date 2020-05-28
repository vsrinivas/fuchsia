// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/array.h>
#include <src/lib/chunked-compression/chunked-archive.h>
#include <zxtest/zxtest.h>

#include "test-utils.h"

namespace chunked_compression {

TEST(HeaderWriter, ZeroState) {
  size_t sz = kChunkArchiveMinHeaderSize;
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 0, &writer), kStatusOk);

  ASSERT_EQ(writer.Finalize(), kStatusOk);

  SeekTable header;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), buf.size(), &header), kStatusOk);
  EXPECT_EQ(header.DecompressedSize(), 0ul);
  EXPECT_EQ(header.Entries().size(), 0ul);
  EXPECT_EQ(header.CompressedSize(), sz);
  EXPECT_EQ(header.SerializedHeaderSize(), sz);
}

TEST(HeaderWriter, OneEntry) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);

  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 100,
            }),
            kStatusOk);
  ASSERT_EQ(writer.Finalize(), kStatusOk);

  SeekTable header;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), sz + 100, &header), kStatusOk);
  EXPECT_EQ(header.CompressedSize(), sz + 100);
  EXPECT_EQ(header.DecompressedSize(), 256ul);
  EXPECT_EQ(header.SerializedHeaderSize(), sz);
  ASSERT_EQ(header.Entries().size(), 1ul);

  const SeekTableEntry& entry = header.Entries()[0];
  ASSERT_EQ(entry.decompressed_offset, 0ul);
  ASSERT_EQ(entry.decompressed_size, 256ul);
  ASSERT_EQ(entry.compressed_offset, sz);
  ASSERT_EQ(entry.compressed_size, 100ul);
}

TEST(HeaderWriter, TwoEntries) {
  size_t sz = kChunkArchiveMinHeaderSize + 2 * sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 2, &writer), kStatusOk);

  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 120ul,
            }),
            kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 256ul,
                .decompressed_size = 100ul,
                // Note the compressed frames are non-contiguous (the second starts at 2000)
                .compressed_offset = 2000ul,
                .compressed_size = 40ul,
            }),
            kStatusOk);
  ASSERT_EQ(writer.Finalize(), kStatusOk);

  SeekTable header;
  HeaderReader reader;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 2040ul, &header), kStatusOk);

  // Compressed size should be the end of the last frame.
  EXPECT_EQ(header.CompressedSize(), 2040ul);
  EXPECT_EQ(header.DecompressedSize(), 356ul);
  EXPECT_EQ(header.SerializedHeaderSize(), sz);
  ASSERT_EQ(header.Entries().size(), 2ul);

  const SeekTableEntry& entry1 = header.Entries()[0];
  ASSERT_EQ(entry1.decompressed_offset, 0ul);
  ASSERT_EQ(entry1.decompressed_size, 256ul);
  ASSERT_EQ(entry1.compressed_offset, sz);
  ASSERT_EQ(entry1.compressed_size, 120ul);
  const SeekTableEntry& entry2 = header.Entries()[1];
  ASSERT_EQ(entry2.decompressed_offset, 256ul);
  ASSERT_EQ(entry2.decompressed_size, 100ul);
  ASSERT_EQ(entry2.compressed_offset, 2000ul);
  ASSERT_EQ(entry2.compressed_size, 40ul);
}

TEST(HeaderWriter, MaxEntries) {
  size_t sz = HeaderWriter::MetadataSizeForNumFrames(kChunkArchiveMaxFrames);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), kChunkArchiveMaxFrames, &writer),
            kStatusOk);
}

TEST(HeaderWriter, FinalizeCalledEarly) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);

  ASSERT_EQ(writer.Finalize(), kStatusErrBadState);
}

TEST(HeaderWriter, TooManyEntriesWritten) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);

  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 112ul,
            }),
            kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 256ul,
                .decompressed_size = 100ul,
                .compressed_offset = 2000ul,
                .compressed_size = 40ul,
            }),
            kStatusErrBadState);
}

// Write_Invalid_I* tests verify the invariants documented in the header during writing.

TEST(HeaderWriter, Write_Invalid_I0_DecompressedDataStartsAbove0) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 1ul,
                .decompressed_size = 255ul,
                .compressed_offset = sz,
                .compressed_size = 112ul,
            }),
            kStatusErrInvalidArgs);
}

TEST(HeaderWriter, Write_Invalid_I1_CompressedDataOverlapsHeader) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz - 1ul,
                .compressed_size = 112ul,
            }),
            kStatusErrInvalidArgs);
}

TEST(HeaderWriter, Write_Invalid_I2_NonContigDecompressedFrames) {
  size_t sz = kChunkArchiveMinHeaderSize + 2 * sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 2, &writer), kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 2ul,
            }),
            kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                // Gap between frames
                .decompressed_offset = 257ul,
                .decompressed_size = 99ul,
                .compressed_offset = sz + 2ul,
                .compressed_size = 10ul,
            }),
            kStatusErrInvalidArgs);
}

TEST(HeaderWriter, Write_Invalid_I3_NonMonotonicCompressedFrames) {
  size_t sz = kChunkArchiveMinHeaderSize + 2 * sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 2, &writer), kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 100ul,
            }),
            kStatusOk);
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 256ul,
                .decompressed_size = 100ul,
                .compressed_offset = sz + 99ul,
                .compressed_size = 2ul,
            }),
            kStatusErrInvalidArgs);
}

TEST(HeaderWriter, Write_Invalid_I4_ZeroLengthFrames) {
  size_t sz = kChunkArchiveMinHeaderSize + sizeof(SeekTableEntry);
  fbl::Array<uint8_t> buf(new uint8_t[sz], sz);
  HeaderWriter writer;
  ASSERT_EQ(HeaderWriter::Create(buf.get(), buf.size(), 1, &writer), kStatusOk);

  // Decompressed frame
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 0ul,
                .compressed_offset = sz,
                .compressed_size = 52ul,
            }),
            kStatusErrInvalidArgs);

  // Compressed frame
  ASSERT_EQ(writer.AddEntry({
                .decompressed_offset = 0ul,
                .decompressed_size = 256ul,
                .compressed_offset = sz,
                .compressed_size = 0ul,
            }),
            kStatusErrInvalidArgs);
}

}  // namespace chunked_compression
