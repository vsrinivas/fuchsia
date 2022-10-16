// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "array-tests.h"

#include <limits>

#include "bootfs-tests.h"
#include "tests.h"

namespace {

using FblUint64ArrayTestTraits = FblArrayTestTraits<uint64_t>;

TEST(ZbitlImageFblByteArrayTests, ExtendBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestExtendBogusZbiImage<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblByteArrayTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<FblByteArrayTestTraits>());
}

TEST(ZbitlViewFblByteArrayTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<FblByteArrayTestTraits>());
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
    cpp20::span to{buff, kOneItemZbiSize};
    auto result = view.Copy(to, kOneItemZbiSize, 1u);
    ASSERT_TRUE(result.is_error());
    EXPECT_EQ("offset + length exceeds ZBI size", std::move(result).error_value().zbi_error);
  }

  // Byte-range, direct copy: to_offset + length overflows
  {
    std::byte buff[kOneItemZbiSize];
    cpp20::span to{buff, kOneItemZbiSize};
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

TEST(StorageFromRawHeader, Creation) {
  // Create a simple in-memory ZBI with a single item payload.
  zbitl::Image<fbl::Array<std::byte>> image;
  ASSERT_TRUE(image.clear().is_ok());
  ASSERT_TRUE(image.Append(zbi_header_t{.type = ZBI_TYPE_DISCARD}, {}).is_ok());
  std::byte* raw_pointer = image.storage().data();
  const zbi_header_t* header = reinterpret_cast<zbi_header_t*>(raw_pointer);

  {
    auto view = zbitl::StorageFromRawHeader(header);  // default type is a ByteView
    EXPECT_EQ(view.data(), raw_pointer);
    EXPECT_EQ(view.size(), image.size_bytes());
  }
  {
    auto view = zbitl::StorageFromRawHeader<cpp20::span<const std::byte>>(header);
    EXPECT_EQ(view.data(), raw_pointer);
    EXPECT_EQ(view.size(), image.size_bytes());
  }
}

TEST(StorageFromRawHeader, BadHeader) {
  // Create a zbi_header_t with invalid magic.
  constexpr zbi_header_t header = {
      .length = 12345,
  };

  // Ensure that the length field was ignored, and that the returned span only
  // covers the zbi_header_t itself.
  zbitl::ByteView view = zbitl::StorageFromRawHeader(&header);
  EXPECT_EQ(view.size(), sizeof(zbi_header_t));
  EXPECT_EQ(view.data(), reinterpret_cast<const std::byte*>(&header));
}

TEST(ZbitlBootfsFblByteArrayTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<FblByteArrayTestTraits>());
}

TEST(ZbitlBootfsFblByteArrayTests, Subdirectory) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsSubdirectory<FblByteArrayTestTraits>());
}

}  // namespace
