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
namespace {

using test_utils::CreateHeader;

}  // namespace

TEST(SeekTable, EntryForCompressedOffset) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf =
      CreateHeader({{
                        .decompressed_offset = 0ul,
                        .decompressed_size = 256ul,
                        .compressed_offset = 100,
                        .compressed_size = 100ul,
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

  EXPECT_EQ(header.EntryForCompressedOffset(99ul), std::nullopt);
  EXPECT_EQ(header.EntryForCompressedOffset(100ul), std::optional<unsigned>(0));
  EXPECT_EQ(header.EntryForCompressedOffset(199ul), std::optional<unsigned>(0));
  EXPECT_EQ(header.EntryForCompressedOffset(200ul), std::nullopt);
  EXPECT_EQ(header.EntryForCompressedOffset(1999ul), std::nullopt);
  EXPECT_EQ(header.EntryForCompressedOffset(2000ul), std::optional<unsigned>(1));
  EXPECT_EQ(header.EntryForCompressedOffset(2039ul), std::optional<unsigned>(1));
  EXPECT_EQ(header.EntryForCompressedOffset(2040ul), std::nullopt);
}

TEST(SeekTable, EntryForDecompressedOffset) {
  HeaderReader reader;
  fbl::Array<uint8_t> buf =
      CreateHeader({{
                        .decompressed_offset = 0ul,
                        .decompressed_size = 256ul,
                        .compressed_offset = 100ul,
                        .compressed_size = 100ul,
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

  EXPECT_EQ(header.EntryForDecompressedOffset(0ul), std::optional<unsigned>(0));
  EXPECT_EQ(header.EntryForDecompressedOffset(255ul), std::optional<unsigned>(0));
  EXPECT_EQ(header.EntryForDecompressedOffset(256ul), std::optional<unsigned>(1));
  EXPECT_EQ(header.EntryForDecompressedOffset(355ul), std::optional<unsigned>(1));
  EXPECT_EQ(header.EntryForDecompressedOffset(356ul), std::nullopt);
}

}  // namespace chunked_compression
