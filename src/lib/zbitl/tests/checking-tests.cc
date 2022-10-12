// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/checking.h>
#include <lib/zbitl/view.h>

#include <gtest/gtest.h>

// Meant for fit::result<std::string_view>.
#define EXPECT_IS_OK(result) \
  EXPECT_TRUE(result.is_ok()) << "unexpected error: " << result.error_value().data()
#define EXPECT_IS_ERROR(result) EXPECT_TRUE(result.is_error())
// Meant for fit::result<zbitl::View::Error>.
#define EXPECT_VIEW_IS_OK(result) \
  EXPECT_TRUE(result.is_ok()) << "unexpected error: " << result.error_value().zbi_error

namespace {

using ByteView = std::basic_string_view<const uint8_t>;

constexpr uint32_t kKernelType = 1u;
constexpr uint32_t kNonKernelType = 2u;

static constexpr zbi_header_t kValidItemHeader = {
    .length = ZBI_ALIGNMENT,
    .flags = ZBI_FLAGS_VERSION | ZBI_FLAGS_CRC32,
    .magic = ZBI_ITEM_MAGIC,
    .crc32 = 123,
};

static constexpr zbi_header_t kValidContainerHeader = ZBI_CONTAINER_HEADER(0);

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
  auto result = zbitl::CheckBootable(zbi, kKernelType);
  if (expect_ok) {
    EXPECT_IS_OK(result);
  } else {
    EXPECT_IS_ERROR(result);
  }
  EXPECT_VIEW_IS_OK(zbi.take_error());
}

TEST(ZbitlCheckBootableTests, BootableZbi) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kKernelType, kNonKernelType, true));
}

TEST(ZbitlCheckBootableTests, KernelNotFirst) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kNonKernelType, kKernelType, false));
}

TEST(ZbitlCheckBootableTests, KernelMissing) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kNonKernelType, kNonKernelType, false));
}

TEST(ZbitlCheckBootableTests, TwoKernels) {
  ASSERT_NO_FATAL_FAILURE(CheckTwoItemZbi(kKernelType, kKernelType, true));
}

TEST(ZbitlHeaderTests, ItemMagicAndFlagsMissing) {
  // * Item fits, but magic, required flags and CRC are unset.
  // Expectation: failure.
  zbi_header_t header = kValidItemHeader;
  header.flags = 0u;
  header.magic = 0u;
  header.crc32 = 0u;

  EXPECT_IS_ERROR(zbitl::CheckItemHeader(header));
}

TEST(ZbitlHeaderTests, ValidItemHeader) {
  // * Item fits, magic is correct, and required flags and CRC are set.
  // Expectation: success.
  EXPECT_IS_OK(zbitl::CheckItemHeader(kValidItemHeader));
}

TEST(ZbitlHeaderTests, ItemCrcIsMissing) {
  // * Item fits, magic is correct, required flags are set, and CRC is missing.
  // Expectation: failure.
  zbi_header_t header = kValidItemHeader;
  header.flags = ZBI_ITEM_NO_CRC32;
  EXPECT_IS_OK(zbitl::CheckItemHeader(header));
}

TEST(ZbitlHeaderTests, ItemFlagsMissing) {
  // * Item fits, magic is correct, required flags are missing, and CRC is set.
  // Expectation: failure.
  zbi_header_t header = kValidItemHeader;
  header.flags = 0u;
  EXPECT_IS_ERROR(zbitl::CheckItemHeader(header));
}

TEST(ZbitlHeaderTest, ValidContainerHeader) {
  EXPECT_IS_OK(zbitl::CheckContainerHeader(kValidContainerHeader));
}

TEST(ZbitlHeaderTests, ContainerMagicMissing) {
  // A container header requires both item and container magic to be set.
  {
    zbi_header_t header = kValidContainerHeader;
    header.magic = 0u;
    EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
  }
  {
    zbi_header_t header = kValidContainerHeader;
    header.extra = 0u;  // Holds container magic
    EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
  }
}

TEST(ZbitlHeaderTests, ContainerFlagsMissing) {
  zbi_header_t header = kValidContainerHeader;
  header.flags = 0u;
  EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
}

TEST(ZbitlHeaderTests, BadContainerType) {
  // Must be ZBI_TYPE_CONTAINER.
  zbi_header_t header = kValidContainerHeader;
  header.type = ZBI_TYPE_IMAGE_ARGS;
  EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
}

TEST(ZbitlHeaderTests, ContainerCrc) {
  // No CRC flag must be set.
  zbi_header_t header = kValidContainerHeader;
  header.flags |= ZBI_FLAGS_CRC32;
  EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
}

TEST(ZbitlHeaderTests, UnalignedContainerLength) {
  // Must be ZBI_ALIGNMENT-aligned.
  zbi_header_t header = kValidContainerHeader;
  header.length = ZBI_ALIGNMENT - 1;
  EXPECT_IS_ERROR(zbitl::CheckContainerHeader(header));
}

}  // namespace
