// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_TESTS_TESTS_H_
#define SRC_LIB_ZBITL_TESTS_TESTS_H_

#include <lib/zbitl/error-string.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/item.h>
#include <lib/zbitl/json.h>
#include <lib/zbitl/view.h>
#include <zircon/boot/image.h>

#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

// While it is convenient to use std::string as a container in representing ZBI
// content, we alias the type to convey that it need not necessarily represent
// text.
using Bytes = std::string;

constexpr size_t kMaxZbiSize = 4192;
constexpr size_t kOneItemZbiSize = 80;

enum class TestDataZbiType {
  kEmpty,
  kOneItem,
  kCompressedItem,
  kBadCrcItem,
  kMultipleSmallItems,
  kSecondItemOnPageBoundary,
  kBootfs,
};

// Parameterizes the behavior of copying a single item.
enum class ItemCopyMode {
  // Copy just the payload.
  kRaw,
  // Copy the header and payload.
  kWithHeader,
  // Copy the payload and decompress it as necessary.
  kStorage,
};

//
// Helpers for accessing test data.
//

size_t GetExpectedItemType(TestDataZbiType type);

bool ExpectItemsAreCompressed(TestDataZbiType type);

size_t GetExpectedNumberOfItems(TestDataZbiType type);

void GetExpectedPayload(TestDataZbiType type, size_t idx, Bytes* contents);

void GetExpectedPayloadWithHeader(TestDataZbiType type, size_t idx, Bytes* contents);

std::string GetExpectedJson(TestDataZbiType type);

void OpenTestDataZbi(TestDataZbiType type, std::string_view work_dir, fbl::unique_fd* fd,
                     size_t* num_bytes);

struct TestAllocator {
  fitx::result<std::string_view, std::unique_ptr<std::byte[]>> operator()(size_t bytes) {
    allocated_.push_back(bytes);
    return zbitl::decompress::DefaultAllocator(bytes);
  }

  std::vector<size_t> allocated_;
};

//
// Each type of storage under test is expected to implement a "test traits"
// struct with the following properties:
//   * `storage_type` type declaration;
//   * `Context` struct with `storage_type TakeStorage()` method that transfers
//     ownership of a storage object of type `storage_type`. It is
//     expected to be called at most once and the associated storage object is
//     valid only for as long as the context is alive.
//   * `static void Create(fbl::unique_fd, size_t, Context*)` for initializing
//     a context object from given file contents of a given size.
//   * `static void Create(size_t, Context*)`, for initializing a context
//     object corrsponding to a fresh storage object of a given size.
//   * `static void Read(storage_type&, payload_type, size_t, std::string*)`
//     for reading a payload of a given size into a string, where
//     `payload_type` coincides with
//     `zbitl::StorageTraits<storage_type>::payload_type`.
//   * a static constexpr bool `kExpectExtensibility` giving the expectation of
//     whether storage capacity can be extended.
//   * a static constexpr bool `kExpectOneShotReads` giving the expectation of
//     whether whole payloads can be accessed in memory directly.
//   * a static constexpr bool `kExpectUnbufferedReads` giving the expectation of
//     whether whole payloads can be access in memory directly or read into a
//     provided buffer without copying.
//   * a static constexpr bool `kExpectUnbufferedWrites` giving the expectation
//     of whether references to whole payloads can be provided for direct
//     mutation.
//
// If the storage type is default-constructible, the trait must have a static
// constexpr Boolean member `kDefaultConstructedViewHasStorageError` indicating
// whether a default-constructed view of that storage type yields an error on
// iteration.
//
// If the storage type is writable, it must also provide:
//   * `static void ToPayload(storage_type&, uint32_t, payload_type&)` that
//     populates a payload value given by an offset into a storage instance.
//
// If the storage type is writable, it must also provide:
//   * `void Write(storage_type&, uint32_t offset, Bytes data)` that writes data
//     to the provided offset.
//
// If the storage type supports creation
// (i.e., zbitl::StorageTraits<storage_type> defines Create()) then the test
//  traits must declare a `creation_traits` type giving the associated test
//  trait type of its created storage.
//

//
// Test cases.
//

