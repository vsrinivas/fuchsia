// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/uuid/uuid.h"

#include <stdint.h>

#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <re2/re2.h>

namespace uuid {
namespace {

// Ensure the empty UUID has the correct form.
TEST(Uuid, Empty) {
  const char kExpectedEmpty[] = "00000000-0000-0000-0000-000000000000";

  Uuid empty;
  EXPECT_EQ(empty.ToString(), kExpectedEmpty);

  std::optional<Uuid> parsed_empty = Uuid::FromString(std::string_view(kExpectedEmpty));
  EXPECT_NE(parsed_empty, std::nullopt);
  EXPECT_EQ(*parsed_empty, empty);

  std::ostringstream out;
  out << empty;
  EXPECT_EQ(out.str(), kExpectedEmpty);
}

TEST(Uuid, Equality) {
  // Two empty UUIDs are equal.
  EXPECT_EQ(Uuid(), Uuid());

  // Two generated UUIDs should not be equal.
  EXPECT_NE(Uuid::Generate(), Uuid::Generate());
}

TEST(Uuid, FromRaw) {
  Uuid a = Uuid::Generate();

  // Get raw UUID fields. Should still be equal.
  RawUuid raw = a.raw();
  EXPECT_EQ(a, Uuid(raw));

  // Tweak one of the raw fields; we should no longer be equal.
  raw.time_mid++;
  EXPECT_NE(a, Uuid(raw));
}

TEST(Uuid, EqualFromBytes) {
  // Generate a UUID, and copy it via its bytes array.
  Uuid a = Uuid::Generate();
  Uuid b = Uuid(a.bytes());
  EXPECT_EQ(a, b);
}

// Ensure that UUIDs are somewhat unique.
TEST(Uuid, Unique) {
  std::unordered_set<std::string> seen_uuids;

  // Ensure that if we generate 256 UUIDs, none are the same.
  for (int i = 0; i < 256; ++i) {
    // Generate a UUID, and ensure we haven't already seen it.
    std::string n = Uuid::Generate().ToString();
    EXPECT_TRUE(seen_uuids.find(n) == seen_uuids.end());

    seen_uuids.insert(n);
  }
}

// Ensure that our generated UUIDs in their string format have the
// correct version set and reserved bits set.
TEST(Uuid, Version4) {
  // The format of UUID version 4 must be xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx,
  // where y is one of [8, 9, A, B].
  re2::RE2 uuid_v4("^........-....-4...-[89ab]...-............$");

  // Test a few random UUIDs.
  for (int i = 0; i < 10; ++i) {
    std::string n = uuid::Generate();
    EXPECT_TRUE(re2::RE2::FullMatch(n, uuid_v4))
        << "UUID '" << n << "' did not match expected template.";
  }
}

// Ensure that the byte/string representations of UUIDs match known-good values.
TEST(Uuid, ToStringLittleEndian) {
  if constexpr (BYTE_ORDER != LITTLE_ENDIAN) {
    printf("Skipped.\n");
    return;
  }

  // GPT EFI GUID.
  {
    Uuid uuid = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
    const char kExpected[] = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

    EXPECT_EQ(kExpected, uuid.ToString());

    std::ostringstream out;
    out << uuid;
    EXPECT_EQ(kExpected, out.str());
  }

  // Chrome OS.
  {
    Uuid uuid = {0x5d, 0x2a, 0x3a, 0xfe, 0x32, 0x4f, 0xa7, 0x41,
                 0xb7, 0x25, 0xac, 0xcc, 0x32, 0x85, 0xa3, 0x09};
    const char kExpected[] = "fe3a2a5d-4f32-41a7-b725-accc3285a309";

    EXPECT_EQ(kExpected, uuid.ToString());

    std::ostringstream out;
    out << uuid;
    EXPECT_EQ(kExpected, out.str());
  }
}

TEST(Uuid, FromStringLittleEndian) {
  if constexpr (BYTE_ORDER != LITTLE_ENDIAN) {
    printf("Skipped.\n");
    return;
  }

  // GPT EFI GUID.
  {
    const Uuid kExpected = {0x28, 0x73, 0x2a, 0xc1, 0x1f, 0xf8, 0xd2, 0x11,
                            0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b};
    const char kString[] = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";

    auto parsed = Uuid::FromString(kString);
    EXPECT_NE(parsed, std::nullopt);
    EXPECT_EQ(*parsed, kExpected);
  }

  // Chrome OS.
  {
    const Uuid kExpected = {0x5d, 0x2a, 0x3a, 0xfe, 0x32, 0x4f, 0xa7, 0x41,
                            0xb7, 0x25, 0xac, 0xcc, 0x32, 0x85, 0xa3, 0x09};
    const char kString[] = "fe3a2a5d-4f32-41a7-b725-accc3285a309";

    auto parsed = Uuid::FromString(kString);
    EXPECT_NE(parsed, std::nullopt);
    EXPECT_EQ(*parsed, kExpected);
  }
}

TEST(Uuid, FromStringTooShort) {
  const char kString[] = "12345678-";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

TEST(Uuid, FromStringFieldsWrongSize) {
  const char kString[] = "123456-789123-1234-1234-123456789abc";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

TEST(Uuid, FromStringNotEnoughFields) {
  const char kString[] = "fe3a2a5d-4f32-41a7-b725aaccc3285a309";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

TEST(Uuid, FromStringTooManyFields) {
  const char kString[] = "fe3a2a5d-4f32-41a7-b725-accc38-5a309";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

TEST(Uuid, FromStringLeadingJunkRejected) {
  const char kString[] = "not a uuidfe3a2a5d-4f32-41a7-b725aaccc3285a309";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

TEST(Uuid, FromStringTrailingJunkRejected) {
  const char kString[] = "fe3a2a5d-4f32-41a7-b725aaccc3285a309trailing data";
  EXPECT_EQ(Uuid::FromString(kString), std::nullopt);
}

// Ensure that UUIDs produced by Generate() pass IsValid() and IsValidOutputString().
TEST(Uuid, GeneratedIsValid) {
  for (int i = 0; i < 256; ++i) {
    auto uuid = uuid::Generate();
    EXPECT_TRUE(uuid::IsValid(uuid));
    EXPECT_TRUE(uuid::IsValidOutputString(uuid));
  }
}

}  // namespace
}  // namespace uuid
