// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "span-tests.h"

#include <string>
#include <tuple>

#include "bootfs-tests.h"
#include "tests.h"

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

TEST(ZbitlViewByteSpanTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<ByteSpanTestTraits>());
}

TEST_ITERATION(ZbitlViewByteSpanTests, ByteSpanTestTraits)

TEST_MUTATION(ZbitlViewByteSpanTests, ByteSpanTestTraits)

TEST(ZbitlImageByteSpanTests, ExtendBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<ByteSpanTestTraits>());
}

TEST(ZbitlImageByteSpanTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<ByteSpanTestTraits>());
}

TEST(ZbitlBootfsByteSpanTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<ByteSpanTestTraits>());
}

TEST(ZbitlViewStringTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<StringTestTraits>());
}

TEST_ITERATION(ZbitlViewStringTests, StringTestTraits)

TEST(ZbitlBootfsStringTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<StringTestTraits>());
}

TEST(ZbitlViewStringTests, TooSmallForNextHeader) {
  // "payload" here refers to that of the entire container.
  constexpr std::string_view kExpectedError = "container doesn't fit. Truncated?";

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

// Construct and iterate over a ZBI with a payload of `actual_size`
// which has a header size claiming `claimed_size`.
//
// We check that an error was correctly reported.
void CheckInvalidPayloadSizeDetected(uint32_t claimed_size, uint32_t actual_size) {
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
        .type = ZBI_TYPE_IMAGE_ARGS,
        .length = claimed_size,
        .flags = ZBI_FLAG_VERSION,
        .magic = ZBI_ITEM_MAGIC,
        .crc32 = ZBI_ITEM_NO_CRC32,
    };
    zbi.append(reinterpret_cast<const char*>(&header), sizeof(header));
    zbi.append(actual_size, 'X');
  }

  // Iterate over the ZBI, and ensure no items were found.
  zbitl::View<std::string_view> view(zbi);
  EXPECT_TRUE(view.begin() == view.end());

  // Ensure an error was produced.
  auto result = view.take_error();
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error_value().zbi_error, "container too short for next item payload");
}

TEST(ZbitlViewStringTests, TooSmallForNextPayload) {
  // Try a variety of actual/claimed sizes.
  struct TestCase {
    uint32_t claimed_size;
    uint32_t actual_size;
  } sizes[] = {
      {1, 0},
      {ZBI_ALIGNMENT - 1, 0},
      {ZBI_ALIGNMENT, 0},
      {ZBI_ALIGNMENT, ZBI_ALIGNMENT - 1},
      {ZBI_ALIGNMENT + 1, ZBI_ALIGNMENT},
      {1024, 1023},
      {1024, 1024 - ZBI_ALIGNMENT},
      {UINT_MAX, 0},
      {UINT_MAX, 1024},
  };
  for (const TestCase& size : sizes) {
    SCOPED_TRACE("claimed_size = " + std::to_string(size.claimed_size) +
                 ", actual_size = " + std::to_string(size.actual_size));
    CheckInvalidPayloadSizeDetected(size.claimed_size, size.actual_size);
  }
}

}  // namespace
