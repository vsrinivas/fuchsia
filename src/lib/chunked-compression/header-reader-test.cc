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

TEST(HeaderReader, ZeroState) {
  SeekTable header;
  EXPECT_EQ(header.DecompressedSize(), 0ul);
  EXPECT_EQ(header.Entries().size(), 0ul);
  // Headers occupies a minimum of 16 bytes.
  EXPECT_EQ(header.CompressedSize(), 16ul);
  EXPECT_EQ(header.SerializedHeaderSize(), 16ul);
}

TEST(HeaderReader, Parse_BadArgs) {
  HeaderReader reader;
  SeekTable header;
  ASSERT_EQ(reader.Parse(nullptr, 0ul, 0ul, &header), kStatusErrInvalidArgs);
  uint8_t buf[15];
  ASSERT_EQ(reader.Parse(buf, sizeof(buf), sizeof(buf), &header), kStatusErrBufferTooSmall);
  uint8_t buf2[16];
  ASSERT_EQ(reader.Parse(buf2, sizeof(buf2), sizeof(buf2), nullptr), kStatusErrInvalidArgs);
}

TEST(HeaderReader, Parse_Empty) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader();

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), buf.size(), &header), kStatusOk);

  EXPECT_EQ(header.DecompressedSize(), 0ul);
  EXPECT_EQ(header.Entries().size(), 0ul);
  // Headers occupies a minimum of 16 bytes.
  EXPECT_EQ(header.CompressedSize(), 16ul);
  EXPECT_EQ(header.SerializedHeaderSize(), 16ul);
}

TEST(HeaderReader, Parse_OneEntry) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
      .decompressed_offset = 0ul,
      .decompressed_size = 256ul,
      .compressed_offset = 48ul,
      .compressed_size = 112ul,
  }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 160ul, &header), kStatusOk);

  EXPECT_EQ(header.CompressedSize(), 160ul);
  EXPECT_EQ(header.DecompressedSize(), 256ul);
  EXPECT_EQ(header.SerializedHeaderSize(), 48ul);
  ASSERT_EQ(header.Entries().size(), 1ul);

  const SeekTableEntry& entry = header.Entries()[0];
  ASSERT_EQ(entry.decompressed_offset, 0ul);
  ASSERT_EQ(entry.decompressed_size, 256ul);
  ASSERT_EQ(entry.compressed_offset, 48ul);
  ASSERT_EQ(entry.compressed_size, 112ul);
}

TEST(HeaderReader, Parse_TwoEntries) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf =
      CreateHeader({{
                        .decompressed_offset = 0ul,
                        .decompressed_size = 256ul,
                        .compressed_offset = 80ul,
                        .compressed_size = 120ul,
                    },
                    {
                        .decompressed_offset = 256ul,
                        .decompressed_size = 100ul,
                        // Note the compressed frames are non-contiguous (the second starts at 2000)
                        .compressed_offset = 2000ul,
                        .compressed_size = 40ul,
                    }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 2040ul, &header), kStatusOk);

  // Compressed size should be the end of the last frame.
  EXPECT_EQ(header.CompressedSize(), 2040ul);
  EXPECT_EQ(header.DecompressedSize(), 356ul);
  EXPECT_EQ(header.SerializedHeaderSize(), 80ul);
  ASSERT_EQ(header.Entries().size(), 2ul);

  const SeekTableEntry& entry1 = header.Entries()[0];
  ASSERT_EQ(entry1.decompressed_offset, 0ul);
  ASSERT_EQ(entry1.decompressed_size, 256ul);
  ASSERT_EQ(entry1.compressed_offset, 80ul);
  ASSERT_EQ(entry1.compressed_size, 120ul);
  const SeekTableEntry& entry2 = header.Entries()[1];
  ASSERT_EQ(entry2.decompressed_offset, 256ul);
  ASSERT_EQ(entry2.decompressed_size, 100ul);
  ASSERT_EQ(entry2.compressed_offset, 2000ul);
  ASSERT_EQ(entry2.compressed_size, 40ul);
}

TEST(HeaderReader, Parse_BadMagic) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader();
  // Bit flip the first byte in the archive.
  buf[0] ^= 0xff;

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), buf.size(), &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_BadVersion) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader();
  reinterpret_cast<ArchiveVersionType*>(buf.get() + kChunkArchiveVersionOffset)[0] = 2u;

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), buf.size(), &header), kStatusErrInvalidArgs);
}

// Parse_Invalid_I* tests verify the invariants documented in the header during parsing.

TEST(HeaderReader, Parse_Invalid_I0_DecompressedDataStartsAbove0) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
      .decompressed_offset = 1ul,
      .decompressed_size = 255ul,
      .compressed_offset = 48ul,
      .compressed_size = 112ul,
  }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 160ul, &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_Invalid_I1_CompressedDataOverlapsHeader) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
      .decompressed_offset = 0ul,
      .decompressed_size = 256ul,
      // Header ends at byte 48
      .compressed_offset = 47ul,
      .compressed_size = 112ul,
  }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 160ul, &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_Invalid_I2_NonContigDecompressedFrames) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
                                              .decompressed_offset = 0ul,
                                              .decompressed_size = 256ul,
                                              .compressed_offset = 80ul,
                                              .compressed_size = 2ul,
                                          },
                                          {
                                              // Gap between frames
                                              .decompressed_offset = 257ul,
                                              .decompressed_size = 99ul,
                                              .compressed_offset = 82ul,
                                              .compressed_size = 18ul,
                                          }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 100ul, &header), kStatusErrIoDataIntegrity);

  buf = CreateHeader({{
                          .decompressed_offset = 0ul,
                          .decompressed_size = 256ul,
                          .compressed_offset = 80ul,
                          .compressed_size = 2ul,
                      },
                      {
                          // Overlap between frames
                          .decompressed_offset = 255ul,
                          .decompressed_size = 101ul,
                          .compressed_offset = 82ul,
                          .compressed_size = 100ul,
                      }});

  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 100ul, &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_Invalid_I3_OverlappingCompressedFrames) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
                                              .decompressed_offset = 0ul,
                                              .decompressed_size = 256ul,
                                              .compressed_offset = 80ul,
                                              .compressed_size = 20ul,
                                          },
                                          {
                                              .decompressed_offset = 256ul,
                                              .decompressed_size = 100ul,
                                              // Overlap between frames
                                              .compressed_offset = 99ul,
                                              .compressed_size = 2ul,
                                          }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 101ul, &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_Invalid_I4_ZeroLengthFrames) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
      .decompressed_offset = 0ul,
      // Zero-length decompressed frame
      .decompressed_size = 0ul,
      .compressed_offset = 48ul,
      .compressed_size = 52ul,
  }});

  SeekTable header;
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 100ul, &header), kStatusErrIoDataIntegrity);

  buf = CreateHeader({{
      .decompressed_offset = 0ul,
      .decompressed_size = 100ul,
      .compressed_offset = 48ul,
      // Zero-length compressed frame
      .compressed_size = 0ul,
  }});

  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 48ul, &header), kStatusErrIoDataIntegrity);
}

TEST(HeaderReader, Parse_Invalid_I5_CompressedFrameExceedsFile) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf = CreateHeader({{
      .decompressed_offset = 0ul,
      .decompressed_size = 256ul,
      .compressed_offset = 48ul,
      .compressed_size = 52ul,
  }});

  SeekTable header;
  // File claims to be 50 bytes long, but the compressed frame goes from [48, 100)
  ASSERT_EQ(reader.Parse(buf.get(), buf.size(), 90ul, &header), kStatusErrIoDataIntegrity);
}

}  // namespace chunked_compression
