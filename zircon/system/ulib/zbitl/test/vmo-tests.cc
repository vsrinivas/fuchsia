// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vmo-tests.h"

#include <algorithm>

#include "copy-tests.h"

namespace {

void ExpectVmoIsCloned(const zx::vmo& vmo, const zx::vmo& parent) {
  zx_info_vmo_t info, parent_info;
  ASSERT_EQ(ZX_OK,
            parent.get_info(ZX_INFO_VMO, &parent_info, sizeof(parent_info), nullptr, nullptr));
  ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(parent_info.koid, info.parent_koid);
  EXPECT_TRUE(info.flags & ZX_INFO_VMO_IS_COW_CLONE, "flags: %#x", info.flags);
}

void ExpectVmoIsNotCloned(const zx::vmo& vmo) {
  zx_info_vmo_t info;
  ASSERT_EQ(ZX_OK, vmo.get_info(ZX_INFO_VMO, &info, sizeof(info), nullptr, nullptr));
  EXPECT_EQ(0, info.parent_koid);
  EXPECT_FALSE(info.flags & ZX_INFO_VMO_IS_COW_CLONE, "flags: %#x", info.flags);
}

template <typename TestTraits>
void TestCloning() {
  using Storage = typename TestTraits::storage_type;
  static_assert(zbitl::View<Storage>::CanCopyCreate());
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  // kSecondItemOnPageBoundary.
  {
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURES(
        OpenTestDataZbi(TestDataZbiType::kSecondItemOnPageBoundary, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    // kSecondItemOnPageBoundary, copying the first item.
    // item offset == sizeof(zbi_header_t), and so we expect a clone without a
    // discard item.
    {
      auto first = view.begin();
      EXPECT_EQ(sizeof(zbi_header_t), first.item_offset());
      auto result = view.Copy(first, Next(first));
      ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
      ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

      auto created = std::move(result).value().value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURES(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> createdView(std::move(created));
      auto createdFirst = createdView.begin();
      EXPECT_EQ(createdView.end(), Next(createdFirst));  // Should only have one item.
      zbi_header_t src_header = *((*first).header);
      zbi_header_t dest_header = *((*createdFirst).header);
      EXPECT_BYTES_EQ(&src_header, &dest_header, sizeof(zbi_header_t));

      auto error = createdView.take_error();
      EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                   std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                   error.error_value().item_offset);
    }

    // kSecondItemOnPageBoundary, copying the second item.
    // item offset % ZX_PAGE_SIZE == 0, and so we do not expect a clone.
    {
      auto second = Next(view.begin());
      EXPECT_EQ(0, second.item_offset() % ZX_PAGE_SIZE);
      auto result = view.Copy(second, Next(second));
      ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
      ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

      auto created = std::move(result).value().value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      ASSERT_NO_FATAL_FAILURES(ExpectVmoIsNotCloned(vmo));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> createdView(std::move(created));
      auto createdFirst = createdView.begin();
      EXPECT_EQ(createdView.end(), Next(createdFirst));  // Should only have one item.
      zbi_header_t src_header = *((*second).header);
      zbi_header_t dest_header = *((*createdFirst).header);
      EXPECT_BYTES_EQ(&src_header, &dest_header, sizeof(zbi_header_t));

      auto error = createdView.take_error();
      EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                   std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                   error.error_value().item_offset);
    }

    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  // kMultipleSmallItems
  {
    fbl::unique_fd fd;
    size_t size = 0;
    ASSERT_NO_FATAL_FAILURES(
        OpenTestDataZbi(TestDataZbiType::kMultipleSmallItems, dir.path(), &fd, &size));

    typename TestTraits::Context context;
    ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
    zbitl::View view(context.TakeStorage());

    // kMultipleSmallItems, copying the first item.
    // item offset  == sizeof(zbi_header_t), and so we expect a clone without a
    // discard item.
    {
      auto first = view.begin();
      EXPECT_EQ(sizeof(zbi_header_t), first.item_offset());
      auto result = view.Copy(first, Next(first));
      ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
      ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

      auto created = std::move(result).value().value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURES(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> createdView(std::move(created));
      auto createdFirst = createdView.begin();
      EXPECT_EQ(createdView.end(), Next(createdFirst));  // Should only have one item.
      zbi_header_t src_header = *((*first).header);
      zbi_header_t dest_header = *((*createdFirst).header);
      EXPECT_BYTES_EQ(&src_header, &dest_header, sizeof(zbi_header_t));

      auto error = createdView.take_error();
      EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                   std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                   error.error_value().item_offset);
    }

    // kMultipleSmallItems, copying the second item.
    // 2 * sizeof(zbi_header_t) <= item offset < ZX_PAGE_SIZE, and so we expect
    // a clone with a single discard item.
    {
      constexpr uint32_t kSecondItemSize = 240;
      auto second = Next(view.begin());
      EXPECT_EQ(kSecondItemSize, second.item_offset());
      auto result = view.Copy(second, Next(second));
      ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
      ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

      auto created = std::move(result).value().value();
      const zx::vmo& vmo = CreationTestTraits::GetVmo(created);
      const zx::vmo& parent = TestTraits::GetVmo(view.storage());  // Well, would-be parent.
      ASSERT_NO_FATAL_FAILURES(ExpectVmoIsCloned(vmo, parent));

      // CRC-checking and header checking is sufficient to determine
      // byte-for-byte equality.
      zbitl::CrcCheckingView<decltype(created)> createdView(std::move(created));
      auto createdFirst = createdView.begin();
      auto createdSecond = Next(createdFirst);
      EXPECT_EQ(createdView.end(), Next(createdSecond));  // Should have two items.

      zbi_header_t src_header = *((*second).header);
      zbi_header_t dest1_header = *((*createdFirst).header);
      uint64_t dest1_payload = (*createdFirst).payload;
      zbi_header_t dest2_header = *((*createdSecond).header);

      EXPECT_EQ(ZBI_TYPE_DISCARD, dest1_header.type);
      constexpr uint32_t kExpectedDiscardSize = kSecondItemSize - 2 * sizeof(zbi_header_t);
      ASSERT_EQ(kExpectedDiscardSize, dest1_header.length);
      Bytes contents;
      ASSERT_NO_FATAL_FAILURES(CreationTestTraits::Read(createdView.storage(), dest1_payload,
                                                        kExpectedDiscardSize, &contents));
      EXPECT_EQ(kExpectedDiscardSize, contents.size());
      EXPECT_TRUE(
          std::all_of(contents.begin(), contents.end(), [](char c) -> bool { return c == 0; }));

      EXPECT_BYTES_EQ(&src_header, &dest2_header, sizeof(zbi_header_t));

      auto error = createdView.take_error();
      EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                   std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                   error.error_value().item_offset);
    }

    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }
}

TEST(ZbitlViewVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<VmoTestTraits>());
}

TEST(ZbitlViewVmoTests, Cloning) { ASSERT_NO_FATAL_FAILURES(TestCloning<VmoTestTraits>()); }

TEST_ITERATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_MUTATION(ZbitlViewVmoTests, VmoTestTraits)

TEST_COPY_CREATION(ZbitlViewVmoTests, VmoTestTraits)

TEST(ZbitlViewUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<UnownedVmoTestTraits>());
}