template <typename TestTraits>
inline void TestDefaultConstructedView() {
  using Storage = typename TestTraits::storage_type;
  static_assert(std::is_default_constructible_v<Storage>,
                "this test case only applies to default-constructible storage types");

  zbitl::View<Storage> view;

  // This ensures that everything statically compiles when instantiating the
  // templates, even though the header/payloads are never used.
  for (auto [header, payload] : view) {
    EXPECT_EQ(header->flags, header->flags);
    FAIL() << "should not be reached";
  }

  auto error = view.take_error();
  ASSERT_TRUE(error.is_error()) << "no error when header cannot be read??";
  EXPECT_FALSE(error.error_value().zbi_error.empty()) << "empty zbi_error string!!";
  if constexpr (TestTraits::kDefaultConstructedViewHasStorageError) {
    EXPECT_TRUE(error.error_value().storage_error.has_value());
  } else {
    EXPECT_FALSE(error.error_value().storage_error.has_value());
  }
}

template <typename TestTraits>
inline void TestViewFromBogusZbi() {
  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(sizeof(zbi_header_t), &context));

  // make bogus.
  auto storage = context.TakeStorage();
  zbi_header_t container_header = {.length = 100};
  Bytes bytes(reinterpret_cast<const char*>(&container_header), sizeof(container_header));
  TestTraits::Write(storage, 0, bytes);

  zbitl::View view(std::move(storage));

  EXPECT_TRUE(view.container_header().is_error());
  EXPECT_EQ(view.size_bytes(), 0u);
}

template <typename TestTraits>
inline void TestIteration(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  auto container_result = view.container_header();
  ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;
    EXPECT_EQ(GetExpectedItemType(type), header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
    EXPECT_EQ(expected, actual);

    const uint32_t flags = header->flags;
    EXPECT_TRUE(flags & ZBI_FLAG_VERSION) << "flags: 0x" << std::hex << flags;

    // Let's verify CRC32 while we're at it.
    auto crc_result = view.CheckCrc32(it);
    ASSERT_FALSE(crc_result.is_error()) << ViewErrorString(crc_result.error_value());
    EXPECT_EQ(type != TestDataZbiType::kBadCrcItem, crc_result.value());
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

#define TEST_ITERATION_BY_TYPE(suite_name, TestTraits, type_name, type) \
  TEST(suite_name, type_name##Iteration) {                              \
    auto test = TestIteration<TestTraits>;                              \
    ASSERT_NO_FATAL_FAILURE(test(type));                                \
  }

#define TEST_ITERATION(suite_name, TestTraits)                                            \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, EmptyZbi, TestDataZbiType::kEmpty)       \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem)   \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, BadCrcZbi, TestDataZbiType::kBadCrcItem) \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                   \
                         TestDataZbiType::kMultipleSmallItems)                            \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,             \
                         TestDataZbiType::kSecondItemOnPageBoundary)

