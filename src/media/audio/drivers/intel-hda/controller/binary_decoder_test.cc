// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binary_decoder.h"

#include <zircon/errors.h>

#include <array>
#include <cstdint>
#include <vector>

#include <zxtest/zxtest.h>

namespace audio::intel_hda {
namespace {

TEST(BinaryDecoder, Empty) {
  BinaryDecoder decoder{cpp20::span<uint8_t>()};

  // Empty read.
  auto empty_read = decoder.Read(0);
  ASSERT_TRUE(empty_read.is_ok());
  EXPECT_EQ(empty_read.value().size(), 0);

  // Non-empty read.
  ASSERT_EQ(decoder.Read(1).status_value(), ZX_ERR_OUT_OF_RANGE);
}

TEST(BinaryDecoder, NonEmptyRead) {
  std::array<uint8_t, 5> buffer;
  BinaryDecoder decoder{cpp20::span<uint8_t>(buffer.begin(), buffer.size())};

  // Successful read.
  auto a = decoder.Read(1);
  ASSERT_TRUE(a.is_ok());
  EXPECT_EQ(a.value().size(), 1);
  EXPECT_EQ(a.value().begin(), &buffer[0]);

  // Another read.
  auto b = decoder.Read(1);
  ASSERT_TRUE(b.is_ok());
  EXPECT_EQ(b.value().size(), 1);
  EXPECT_EQ(b.value().begin(), &buffer[1]);

  // Too big a read.
  auto c = decoder.Read(4);
  ASSERT_EQ(c.status_value(), ZX_ERR_OUT_OF_RANGE);

  // But we should still be able to read the last three bytes.
  auto d = decoder.Read(3);
  ASSERT_TRUE(d.is_ok());
  EXPECT_EQ(d.value().size(), 3);
  EXPECT_EQ(d.value().begin(), &buffer[2]);
}

TEST(BinaryDecoder, ReadStruct) {
  struct MyStruct {
    char a, b;
  };

  // Can't read from too small a buffer.
  {
    std::array<uint8_t, 1> small_buffer = {1};
    BinaryDecoder decoderA{cpp20::span<uint8_t>(small_buffer.begin(), small_buffer.size())};
    ASSERT_EQ(decoderA.Read<MyStruct>().status_value(), ZX_ERR_OUT_OF_RANGE);
  }

  // We should be able to read from a precisely sized buffer.
  {
    std::array<uint8_t, 2> correct_buffer = {1, 2};

    BinaryDecoder decoderB{cpp20::span<uint8_t>(correct_buffer.begin(), correct_buffer.size())};
    auto value = decoderB.Read<MyStruct>();
    ASSERT_TRUE(value.is_ok());
    EXPECT_EQ(value.value().a, 1);
    EXPECT_EQ(value.value().b, 2);
    EXPECT_FALSE(decoderB.Read(1).is_ok());
  }

  // Reading from the beginning of a buffer is fine too.
  {
    std::array<uint8_t, 3> big_buffer = {1, 2, 3};

    BinaryDecoder decoderC{cpp20::span<uint8_t>(big_buffer.begin(), big_buffer.size())};
    auto value = decoderC.Read<MyStruct>();
    ASSERT_TRUE(value.is_ok());
    EXPECT_EQ(value.value().a, 1);
    EXPECT_EQ(value.value().b, 2);
    EXPECT_TRUE(decoderC.Read(1).is_ok());
  }
}

TEST(BinaryDecoder, ReadIntoPointerSuccess) {
  struct MyStruct {
    char a, b;
  };

  std::array<uint8_t, 2> correct_buffer = {1, 2};
  BinaryDecoder decoderB{cpp20::span<uint8_t>(correct_buffer.begin(), correct_buffer.size())};
  MyStruct value;
  EXPECT_EQ(decoderB.Read<MyStruct>(&value), ZX_OK);
  EXPECT_EQ(value.a, 1);
  EXPECT_EQ(value.b, 2);
}

TEST(BinaryDecoder, ReadIntoPointerFailure) {
  struct MyStruct {
    char a, b;
  };

  std::array<uint8_t, 1> small_buffer = {1};
  BinaryDecoder decoderC{cpp20::span<uint8_t>(small_buffer.begin(), small_buffer.size())};
  MyStruct value;
  EXPECT_NE(decoderC.Read<MyStruct>(&value), ZX_OK);
}

TEST(BinaryDecoder, VarLengthRead) {
  struct VarLength {
    char size;
    char data;
  };

  // Insufficient data available.
  {
    BinaryDecoder d{cpp20::span<uint8_t>()};
    ASSERT_FALSE((d.VariableLengthRead<VarLength>(&VarLength::size).is_ok()));
  }

  // Length is smaller than the header structure.
  {
    std::array<uint8_t, 3> buffer = {/*size=*/1, /*data=*/2, /*payload=*/3};
    BinaryDecoder d{cpp20::span<uint8_t>(buffer.begin(), buffer.size())};
    EXPECT_EQ(d.VariableLengthRead<VarLength>(&VarLength::size).status_value(),
              ZX_ERR_OUT_OF_RANGE);
  }

  // Length is larger than the buffer.
  {
    std::array<uint8_t, 3> buffer = {/*size=*/4, /*data=*/2, /*payload=*/3};
    BinaryDecoder d{cpp20::span<uint8_t>(buffer.begin(), buffer.size())};
    EXPECT_EQ(d.VariableLengthRead<VarLength>(&VarLength::size).status_value(),
              ZX_ERR_OUT_OF_RANGE);
  }

  // Successful read.
  {
    std::array<uint8_t, 3> buffer = {/*size=*/3, /*data=*/2, /*payload=*/1};
    BinaryDecoder d{cpp20::span<uint8_t>(buffer.begin(), buffer.size())};
    auto val_status = d.VariableLengthRead<VarLength>(&VarLength::size);
    ASSERT_TRUE(val_status.is_ok());
    auto [val, payload] = val_status.value();
    EXPECT_EQ(val.size, 3);
    EXPECT_EQ(val.data, 2);
    EXPECT_EQ(payload.size(), 1);
    EXPECT_EQ(payload.begin(), &buffer[2]);
  }
}

TEST(ParseUnpaddedString, Empty) {
  char buff[2] = "";
  EXPECT_EQ("", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, SingleChar) {
  char buff[2] = "A";
  EXPECT_EQ("A", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, ConstChar) {
  const char buff[2] = "A";
  EXPECT_EQ("A", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, FillArray) {
  char buff[2] = {'A', 'A'};
  EXPECT_EQ("AA", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, InvalidDataAfterNul) {
  char buff[10] = "A\0BCDEF";
  EXPECT_EQ("A", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, Uint8Bytes) {
  uint8_t buff[2] = "A";
  EXPECT_EQ("A", ParseUnpaddedString(buff));
}

TEST(ParseUnpaddedString, Uint8BytesFullWidth) {
  uint8_t buff[3] = {'A', 'B', 'C'};
  EXPECT_EQ("ABC", ParseUnpaddedString(buff));
}

}  // namespace
}  // namespace audio::intel_hda
