// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "view-tests.h"

#include <string>
#include <tuple>

namespace {

struct EmptyTupleTestTraits {
  using storage_type = std::tuple<>;

  static constexpr bool kDefaultConstructedViewHasStorageError = true;
};

// The DefaultConstructed case is the only one that std::tuple<> passes since
// every other case requires readable storage.
TEST(ZbitlViewEmptyTupleTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<EmptyTupleTestTraits>());
}

TEST(ZbitlViewByteViewTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<ByteViewTestTraits>());
}

TEST(ZbitlViewByteViewTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<ByteViewTestTraits>());
}

TEST_ITERATION(ZbitlViewByteViewTests, ByteViewTestTraits)

TEST(ZbitlViewStringTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<StringTestTraits>());
}

TEST(ZbitlViewStringTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<StringTestTraits>());
}

TEST_ITERATION(ZbitlViewStringTests, StringTestTraits)

TEST(ZbitlViewStringTests, TooSmallForNextHeader) {
  constexpr std::string_view kExpectedError =
      "container header specifies length that exceeds capcity";

  // Construct a ZBI of reported size 64, but actual length 32 (just enough to
  // fit a single item header). Both accessing the container and header and
  // iteration should result in error, specifically `kExpectedError`.
  std::string zbi;
  {
    const zbi_header_t header = ZBI_CONTAINER_HEADER(sizeof(zbi_header_t));
    zbi.append(reinterpret_cast<const char*>(&header), sizeof(header));
  }
  zbitl::View<std::string_view> view(zbi);

  {
    auto result = view.container_header();
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(kExpectedError, result.error_value().zbi_error);
  }

  {
    for (auto [header, payload] : view) {
      EXPECT_EQ(header->type, header->type);
    }

    auto result = view.take_error();
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ(kExpectedError, result.error_value().zbi_error);
  }
}

TEST(ZbitlViewStringTests, TooSmallForNextPayload) {
  constexpr uint32_t kItemType = ZBI_TYPE_IMAGE_ARGS;

  constexpr std::string_view kExpectedError = "container too short for next item payload";

  // Construct a ZBI of reported size 64, but whose last header reports that
  // the last item extends beyond that. Iteration should result in
  // `kExpectedError`.
  std::string zbi;
  {
    const zbi_header_t header =
        ZBI_CONTAINER_HEADER(sizeof(zbi_header_t));  // Fits one item header.
    zbi.append(reinterpret_cast<const char*>(&header), sizeof(header));
  }
  {
    const zbi_header_t header = {
        .type = kItemType,
        .length = 8,
        .flags = ZBI_FLAG_VERSION,
        .magic = ZBI_ITEM_MAGIC,
        .crc32 = ZBI_ITEM_NO_CRC32,
    };
    zbi.append(reinterpret_cast<const char*>(&header), sizeof(header));
  }
  zbitl::View<std::string_view> view(zbi);

  for (auto [header, payload] : view) {
    EXPECT_EQ(header->type, header->type);
  }

  auto result = view.take_error();
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(kExpectedError, result.error_value().zbi_error);
}

}  // namespace