template <typename TestTraits>
void TestMutation(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  OpenTestDataZbi(type, dir.path(), &fd, &size);

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());  // Yay deduction guides.

  auto container_result = view.container_header();
  ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

  size_t expected_num_items = GetExpectedNumberOfItems(type);

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    auto [header, payload] = *it;

    EXPECT_EQ(GetExpectedItemType(type), header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
    EXPECT_EQ(expected, actual);

    ASSERT_TRUE(view.EditHeader(it, {.type = ZBI_TYPE_DISCARD}).is_ok());
  }
  EXPECT_EQ(expected_num_items, idx);

  {
    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  idx = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(static_cast<uint32_t>(ZBI_TYPE_DISCARD), header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
    EXPECT_EQ(expected, actual);
  }
  EXPECT_EQ(expected_num_items, idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

#define TEST_MUTATION_BY_TYPE(suite_name, TestTraits, type_name, type) \
  TEST(suite_name, type_name##Mutation) { ASSERT_NO_FATAL_FAILURE(TestMutation<TestTraits>(type)); }

#define TEST_MUTATION(suite_name, TestTraits)                                                \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem)       \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, BadCrcItemZbi, TestDataZbiType::kBadCrcItem) \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                       \
                        TestDataZbiType::kMultipleSmallItems)                                \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,                 \
                        TestDataZbiType::kSecondItemOnPageBoundary)

template <typename SrcTestTraits, typename DestTestTraits>
constexpr bool kExpectOneShotDecompression =
    SrcTestTraits::kExpectOneShotReads && DestTestTraits::kExpectUnbufferedWrites;

template <typename SrcTestTraits, typename DestTestTraits>
constexpr bool kExpectZeroCopying =
    SrcTestTraits::kExpectOneShotReads ||
    (SrcTestTraits::kExpectUnbufferedReads && DestTestTraits::kExpectUnbufferedWrites);

inline size_t OneShotDecompressionScratchSize() {
  return zbitl::decompress::OneShot::GetScratchSize();
}

template <typename TestTraits>
void TestCopyCreation(TestDataZbiType type, ItemCopyMode mode) {
  using CreationTraits = typename TestTraits::creation_traits;
  using Storage = typename TestTraits::storage_type;
  using CreationStorage = typename CreationTraits::storage_type;

  static_assert(zbitl::View<Storage>::template CanZeroCopy<CreationStorage>() ==
                kExpectZeroCopying<TestTraits, CreationTraits>);

  files::ScopedTempDir dir;
  TestAllocator allocator;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  auto get_size = [&mode](const zbi_header_t& header) -> size_t {
    switch (mode) {
      case ItemCopyMode::kRaw:
        return header.length;
      case ItemCopyMode::kWithHeader:
        return header.length + sizeof(header);
      case ItemCopyMode::kStorage:
        // Though we are officially using the code-under-test here, the spec
        // currently provides no way to determine whether a given type is a
        // a storage type; once can only check whether it is among an
        // exhaustive list of such types, which is what this utility does.
        return zbitl::TypeIsStorage(header.type) ? header.extra : header.length;
    };
    __UNREACHABLE;
  };

  auto do_copy = [&mode, &view, &allocator](auto it) {
    switch (mode) {
      case ItemCopyMode::kRaw:
        return view.CopyRawItem(it);
      case ItemCopyMode::kWithHeader:
        return view.CopyRawItemWithHeader(it);
      case ItemCopyMode::kStorage:
        return view.CopyStorageItem(it, allocator);
    };
    __UNREACHABLE;
  };

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *it->header;

    const size_t created_size = get_size(header);
    auto result = do_copy(it);
    ASSERT_TRUE(result.is_ok()) << "item " << idx << ": "
                                << ViewCopyErrorString(result.error_value());
    if (mode == ItemCopyMode::kStorage) {
      if (ExpectItemsAreCompressed(type)) {
        ASSERT_FALSE(allocator.allocated_.empty());
        // The first allocated size is expected to be the scratch size.
        if (kExpectOneShotDecompression<TestTraits, CreationTraits>) {
          EXPECT_EQ(OneShotDecompressionScratchSize(), allocator.allocated_.front());
          EXPECT_EQ(1u, allocator.allocated_.size());
        } else {
          EXPECT_GT(allocator.allocated_.front(), OneShotDecompressionScratchSize());
        }
      } else {
        EXPECT_TRUE(allocator.allocated_.empty());
      }
    }

    auto created = std::move(result).value();

    Bytes actual;
    typename CreationTraits::payload_type created_payload;
    ASSERT_NO_FATAL_FAILURE(CreationTraits::ToPayload(created, 0, created_payload));
    ASSERT_NO_FATAL_FAILURE(CreationTraits::Read(created, created_payload, created_size, &actual));

    Bytes expected;
    switch (mode) {
      case ItemCopyMode::kRaw:
      case ItemCopyMode::kStorage:
        ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
        break;
      case ItemCopyMode::kWithHeader:
        ASSERT_NO_FATAL_FAILURE(GetExpectedPayloadWithHeader(type, idx, &expected));
        break;
    };

    EXPECT_EQ(expected, actual);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

// We simply test in this case that we are able to copy-create the byte ranges
// associated with the item payloads. More strenuous exercise of the interface
// is done under-the-hood by the other copy-creation tests.
template <typename TestTraits>
void TestCopyCreationByByteRange(TestDataZbiType type) {
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    uint32_t payload_size = it->header->length;
    // We pick a `to_offset` of `idx` for want of a value of zero along with
    // varying, non-zero, non-random values.
    uint32_t to_offset = static_cast<uint32_t>(idx);
    auto result = view.Copy(it.payload_offset(), payload_size, to_offset);
    EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());

    auto created = std::move(result).value();

    Bytes actual;
    typename CreationTestTraits::payload_type created_payload;
    ASSERT_NO_FATAL_FAILURE(CreationTestTraits::ToPayload(created, 0, created_payload));
    ASSERT_NO_FATAL_FAILURE(
        CreationTestTraits::Read(created, created_payload, to_offset + payload_size, &actual));

    // We expect a head of `to_offset`-many zeroes.
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
    expected = Bytes(to_offset, '\0').append(expected);

    EXPECT_EQ(expected, actual);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

template <typename TestTraits>
void TestCopyCreationByIteratorRange(TestDataZbiType type) {
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  // [begin(), begin())
  {
    auto copy_result = view.Copy(view.begin(), view.begin());
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();
    zbitl::View created_view(std::move(created));

    // Should be empty.
    EXPECT_EQ(created_view.end(), created_view.begin());

    auto result = created_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  // [begin(), begin() + 1)
  if (view.begin() != view.end()) {
    auto first = view.begin();
    auto second = std::next(first);

    auto copy_result = view.Copy(first, second);
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();

    zbitl::View created_view(std::move(created));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    size_t idx = 0;
    for (auto [header, payload] : created_view) {
      EXPECT_EQ(GetExpectedItemType(type), header->type);

      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          CreationTestTraits::Read(created_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
      EXPECT_EQ(expected, actual);

      const uint32_t flags = header->flags;
      EXPECT_TRUE(flags & ZBI_FLAG_VERSION) << "flags: 0x" << std::hex << flags;
    }
    EXPECT_EQ(1u, idx);

    auto result = created_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  // [begin() + 1, end()).
  if (view.begin() != view.end()) {
    auto second = std::next(view.begin());

    auto copy_result = view.Copy(second, view.end());
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();

    zbitl::View created_view(std::move(created));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    // We might have filled slop with a single discard element; skip if so.
    auto it = created_view.begin();
    if (it != created_view.end() && it->header->type == ZBI_TYPE_DISCARD) {
      ++it;
    }

    size_t idx = 1;  // Corresponding to begin() + 1.
    for (; it != created_view.end(); ++it, ++idx) {
      auto [header, payload] = *it;
      EXPECT_EQ(GetExpectedItemType(type), header->type);

      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          CreationTestTraits::Read(created_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
      EXPECT_EQ(expected, actual);

      const uint32_t flags = header->flags;
      EXPECT_TRUE(flags & ZBI_FLAG_VERSION) << "flags: 0x" << std::hex << flags;
    }
    EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

    auto result = created_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopyingIntoSmallStorage() {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kOneItem, dir.path(), &fd, &size));

  typename SrcTestTraits::Context src_context;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Create(std::move(fd), size, &src_context));
  zbitl::View view(src_context.TakeStorage());

  auto [header, payload] = *(view.begin());

  typename DestTestTraits::Context dest_context;
  ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create((header->length) / 2, &dest_context));
  auto small_storage = dest_context.TakeStorage();

  auto copy_result = view.Copy(small_storage, view.begin().payload_offset(), header->length);
  ASSERT_FALSE(copy_result.is_error()) << ViewCopyErrorString(std::move(copy_result).error_value());

  Bytes expected;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Read(view.storage(), payload, header->length, &expected));

  Bytes actual;
  typename DestTestTraits::payload_type as_payload;
  ASSERT_NO_FATAL_FAILURE(DestTestTraits::ToPayload(small_storage, 0, as_payload));
  ASSERT_NO_FATAL_FAILURE(DestTestTraits::Read(small_storage, as_payload, header->length, &actual));

  EXPECT_EQ(expected, actual);

  auto result = view.take_error();
  ASSERT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
}

#define TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, type_name, type, mode, \
                                            mode_name)                                     \
  TEST(suite_name, type_name##CopyCreation##mode_name) {                                   \
    ASSERT_NO_FATAL_FAILURE(TestCopyCreation<TestTraits>(type, mode));                     \
  }

#define TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, type_name, type)     \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, type_name, type,  \
                                      ItemCopyMode::kRaw, )                     \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, type_name, type,  \
                                      ItemCopyMode::kWithHeader, WithHeader)    \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, type_name, type,  \
                                      ItemCopyMode::kStorage, AsStorage)        \
  TEST(suite_name, type_name##CopyCreationByByteRange) {                        \
    ASSERT_NO_FATAL_FAILURE(TestCopyCreationByByteRange<TestTraits>(type));     \
  }                                                                             \
  TEST(suite_name, type_name##CopyCreationByIteratorRange) {                    \
    ASSERT_NO_FATAL_FAILURE(TestCopyCreationByIteratorRange<TestTraits>(type)); \
  }

