// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/view.h>

#include <gtest/gtest.h>

// Meant for fitx::result<std::string_view>.
#define EXPECT_IS_OK(result) \
  EXPECT_TRUE(result.is_ok()) << "unexpected error: " << result.error_value().data()
#define EXPECT_IS_ERROR(result) EXPECT_TRUE(result.is_error())
// Meant for fitx::result<zbitl::View::Error>.
#define EXPECT_VIEW_IS_OK(result) \
  EXPECT_TRUE(result.is_ok()) << "unexpected error: " << result.error_value().zbi_error

namespace {

using ByteView = std::basic_string_view<const uint8_t>;

constexpr uint32_t kKernelType = 1u;
constexpr uint32_t kBootfsType = 2u;
constexpr uint32_t kMiscType = 3u;

static constexpr zbi_header_t kValidHeader = {
    .length = ZBI_ALIGNMENT,
    .flags = ZBI_FLAG_VERSION | ZBI_FLAG_CRC32,
    .magic = ZBI_ITEM_MAGIC,
    .crc32 = 123,
};

inline void CheckTwoItemZbi(uint32_t type1, uint32_t type2, bool expect_ok) {
  constexpr size_t kPayloadSize = ZBI_ALIGNMENT;
  struct TwoItemZbi {
    struct Item {
      alignas(ZBI_ALIGNMENT) zbi_header_t header;
      uint8_t payload[kPayloadSize];
    };
    alignas(ZBI_ALIGNMENT) zbi_header_t header;
    Item items[2];
  };

  const TwoItemZbi contents = {
      .header = ZBI_CONTAINER_HEADER(sizeof(contents.items)),
      .items =
          {
              {
                  .header = zbitl::SanitizeHeader({.type = type1, .length = kPayloadSize}),
              },
              {
                  .header = zbitl::SanitizeHeader({.type = type2, .length = kPayloadSize}),
              },
          },
  };

  ByteView bytes(reinterpret_cast<const uint8_t*>(&contents), sizeof(contents));
  zbitl::View<ByteView> zbi(bytes);
  auto result = zbitl::CheckComplete(zbi, kKernelType, kBootfsType);
  if (expect_ok) {
    EXPECT_IS_OK(result);
  } else {
    EXPECT_IS_ERROR(result);
  }
  EXPECT_VIEW_IS_OK(zbi.take_error());
}

// The set of states of interest here is the product of
//  * kernel item states = { first, present but not first, not present }
// with
//  * bootfs item states = { present, not present }
// Only (first, present) should result in a complete ZBI (all else being
// equal).
TEST(ZbitlCompletenessTest, CompleteZbi) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kKernelType, kBootfsType, true));
}

TEST(ZbitlCompletenessTest, BootfsMissing) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kKernelType, kMiscType, false));
}

TEST(ZbitlCompletenessTest, KernelNotFirst) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kBootfsType, kKernelType, false));
}

TEST(ZbitlCompletenessTest, KernelNotFirstAndBootfsMissing) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kMiscType, kKernelType, false));
}

TEST(ZbitlCompletenessTest, KernelMissing) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kMiscType, kBootfsType, false));
}

TEST(ZbitlCompletenessTest, KernelAndBootfsMissing) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kMiscType, kMiscType, false));
}

TEST(ZbitlHeaderTest, MagicAndFlagsMissing) {
  // * Item fits, but magic, required flags and CRC are unset.
  // Expectation: failure.
  zbi_header_t header = kValidHeader;
  header.flags = 0u;
  header.magic = 0u;
  header.crc32 = 0u;

  EXPECT_IS_ERROR(zbitl::CheckHeader(header));
}

TEST(ZbitlHeaderTest, ValidHeader) {
  // * Item fits, magic is correct, and required flags and CRC are set.
  // Expectation: success.
  EXPECT_IS_OK(zbitl::CheckHeader(kValidHeader));
}

TEST(ZbitlHeaderTest, CrcIsMissing) {
  // * Item fits, magic is correct, required flags are set, and CRC is missing.
  // Expectation: failure.
  zbi_header_t header = kValidHeader;
  header.flags = ZBI_ITEM_NO_CRC32;
  EXPECT_IS_OK(zbitl::CheckHeader(header));
}

TEST(ZbitlHeaderTest, FlagsMissing) {
  // * Item fits, magic is correct, required flags are missing, and CRC is set.
  // Expectation: failure.
  zbi_header_t header = kValidHeader;
  header.flags = 0u;
  EXPECT_IS_ERROR(zbitl::CheckHeader(header));
}

}  // namespace
