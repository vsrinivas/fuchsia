// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-tests.h"

#include <lib/zbitl/error-string.h>

#include <algorithm>

#include <src/lib/files/file.h>

#include "bootfs-tests.h"
#include "tests.h"

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

      zbitl::View created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      auto crc_result = created_view.CheckCrc32(created_first);
      ASSERT_FALSE(crc_result.is_error()) << zbitl::ViewErrorString(crc_result.error_value());
      EXPECT_TRUE(crc_result.value());

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

      zbitl::View created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      auto crc_result = created_view.CheckCrc32(created_first);
      ASSERT_FALSE(crc_result.is_error()) << zbitl::ViewErrorString(crc_result.error_value());
      EXPECT_TRUE(crc_result.value());

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

      zbitl::View created_view(std::move(created));
      auto created_first = created_view.begin();
      EXPECT_EQ(created_view.end(), std::next(created_first));  // Should only have one item.

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      auto crc_result = created_view.CheckCrc32(created_first);
      ASSERT_FALSE(crc_result.is_error()) << zbitl::ViewErrorString(crc_result.error_value());
      EXPECT_TRUE(crc_result.value());

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

      zbitl::View created_view(std::move(created));
      auto created_first = created_view.begin();
      auto created_second = std::next(created_first);
      EXPECT_EQ(created_view.end(), std::next(created_second));  // Should have two items.

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      {
        auto result = created_view.CheckCrc32(created_first);
        ASSERT_FALSE(result.is_error()) << zbitl::ViewErrorString(result.error_value());
        EXPECT_TRUE(result.value());
      }
      {
        auto result = created_view.CheckCrc32(created_second);
        ASSERT_FALSE(result.is_error()) << zbitl::ViewErrorString(result.error_value());
        EXPECT_TRUE(result.value());
      }

      zbi_header_t src_header = *((*second).header);
      zbi_header_t dest1_header = *((*created_first).header);
      uint64_t dest1_payload = (*created_first).payload;
      zbi_header_t dest2_header = *((*created_second).header);

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

template <typename TestTraits>
void TestLargeFileDecompression() {
  using CreationTestTraits = typename TestTraits::creation_traits;

  static constexpr uint32_t kLargeZstdCompressedSize = 16397;
  static constexpr uint32_t kLargeZstdUncompressedSize = 16384;

  std::string compressed;
  ASSERT_TRUE(files::ReadFileToString("/pkg/data/large.zst", &compressed));

  // The compressed size should exceed the VMO buffered read chunk size, so
  // that multiple iterations of streaming decompression are exercised.
  ASSERT_EQ(kLargeZstdCompressedSize, compressed.size());
  ASSERT_GT(kLargeZstdCompressedSize, zbitl::StorageTraits<zx::vmo>::kBufferedReadChunkSize);

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(
      TestTraits::Create(2 * sizeof(zbi_header_t) + ZBI_ALIGN(kLargeZstdCompressedSize), &context));
  zbitl::Image image(context.TakeStorage());

  {
    auto result = image.clear();
    ASSERT_FALSE(result.is_error()) << zbitl::ViewErrorString(result.error_value());
  }
  {
    const zbi_header_t header{
        .type = ZBI_TYPE_STORAGE_RAMDISK,
        .extra = kLargeZstdUncompressedSize,
        .flags = ZBI_FLAG_STORAGE_COMPRESSED,
    };
    auto result = image.Append(header, zbitl::AsBytes(compressed));
    ASSERT_FALSE(result.is_error()) << zbitl::ViewErrorString(result.error_value());
  }

  auto it = image.begin();
  ASSERT_NE(it, image.end());

  {
    auto result = image.CopyStorageItem(it);
    ASSERT_FALSE(result.is_error()) << zbitl::ViewCopyErrorString(result.error_value());
    auto decompressed = std::move(result).value();
    const zx::vmo& vmo = CreationTestTraits::GetVmo(decompressed);
    size_t size;
    ASSERT_EQ(ZX_OK, vmo.get_size(&size));
    EXPECT_EQ(kLargeZstdUncompressedSize, size);
  }

  {
    auto result = image.take_error();
    ASSERT_FALSE(result.is_error()) << zbitl::ViewErrorString(result.error_value());
  }
}