#define TEST_COPY_CREATION(suite_name, TestTraits)                                              \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, EmptyZbi, TestDataZbiType::kEmpty)         \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem)     \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, CompressedItemZbi,                \
                                      TestDataZbiType::kCompressedItem, ItemCopyMode::kStorage, \
                                      AsStorage)                                                \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, BadCrcItemZbi,                    \
                                      TestDataZbiType::kBadCrcItem, ItemCopyMode::kRaw, )       \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                     \
                             TestDataZbiType::kMultipleSmallItems)                              \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,               \
                             TestDataZbiType::kSecondItemOnPageBoundary)

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopying(TestDataZbiType type, ItemCopyMode mode) {
  using SrcStorage = typename SrcTestTraits::storage_type;
  using DestStorage = typename DestTestTraits::storage_type;

  static_assert(zbitl::View<SrcStorage>::template CanZeroCopy<DestStorage>() ==
                kExpectZeroCopying<SrcTestTraits, DestTestTraits>);

  files::ScopedTempDir dir;
  TestAllocator allocator;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  auto get_size = [&mode](const zbi_header_t& header) -> size_t {
    switch (mode) {
      case ItemCopyMode::kRaw:
        return header.length;
      case ItemCopyMode::kWithHeader:
        return header.length + sizeof(header);
      case ItemCopyMode::kStorage:
        return zbitl::UncompressedLength(header);
    };
    __UNREACHABLE;
  };

  auto do_copy = [&mode, &view, &allocator](auto&& storage, auto it) -> auto {
    using Storage = decltype(storage);
    switch (mode) {
      case ItemCopyMode::kRaw:
        return view.CopyRawItem(std::forward<Storage>(storage), it);
      case ItemCopyMode::kWithHeader:
        return view.CopyRawItemWithHeader(std::forward<Storage>(storage), it);
      case ItemCopyMode::kStorage:
        return view.CopyStorageItem(std::forward<Storage>(storage), it, allocator);
    };
    __UNREACHABLE;
  };

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *it->header;
    const size_t copy_size = get_size(header);

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(copy_size, &copy_context));
    auto copy = copy_context.TakeStorage();
    auto result = do_copy(std::move(copy), it);
    ASSERT_TRUE(result.is_ok()) << "item " << idx << ": "
                                << ViewCopyErrorString(result.error_value());

    if (mode == ItemCopyMode::kStorage) {
      if (ExpectItemsAreCompressed(type)) {
        ASSERT_FALSE(allocator.allocated_.empty());
        // The first allocated size is expected to be the scratch size.
        if (kExpectOneShotDecompression<SrcTestTraits, DestTestTraits>) {
          EXPECT_EQ(OneShotDecompressionScratchSize(), allocator.allocated_.front());
          EXPECT_EQ(1u, allocator.allocated_.size());
        } else {
          EXPECT_GT(allocator.allocated_.front(), OneShotDecompressionScratchSize());
        }
      } else {
        EXPECT_TRUE(allocator.allocated_.empty());
      }
    }

    Bytes actual;
    typename DestTestTraits::payload_type copy_payload;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::ToPayload(copy, 0, copy_payload));
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Read(copy, copy_payload, copy_size, &actual));

    Bytes expected;
    switch (mode) {
      case ItemCopyMode::kRaw:
      case ItemCopyMode::kStorage:
        ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
        break;
      case ItemCopyMode::kWithHeader:
        ASSERT_NO_FATAL_FAILURE(GetExpectedPayloadWithHeader(type, idx, &expected));
        break;
    };
    EXPECT_EQ(expected, actual);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

