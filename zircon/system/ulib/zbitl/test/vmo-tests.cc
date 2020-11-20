// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-tests.h"

#include <lib/zbitl/error_string.h>

#include <algorithm>

namespace {

// Convert a zbi_header_t into a std::tuple.
//
// The tuple form allows easy comparison of the fields for tests.
auto HeaderToTuple(const zbi_header_t& header) {
  return std::make_tuple(header.type, header.length, header.extra, header.flags, header.reserved0,
                         header.reserved1, header.magic, header.crc32);
}

void ExpectVmoIsCloned(const zx::vmo& vmo, const zx::vmo& parent) {
  zx_info_vmo_t info, parent_info;
  ASSERT_EQ(ZX_OK,
            parent.get_info(ZX_INFO_VMO, &parent_info, sizeof(parent_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(parent_info.koid, info.parent_koid);
  EXPECT_TRUE(info.flags & ZX_INFO_VMO_IS_COW_CLONE) << "flags: 0x" << std::hex << info.flags;
}

void ExpectVmoIsNotCloned(const zx::vmo& vmo) {
  zx_info_vmo_t info;
  ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(0u, info.parent_koid);
  EXPECT_FALSE(info.flags & ZX_INFO_VMO_IS_COW_CLONE) << "flags: 0x" << std::hex << info.flags;
}

template <typename TestTraits>
void TestCloning() {
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  // kSecondItemOnPageBoundary.
  {
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURE(
        OpenTestDataZbi(TestDataZbiType::kSecondItemOnPageBoundary, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    // kSecondItemOnPageBoundary, copying the first item.
    // item offset == sizeof(zbi_header_t), and so we expect a clone without a
    // discard item.
    {
      auto first = view.begin();
      EXPECT_EQ(sizeof(zbi_header_t), first.item_offset());
      auto copy_result = view.Copy(first, std::next(first));
      ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

      auto created = std::move(copy_result).value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURE(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.
      zbi_header_t src_header = *((*first).header);
      zbi_header_t dest_header = *((*created_first).header);
      EXPECT_EQ(HeaderToTuple(src_header), HeaderToTuple(dest_header));

      auto result = created_view.take_error();
      EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
    }

    // kSecondItemOnPageBoundary, copying the second item.
    // item offset % ZX_PAGE_SIZE == 0, and so we do not expect a clone.
    {
      auto second = std::next(view.begin());
      EXPECT_EQ(0u, second.item_offset() % ZX_PAGE_SIZE);
      auto copy_result = view.Copy(second, std::next(second));
      ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

      auto created = std::move(copy_result).value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      ASSERT_NO_FATAL_FAILURE(ExpectVmoIsNotCloned(vmo));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.
      zbi_header_t src_header = *((*second).header);
      zbi_header_t dest_header = *((*created_first).header);
      EXPECT_EQ(HeaderToTuple(src_header), HeaderToTuple(dest_header));

      auto result = created_view.take_error();
      ASSERT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
    }

    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  // kMultipleSmallItems
  {
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURE(
        OpenTestDataZbi(TestDataZbiType::kMultipleSmallItems, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    // kMultipleSmallItems, copying the first item.
    // item offset  == sizeof(zbi_header_t), and so we expect a clone without a
    // discard item.
    {
      auto first = view.begin();
      EXPECT_EQ(sizeof(zbi_header_t), first.item_offset());
      auto copy_result = view.Copy(first, std::next(first));
      ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

      auto created = std::move(copy_result).value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURE(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.
      zbi_header_t src_header = *((*first).header);
      zbi_header_t dest_header = *((*created_first).header);
      EXPECT_EQ(HeaderToTuple(src_header), HeaderToTuple(dest_header));

      auto result = created_view.take_error();
      EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
    }

    // kMultipleSmallItems, copying the second item.
    // 2 * sizeof(zbi_header_t) <= item offset < ZX_PAGE_SIZE, and so we expect
    // a clone with a single discard item.
    {
      constexpr uint32_t kSecondItemSize = 240;
      auto second = std::next(view.begin());
      EXPECT_EQ(kSecondItemSize, second.item_offset());
      auto copy_result = view.Copy(second, std::next(second));
      ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

      auto created = std::move(copy_result).value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURE(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> created_view(std::move(created));
      auto created_first = created_view.begin();
      auto createdSecond = std::next(created_first);
      EXPECT_EQ(created_view.end(), std::next(createdSecond));  // Should have two items.

      zbi_header_t src_header = *((*second).header);
      zbi_header_t dest1_header = *((*created_first).header);
      uint64_t dest1_payload = (*created_first).payload;
      zbi_header_t dest2_header = *((*createdSecond).header);

      EXPECT_EQ(static_cast<uint32_t>(ZBI_TYPE_DISCARD), dest1_header.type);
      constexpr uint32_t kExpectedDiscardSize = kSecondItemSize - 2 * sizeof(zbi_header_t);
      ASSERT_EQ(kExpectedDiscardSize, dest1_header.length);
      Bytes contents;
      ASSERT_NO_FATAL_FAILURE(CreationTestTraits::Read(created_view.storage(), dest1_payload,
                                                       kExpectedDiscardSize, &contents));
      EXPECT_EQ(kExpectedDiscardSize, contents.size());
      EXPECT_TRUE(
          std::all_of(contents.begin(), contents.end(), [](char c) -> bool { return c == 0; }));

      EXPECT_EQ(HeaderToTuple(src_header), HeaderToTuple(dest2_header));

      auto result = created_view.take_error();
      ASSERT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
    }

    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }
}

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, Cloning) { ASSERT_NO_FATAL_FAILURE(TestCloning<VmoTestTraits>()); }

TEST_ITERATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_MUTATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_COPY_CREATION(ZbitlViewVmoTests, VmoTestTraits)

TEST(ZbitlImageVmoTests, Appending) { ASSERT_NO_FATAL_FAILURE(TestAppending<VmoTestTraits>()); }

TEST(ZbitlViewUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURE(TestCloning<UnownedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST(ZbitlImageUnownedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<UnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURE(TestCloning<MapUnownedVmoTestTraits>());
}

// Note that the iterations over many-small-items.zbi and
// second-item-on-page-boundary.zbi with CRC32 checking will cover the cases of
// mapping window re-use and replacement, respectively.
TEST_ITERATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST(ZbitlImageMapUnownedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURE(TestCrcCheckFailure<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURE(TestCloning<MapOwnedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST(ZbitlImageMapOwnedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<MapOwnedVmoTestTraits>());
}

}  // namespace