TEST(ZbitlViewUnownedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURES(TestCloning<UnownedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewUnownedVmoTests, UnownedVmoTestTraits)

TEST(ZbitlViewMapUnownedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapUnownedVmoTestTraits>());
}

TEST(ZbitlViewMapUnownedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURES(TestCloning<MapUnownedVmoTestTraits>());
}

// Note that the iterations over many-small-items.zbi and
// second-item-on-page-boundary.zbi with CRC32 checking will cover the cases of
// mapping window re-use and replacement, respectively.
TEST_ITERATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapUnownedVmoTests, MapUnownedVmoTestTraits)

TEST(ZbitlViewMapOwnedVmoTests, DefaultConstructed) {
  ASSERT_NO_FATAL_FAILURES(TestDefaultConstructedView<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, CrcCheckFailure) {
  ASSERT_NO_FATAL_FAILURES(TestCrcCheckFailure<MapOwnedVmoTestTraits>());
}

TEST(ZbitlViewMapOwnedVmoTests, Cloning) {
  ASSERT_NO_FATAL_FAILURES(TestCloning<MapOwnedVmoTestTraits>());
}

TEST_ITERATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_MUTATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

TEST_COPY_CREATION(ZbitlViewMapOwnedVmoTests, MapOwnedVmoTestTraits)

}  // namespace