// We simply test in this case that we are able to copy the byte ranges
// associated with the item payloads. More strenuous exercise of the
// interface is done under-the-hood by the other copy tests.
template <typename SrcTestTraits, typename DestTestTraits>
void TestCopyingByByteRange(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    // We pick a `to_offset` of `idx` for want of a value of zero along with
    // varying, non-zero, non-random values.
    uint32_t to_offset = static_cast<uint32_t>(idx);

    uint32_t payload_size = it->header->length;

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(to_offset + payload_size, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto result = view.Copy(copy, it.payload_offset(), payload_size, to_offset);
    EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());

    Bytes actual;
    typename DestTestTraits::payload_type copy_payload;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::ToPayload(copy, to_offset, copy_payload));
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Read(copy, copy_payload, payload_size, &actual));

    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
    EXPECT_EQ(expected, actual);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopyingByIteratorRange(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  // [begin(), begin())
  {
    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    {
      auto result = view.Copy(copy, view.begin(), view.begin());
      EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());
    }

    zbitl::View copy_view(std::move(copy));

    // Should be empty.
    EXPECT_EQ(copy_view.end(), copy_view.begin());

    {
      auto result = copy_view.take_error();
      EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
    }
  }

  // [begin(), begin() + 1)
  if (view.begin() != view.end()) {
    auto first = view.begin();
    auto second = std::next(first);

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto copy_result = view.Copy(copy, first, second);
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    zbitl::View copy_view(std::move(copy));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    size_t idx = 0;
    for (auto [header, payload] : copy_view) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          DestTestTraits::Read(copy_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
      EXPECT_EQ(expected, actual);
    }
    EXPECT_EQ(first == view.end() ? 0u : 1u, idx);

    auto result = copy_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  if (view.begin() != view.end()) {
    auto second = std::next(view.begin());

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    {
      auto result = view.Copy(copy, second, view.end());
      EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());
    }

    zbitl::View copy_view(std::move(copy));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    size_t idx = 1;  // Corresponding to begin() + 1.
    for (auto [header, payload] : copy_view) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          DestTestTraits::Read(copy_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
      EXPECT_EQ(expected, actual);
    }
    EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

    auto result = copy_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  {
    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }
}

