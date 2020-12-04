// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory-tests.h"

#include <limits>

namespace {

using FblByteSpanTestTraits = FblSpanTestTraits<std::byte>;
using FblUint64ArrayTestTraits = FblArrayTestTraits<uint64_t>;

TEST(ZbitlViewFblByteSpanTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblByteSpanTestTraits>());
}

TEST(ZbitlViewFblByteSpanTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<FblByteSpanTestTraits>());
}

TEST_ITERATION(ZbitlViewFblByteSpanTests, FblByteSpanTestTraits)

TEST_MUTATION(ZbitlViewFblByteSpanTests, FblByteSpanTestTraits)

TEST(ZbitlImageFblByteSpanTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<FblByteSpanTestTraits>());
}

TEST(ZbitlViewFblByteArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblByteArrayTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<FblByteArrayTestTraits>());
}

TEST_ITERATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST_MUTATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST_COPY_CREATION(ZbitlViewFblByteArrayTests, FblByteArrayTestTraits)

TEST(ZbitlImageFblByteArrayTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblUint64ArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblUint64ArrayTestTraits>());
}

// TODO(joshuaseaton): Use ZBIs with payload size divisible by eight so we can
// further test FblUint64ArrayTestTraits.

TEST(ZbitlViewFblByteArrayTests, BoundsChecking) {
  using TestTraits = FblByteArrayTestTraits;

  files::ScopedTempDir dir;
  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kOneItem, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  ASSERT_EQ(kOneItemZbiSize, view.size_bytes());

  // Byte-range, direct copy: offset + length exceeds ZBI size
  {
    std::byte buff[kOneItemZbiSize];
    fbl::Span to{buff, kOneItemZbiSize};
    auto result = view.Copy(to, kOneItemZbiSize, 1u);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ("offset + length exceeds ZBI size", std::move(result).error_value().zbi_error);
  }

  // Byte-range, direct copy: to_offset + length overflows
  {
    std::byte buff[kOneItemZbiSize];
    fbl::Span to{buff, kOneItemZbiSize};
    auto result = view.Copy(to, 0u, 1u, std::numeric_limits<uint32_t>::max());
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ("to_offset + length overflows", std::move(result).error_value().zbi_error);
  }

  // Byte-range copy-creation: offset + length exceeds ZBI size.
  {
    auto result = view.Copy(kOneItemZbiSize, 1u);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ("offset + length exceeds ZBI size", std::move(result).error_value().zbi_error);
  }

  // Byte-range, copy-creation: to_offset + length overflows.
  {
    auto result = view.Copy(0u, 1u, std::numeric_limits<uint32_t>::max());
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ("to_offset + length overflows", std::move(result).error_value().zbi_error);
  }
}

}  // namespace
