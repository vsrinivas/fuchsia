// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "binary_reader.h"

#include <lib/acpi_lite.h>
#include <lib/zx/status.h>
#include <string.h>

#include <initializer_list>
#include <memory>

#include <gtest/gtest.h>

namespace acpi_lite {
namespace {

struct Header {
  uint32_t length;

  uint32_t size() const { return length; }
} __PACKED;

struct Payload {
  Header header;
  uint32_t payload;

  uint32_t size() const { return header.length; }
} __PACKED;

TEST(BinaryReader, Empty) {
  BinaryReader reader{};
  EXPECT_TRUE(reader.empty());
  EXPECT_EQ(reader.ReadFixedLength<uint8_t>(), nullptr);
  EXPECT_EQ(reader.Read<Header>(), nullptr);
  EXPECT_TRUE(reader.SkipBytes(0));
  EXPECT_FALSE(reader.SkipBytes(1));
}

TEST(BinaryReader, ReadStruct) {
  Payload payload = {
      .header =
          {
              .length = sizeof(Payload),
          },
      .payload = 42,
  };

  // Ensure we can read the full struct.
  BinaryReader reader(&payload, sizeof(payload));
  EXPECT_EQ(reader.Read<Payload>()->payload, 42u);

  // Ensure we can't read the struct if they is insufficent bytes.
  reader = BinaryReader(&payload, sizeof(payload) - 1);
  EXPECT_EQ(reader.Read<Payload>(), nullptr);
}

TEST(BinaryReader, SkipBytes) {
  Payload payload = {
      .header =
          {
              .length = sizeof(Payload),
          },
      .payload = 42,
  };

  // Seek past the header to the payload.
  BinaryReader reader(&payload, sizeof(payload));
  ASSERT_TRUE(reader.SkipBytes(sizeof(Header)));

  // Read the payload.
  EXPECT_EQ(reader.ReadFixedLength<Packed<uint32_t>>()->value, 42u);

  // Can't skip any more.
  EXPECT_FALSE(reader.SkipBytes(1));
  EXPECT_TRUE(reader.empty());
}

}  // namespace
}  // namespace acpi_lite