#define TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, \
                                      dest_name, type_name, type, mode, mode_name)         \
  TEST(suite_name, type_name##Copy##src_name##To##dest_name##mode_name) {                  \
    auto test = TestCopying<SrcTestTraits, DestTestTraits>;                                \
    ASSERT_NO_FATAL_FAILURE(test(type, mode));                                             \
  }

#define TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,    \
                             type_name, type)                                                   \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, \
                                type_name, type, ItemCopyMode::kRaw, )                          \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, \
                                type_name, type, ItemCopyMode::kWithHeader, WithHeader)         \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, \
                                type_name, type, ItemCopyMode::kStorage, AsStorage)             \
  TEST(suite_name, type_name##Copy##src_name##To##dest_name##ByByteRange) {                     \
    auto test = TestCopyingByByteRange<SrcTestTraits, DestTestTraits>;                          \
    ASSERT_NO_FATAL_FAILURE(test(type));                                                        \
  }                                                                                             \
  TEST(suite_name, type_name##Copy##src_name##To##dest_name##ByIteratorRange) {                 \
    auto test = TestCopyingByIteratorRange<SrcTestTraits, DestTestTraits>;                      \
    ASSERT_NO_FATAL_FAILURE(test(type));                                                        \
  }

#define TEST_COPYING_INTO_SMALL_STORAGE(suite_name, SrcTestTraits, src_name, DestTestTraits, \
                                        dest_name)                                           \
  TEST(suite_name, Copying##src_name##to##dest_name##SmallStorage) {                         \
    auto test = TestCopyingIntoSmallStorage<SrcTestTraits, DestTestTraits>;                  \
    ASSERT_NO_FATAL_FAILURE(test());                                                         \
  }

#define TEST_COPYING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)               \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, EmptyZbi,   \
                       TestDataZbiType::kEmpty)                                                    \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, OneItemZbi, \
                       TestDataZbiType::kOneItem)                                                  \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,    \
                                CompressedItemZbi, TestDataZbiType::kCompressedItem,               \
                                ItemCopyMode::kStorage, AsStorage)                                 \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,    \
                                BadCrcItemZbi, TestDataZbiType::kBadCrcItem, ItemCopyMode::kRaw, ) \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       MultipleSmallItemsZbi, TestDataZbiType::kMultipleSmallItems)                \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       SecondItemOnPageBoundaryZbi, TestDataZbiType::kSecondItemOnPageBoundary)    \
  TEST_COPYING_INTO_SMALL_STORAGE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)