template <typename TestTraits>
void TestInheritedResizability() {
  using CreationTestTraits = typename TestTraits::creation_traits;

  // Resizable if parent was resizable.
  {
    files::ScopedTempDir dir;
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kOneItem, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURE(
        TestTraits::template Create</*Resizable=*/true>(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    auto copy_result = view.Copy(view.begin(), view.end());
    ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();
    const zx::vmo& vmo = CreationTestTraits::GetVmo(created);

    zx_info_vmo_t info = {};
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_NE(0u, info.flags & ZX_INFO_VMO_RESIZABLE);

    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  // Non-resizable if parent was non-resizable.
  {
    files::ScopedTempDir dir;
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kOneItem, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURE(
        TestTraits::template Create</*Resizable=*/false>(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    auto copy_result = view.Copy(view.begin(), view.end());
    ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();
    const zx::vmo& vmo = CreationTestTraits::GetVmo(created);

    zx_info_vmo_t info = {};
    ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
    EXPECT_EQ(0u, info.flags & ZX_INFO_VMO_RESIZABLE);

    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }
}

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, CreateFromBogusZbi) {
  ASSERT_NO_FATAL_FAILURE(TestViewFromBogusZbi<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, Cloning) { ASSERT_NO_FATAL_FAILURE(TestCloning<VmoTestTraits>()); }

TEST_ITERATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_MUTATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_COPY_CREATION(ZbitlViewVmoTests, VmoTestTraits)

TEST(ZbitlViewVmoTests, LargeFileDecompression) {
  ASSERT_NO_FATAL_FAILURE(TestLargeFileDecompression<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, InheritedResizability) {
  ASSERT_NO_FATAL_FAILURE(TestInheritedResizability<VmoTestTraits>());
}

TEST(ZbitlImageVmoTests, Appending) { ASSERT_NO_FATAL_FAILURE(TestAppending<VmoTestTraits>()); }

TEST(ZbitlBootfsVmoTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<VmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURE(TestCloning<UnownedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST(ZbitlViewUnownedVmoTests, LargeFileDecompression) {
  ASSERT_NO_FATAL_FAILURE(TestLargeFileDecompression<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, InheritedResizability) {
  ASSERT_NO_FATAL_FAILURE(TestInheritedResizability<UnownedVmoTestTraits>());
}

TEST(ZbitlImageUnownedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<UnownedVmoTestTraits>());
}

TEST(ZbitlBootfsUnownedVmoTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<UnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<MapUnownedVmoTestTraits>());
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

TEST(ZbitlViewMapUnownedVmoTests, LargeFileDecompression) {
  ASSERT_NO_FATAL_FAILURE(TestLargeFileDecompression<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, InheritedResizability) {
  ASSERT_NO_FATAL_FAILURE(TestInheritedResizability<MapUnownedVmoTestTraits>());
}

TEST(ZbitlImageMapUnownedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<MapUnownedVmoTestTraits>());
}

TEST(ZbitlBootfsMapUnownedVmoTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURE(TestDefaultConstructedView<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURE(TestCloning<MapOwnedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST(ZbitlViewMapOwnedVmoTests, LargeFileDecompression) {
  ASSERT_NO_FATAL_FAILURE(TestLargeFileDecompression<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, InheritedResizability) {
  ASSERT_NO_FATAL_FAILURE(TestInheritedResizability<MapOwnedVmoTestTraits>());
}

TEST(ZbitlImageMapOwnedVmoTests, Appending) {
  ASSERT_NO_FATAL_FAILURE(TestAppending<MapOwnedVmoTestTraits>());
}

TEST(ZbitlBootfsMapOwnedVmoTests, Iteration) {
  ASSERT_NO_FATAL_FAILURE(TestBootfsIteration<MapOwnedVmoTestTraits>());
}

}  // namespace
