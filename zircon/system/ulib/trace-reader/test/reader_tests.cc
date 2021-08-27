// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "reader_tests.h"

#include <stdint.h>

#include <iterator>

#include <fbl/vector.h>
#include <trace-reader/reader.h>
#include <zxtest/zxtest.h>

namespace trace {
namespace {

TEST(TraceReader, NonEmptyChunk) {
  uint64_t kData[] = {
      // uint64 values
      0,
      UINT64_MAX,
      // int64 values
      test::ToWord(INT64_MIN),
      test::ToWord(INT64_MAX),
      // double values
      test::ToWord(1.5),
      test::ToWord(-3.14),
      // string values (will be filled in)
      0,
      0,
      // sub-chunk values
      123,
      456,
      // more stuff beyond sub-chunk
      789,
  };
  memcpy(kData + 6, "Hello World!----", 16);

  trace::Chunk chunk(kData, std::size(kData));
  EXPECT_EQ(std::size(kData), chunk.remaining_words());

  {
    std::optional value = chunk.ReadUint64();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(0, value.value());
    EXPECT_EQ(10u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadUint64();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(UINT64_MAX, value.value());
    EXPECT_EQ(9u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadInt64();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(INT64_MIN, value.value());
    EXPECT_EQ(8u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadInt64();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(INT64_MAX, value.value());
    EXPECT_EQ(7u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadDouble();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(1.5, value.value());
    EXPECT_EQ(6u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadDouble();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(-3.14, value.value());
    EXPECT_EQ(5u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadString(0);
    EXPECT_TRUE(value.has_value());
    EXPECT_TRUE(value.value().empty());
    EXPECT_EQ(5u, chunk.remaining_words());
  }

  {
    std::optional value = chunk.ReadString(12);
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(12, value.value().length());
    EXPECT_EQ(reinterpret_cast<const char*>(kData + 6), value.value().data());
    EXPECT_TRUE(fbl::String(value.value()) == "Hello World!");
    EXPECT_EQ(3u, chunk.remaining_words());
  }

  {
    std::optional subchunk = chunk.ReadChunk(2);
    EXPECT_TRUE(subchunk.has_value());
    EXPECT_EQ(2u, subchunk.value().remaining_words());

    {
      std::optional value = subchunk.value().ReadUint64();
      EXPECT_TRUE(value.has_value());
      EXPECT_EQ(123, value.value());
      EXPECT_EQ(1u, subchunk.value().remaining_words());
    }

    {
      std::optional value = chunk.ReadUint64();
      EXPECT_TRUE(value.has_value());
      EXPECT_EQ(789, value.value());
      EXPECT_EQ(0u, chunk.remaining_words());
    }

    {
      std::optional value = subchunk.value().ReadUint64();
      EXPECT_TRUE(value.has_value());
      EXPECT_EQ(456, value.value());
      EXPECT_EQ(0u, subchunk.value().remaining_words());
    }

    {
      EXPECT_FALSE(subchunk.value().ReadUint64().has_value());
      EXPECT_FALSE(chunk.ReadUint64().has_value());
    }
  }
}

TEST(TraceReader, InitialState) {
  fbl::Vector<trace::Record> records;
  fbl::String error;
  trace::TraceReader reader(test::MakeRecordConsumer(&records), test::MakeErrorHandler(&error));

  EXPECT_EQ(0, reader.current_provider_id());
  EXPECT_TRUE(reader.current_provider_name() == "");
  EXPECT_TRUE(reader.GetProviderName(0) == "");
  EXPECT_EQ(0, records.size());
  EXPECT_TRUE(error.empty());
}

// NOTE: Most of the reader is covered by the libtrace tests.

}  // namespace
}  // namespace trace