template <typename TestTraits>
void TestAppending() {
  const Bytes to_append[] = {
      "",
      "aligned ",
      "unaligned",
  };

  // The expected resulting size from appending items corresponding to the
  // entries in `to_append`, once per `Append` method.
  constexpr size_t kExpectedFinalSize = 272;

  // For extensible storage, we expect the capacity to increase as needed
  // during Image operations.
  constexpr size_t kInitialSize = TestTraits::kExpectExtensibility ? 0 : kExpectedFinalSize;

  constexpr uint32_t kItemType = ZBI_TYPE_IMAGE_ARGS;

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(kInitialSize, &context));

  zbitl::Image image(context.TakeStorage());

  // clear() will turn an empty storage object into an empty ZBI (i.e., of
  // sufficient size to hold a trivial ZBI container header).
  {
    auto clear_result = image.clear();
    ASSERT_FALSE(clear_result.is_error()) << ViewErrorString(std::move(clear_result).error_value());
    ASSERT_EQ(image.end(), image.begin());  // Is indeed empty.
  }

  // Append-with-payload.
  for (const Bytes& bytes : to_append) {
    auto append_result = image.Append(
        zbi_header_t{
            .type = kItemType,
            .flags = ZBI_FLAG_CRC32,
        },
        zbitl::ByteView{reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
    ASSERT_FALSE(append_result.is_error())
        << "bytes = \"" << bytes
        << "\": " << ViewErrorString(std::move(append_result).error_value());
  }

  // Append-with-deferred-write.
  for (const Bytes& bytes : to_append) {
    auto append_result = image.Append(zbi_header_t{
        .type = kItemType,
        .length = static_cast<uint32_t>(bytes.size()),
    });
    ASSERT_FALSE(append_result.is_error())
        << "bytes = \"" << bytes
        << "\": " << ViewErrorString(std::move(append_result).error_value());

    auto it = std::move(append_result).value();
    ASSERT_NE(it, image.end());

    // The recorded header should be sanitized.
    auto [header, payload] = *it;
    EXPECT_EQ(kItemType, header->type);
    EXPECT_EQ(bytes.size(), header->length);
    EXPECT_EQ(ZBI_ITEM_MAGIC, header->magic);
    EXPECT_TRUE(ZBI_FLAG_VERSION & header->flags);
    EXPECT_FALSE(ZBI_FLAG_CRC32 & header->flags);  // We did not bother to set it.
    EXPECT_EQ(static_cast<uint32_t>(ZBI_ITEM_NO_CRC32), header->crc32);

    if (!bytes.empty()) {
      uint32_t offset = it.payload_offset();
      ASSERT_NO_FATAL_FAILURE(TestTraits::Write(image.storage(), offset, bytes));
    }
  }

  auto it = image.begin();
  for (size_t variation = 0; variation < 2; ++variation) {
    for (size_t i = 0; i < std::size(to_append) && it != image.end(); ++i, ++it) {
      auto [header, payload] = *it;

      // The recorded header should have add a number of fields set on the
      // caller's behalf.
      EXPECT_EQ(kItemType, header->type);
      EXPECT_EQ(to_append[i].size(), header->length);  // Auto-computed in append-with-payload.
      EXPECT_EQ(ZBI_ITEM_MAGIC, header->magic);
      EXPECT_TRUE(ZBI_FLAG_VERSION & header->flags);
      // That we are using a CRC-checking image guarantees that the right
      // CRC32 values are computed.
      switch (variation) {
        case 0: {  // append-with-payload
          EXPECT_TRUE(ZBI_FLAG_CRC32 & header->flags);

          // Verify that the CRC32 was properly auto-computed.
          auto crc_result = image.CheckCrc32(it);
          ASSERT_FALSE(crc_result.is_error()) << zbitl::ViewErrorString(crc_result.error_value());
          EXPECT_TRUE(crc_result.value());
          break;
        }
        case 1: {  // append-with-deferred-write
          EXPECT_FALSE(ZBI_FLAG_CRC32 & header->flags);
          break;
        }
      };

      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(TestTraits::Read(image.storage(), payload, header->length, &actual));
      const Bytes expected = to_append[i];
      // `actual[0:expected.size()]` should coincide with `expected`, and its tail
      // should be a |ZBI_ALIGNMENT|-pad of zeroes.
      ASSERT_EQ(static_cast<uint32_t>(expected.size()), actual.size());
      EXPECT_EQ(expected, actual.substr(0, expected.size()));
      EXPECT_TRUE(std::all_of(actual.begin() + expected.size(), actual.end(),
                              [](char c) -> bool { return c == 0; }));
    }
  }
  EXPECT_EQ(image.end(), it);
  EXPECT_EQ(kExpectedFinalSize, image.size_bytes());

  // If we are dealing with non-extensible storage, attempting to append
  // again should result in an error.
  if constexpr (!TestTraits::kExpectExtensibility) {
    {
      auto result = image.Append(zbi_header_t{.type = kItemType}, zbitl::ByteView{});
      EXPECT_TRUE(result.is_error());
    }
    {
      auto result = image.Append(zbi_header_t{.type = kItemType, .length = 0});
      EXPECT_TRUE(result.is_error());
    }
  } else {
    // Test appending and then truncating or trimming.

    auto count_items = [&image]() -> size_t { return std::distance(image.begin(), image.end()); };
    const size_t count_before = count_items();
    const size_t size_before = image.size_bytes();

    {
      auto result = image.Append(zbi_header_t{.type = kItemType}, zbitl::ByteView{});
      EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
    }
    {
      auto result = image.Append(zbi_header_t{.type = kItemType, .length = 0});
      EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
    }

    const size_t count_after = count_items();
    EXPECT_EQ(count_after, count_before + 2);

    {
      auto item = std::next(image.begin(), count_before);
      ASSERT_EQ(std::distance(item, image.end()), 2u);
      auto result = image.Truncate(item);
      EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
      EXPECT_EQ(count_items(), count_before);
      EXPECT_EQ(image.size_bytes(), size_before);
    }

    {
      auto result = image.Append(zbi_header_t{.type = kItemType, .length = 99});
      ASSERT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
      EXPECT_EQ(result.value()->header->length, 99u);
      EXPECT_EQ(image.size_bytes(), size_before + sizeof(zbi_header_t) + ZBI_ALIGN(99));

      auto trim_result = image.TrimLastItem(result.value(), 33u);
      ASSERT_FALSE(trim_result.is_error()) << ViewErrorString(std::move(trim_result).error_value());
      EXPECT_EQ(trim_result.value()->header->length, 33u);
      EXPECT_EQ(image.size_bytes(), size_before + sizeof(zbi_header_t) + ZBI_ALIGN(33));
    }
  }

  {
    auto result = image.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
  }

  // clear() will reset the underlying ZBI as empty.
  {
    auto clear_result = image.clear();
    ASSERT_FALSE(clear_result.is_error()) << ViewErrorString(std::move(clear_result).error_value());
    ASSERT_EQ(image.end(), image.begin());  // Is indeed empty.

    auto result = image.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
  }
}

template <typename SrcTestTraits, typename DestTestTraits>
void TestExtending() {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(
      OpenTestDataZbi(TestDataZbiType::kMultipleSmallItems, dir.path(), &fd, &size));

  typename SrcTestTraits::Context src_context;
  ASSERT_NO_FATAL_FAILURE(SrcTestTraits::Create(std::move(fd), size, &src_context));
  zbitl::View view(src_context.TakeStorage());

  typename DestTestTraits::Context dest_context;
  ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(0, &dest_context));
  zbitl::Image image(dest_context.TakeStorage());

  // clear() will turn an empty storage object into an empty ZBI (i.e., of
  // sufficient size to hold a trivial ZBI container header).
  {
    auto clear_result = image.clear();
    ASSERT_FALSE(clear_result.is_error()) << ViewErrorString(std::move(clear_result).error_value());
    ASSERT_EQ(image.end(), image.begin());  // Is indeed empty.
  }

  // [begin(), begin() + 1)
  {
    auto extend_result = image.Extend(view.begin(), ++view.begin());
    EXPECT_FALSE(extend_result.is_error())
        << ViewCopyErrorString(std::move(extend_result).error_value());

    size_t idx = 0;
    for (auto [header, payload] : image) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          DestTestTraits::Read(image.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(
          GetExpectedPayload(TestDataZbiType::kMultipleSmallItems, idx++, &expected));
      EXPECT_EQ(expected, actual);
    }
    EXPECT_EQ(1u, idx);
  }

  // [begin() + 1, end())
  {
    auto extend_result = image.Extend(++view.begin(), view.end());
    EXPECT_FALSE(extend_result.is_error())
        << ViewCopyErrorString(std::move(extend_result).error_value());

    size_t idx = 0;
    for (auto [header, payload] : image) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURE(
          DestTestTraits::Read(image.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURE(
          GetExpectedPayload(TestDataZbiType::kMultipleSmallItems, idx++, &expected));
      EXPECT_EQ(expected, actual);
    }
    EXPECT_EQ(GetExpectedNumberOfItems(TestDataZbiType::kMultipleSmallItems), idx);
  }

  {
    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
  }
  {
    auto result = image.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(std::move(result).error_value());
  }
}

// Ensures that the relevant macros expansions of TEST_EXTENDING's arguments
// happen as expected.
#define TEST_EXTENDING_1(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name) \
  TEST(suite_name, Extend##dest_name##With##src_name) {                                  \
    auto test = TestExtending<SrcTestTraits, DestTestTraits>;                            \
    ASSERT_NO_FATAL_FAILURE(test());                                                     \
  }

#define TEST_EXTENDING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name) \
  TEST_EXTENDING_1(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)

#endif  // SRC_LIB_ZBITL_TESTS_TESTS_H_
