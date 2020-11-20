// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_

#include <lib/zbitl/error_string.h>
#include <lib/zbitl/image.h>
#include <lib/zbitl/json.h>
#include <lib/zbitl/view.h>

#include <iterator>
#include <string>
#include <type_traits>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "src/lib/files/scoped_temp_dir.h"

// While it is convenient to use std::string as a container in representing ZBI
// content, we alias the type to convey that it need not necessarily represent
// text.
using Bytes = std::string;

constexpr size_t kMaxZbiSize = 4192;

constexpr uint32_t kItemType = ZBI_TYPE_IMAGE_ARGS;

enum class TestDataZbiType {
  kEmpty,
  kOneItem,
  kBadCrcItem,
  kMultipleSmallItems,
  kSecondItemOnPageBoundary,
};

// Parameterizes the behavior of copying a single item.
enum class ItemCopyMode {
  // Copy just the payload.
  kRaw,
  // Copy the header and payload.
  kWithHeader,
};

//
// Helpers for accessing test data.
//

size_t GetExpectedNumberOfItems(TestDataZbiType type);

void GetExpectedPayload(TestDataZbiType type, size_t idx, Bytes* contents);

void GetExpectedPayloadWithHeader(TestDataZbiType type, size_t idx, Bytes* contents);

std::string GetExpectedJson(TestDataZbiType type);

void OpenTestDataZbi(TestDataZbiType type, std::string_view work_dir, fbl::unique_fd* fd,
                     size_t* num_bytes);

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
//   * a static constexpr bool `kExpectOneshotReads` giving the expectation of
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
//   * `static payload_type AsPayload(storage_type&)` that returns the payload
//     value representing the entire storage object.
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
    EXPECT_TRUE(false) << "should not be reached";
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

template <typename TestTraits, zbitl::Checking Checking>
inline void TestIteration(TestDataZbiType type) {
  using Storage = typename TestTraits::storage_type;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View<Storage, Checking> view(context.TakeStorage());

  auto container_result = view.container_header();
  ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

  size_t idx = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(kItemType, header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx++, &expected));
    EXPECT_EQ(expected, actual);

    const uint32_t flags = header->flags;
    EXPECT_TRUE(flags & ZBI_FLAG_VERSION) << "flags: 0x" << std::hex << flags;
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto result = view.take_error();
  EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
}

#define TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, checking_name, checking, \
                                            type_name, type)                                 \
  TEST(suite_name, type_name##checking_name##Iteration) {                                    \
    auto test = TestIteration<TestTraits, checking>;                                         \
    ASSERT_NO_FATAL_FAILURE(test(type));                                                     \
  }

// Note: using the CRC32-checking in tests is a cheap and easy way to verify
// that the storage type is delivering the correct payload data.
#define TEST_ITERATION_BY_TYPE(suite_name, TestTraits, type_name, type)                         \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, Permissive,                       \
                                      zbitl::Checking::kPermissive, type_name, type)            \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, Strict, zbitl::Checking::kStrict, \
                                      type_name, type)                                          \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, Crc, zbitl::Checking::kCrc,       \
                                      type_name, type)

#define TEST_ITERATION(suite_name, TestTraits)                                                  \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, EmptyZbi, TestDataZbiType::kEmpty)             \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem)         \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, Permissive,                       \
                                      zbitl::Checking::kPermissive, BadCrcZbi,                  \
                                      TestDataZbiType::kBadCrcItem)                             \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, Strict, zbitl::Checking::kStrict, \
                                      BadCrcZbi, TestDataZbiType::kBadCrcItem)                  \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                         \
                         TestDataZbiType::kMultipleSmallItems)                                  \
  TEST_ITERATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,                   \
                         TestDataZbiType::kSecondItemOnPageBoundary)

template <typename TestTraits>
void TestCrcCheckFailure() {
  using Storage = typename TestTraits::storage_type;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kBadCrcItem, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(fd), size, &context));
  zbitl::CrcCheckingView<Storage> view(context.TakeStorage());

  auto container_result = view.container_header();
  ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

  for (auto [header, payload] : view) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false) << "should not be reached";
  }
  auto error = view.take_error();

  ASSERT_TRUE(error.is_error());
  // The error shouldn't be one of storage.
  EXPECT_FALSE(error.error_value().storage_error) << error.error_value().zbi_error;

  // For the file types with errno for error_type, print the storage error.
  if constexpr (std::is_same_v<std::decay_t<decltype(error.error_value().storage_error.value())>,
                               int>) {
    EXPECT_FALSE(error.error_value().storage_error) << *error.error_value().storage_error << ": "
                                                    << strerror(*error.error_value().storage_error);
  }

  // This matches the exact error text, so it has to be kept in sync.
  // But otherwise we're not testing that the right error is diagnosed.
  EXPECT_EQ(error.error_value().zbi_error, "item CRC32 mismatch");
}

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

    EXPECT_EQ(kItemType, header->type);

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

template <typename TestTraits>
void TestCopyCreation(TestDataZbiType type, ItemCopyMode mode) {
  using CreationTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

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
    };
    __UNREACHABLE;
  };

  auto do_copy = [&mode, &view ](auto it) -> auto {
    switch (mode) {
      case ItemCopyMode::kRaw:
        return view.CopyRawItem(it);
      case ItemCopyMode::kWithHeader:
        return view.CopyRawItemWithHeader(it);
    };
    __UNREACHABLE;
  };

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *((*it).header);

    const size_t created_size = get_size(header);
    auto result = do_copy(it);
    ASSERT_TRUE(result.is_ok()) << "item " << idx << ": "
                                << ViewCopyErrorString(result.error_value());

    auto created = std::move(result).value();

    Bytes actual;
    auto created_payload = CreationTraits::AsPayload(created);
    ASSERT_NO_FATAL_FAILURE(CreationTraits::Read(created, created_payload, created_size, &actual));

    Bytes expected;
    switch (mode) {
      case ItemCopyMode::kRaw:
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
    uint32_t payload_size = (*it).header->length;
    auto result = view.Copy(it.payload_offset(), payload_size);
    EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());

    auto created = std::move(result).value();

    Bytes actual;
    auto created_payload = CreationTestTraits::AsPayload(created);
    ASSERT_NO_FATAL_FAILURE(
        CreationTestTraits::Read(created, created_payload, payload_size, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURE(GetExpectedPayload(type, idx, &expected));
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

  // [begin(), begin() + 1).
  {
    auto first = view.begin();

    auto copy_result = view.Copy(first, std::next(first));
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();

    zbitl::View created_view(std::move(created));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    size_t idx = 0;
    for (auto [header, payload] : created_view) {
      EXPECT_EQ(kItemType, header->type);

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
  if (std::next(view.begin()) != view.end()) {
    auto copy_result = view.Copy(std::next(view.begin()), view.end());
    EXPECT_FALSE(copy_result.is_error()) << ViewCopyErrorString(copy_result.error_value());

    auto created = std::move(copy_result).value();

    zbitl::View created_view(std::move(created));
    auto container_result = view.container_header();
    ASSERT_FALSE(container_result.is_error()) << ViewErrorString(container_result.error_value());

    // We might have filled slop with a single discard element; skip if so.
    auto first = created_view.begin();
    if ((*first).header->type == ZBI_TYPE_DISCARD) {
      ++first;
    }

    size_t idx = 1;  // Corresponding to begin() + 1.
    for (auto it = first; it != created_view.end(); ++it, ++idx) {
      auto [header, payload] = *it;
      EXPECT_EQ(kItemType, header->type);

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
  ASSERT_NO_FATAL_FAILURE(DestTestTraits::Read(
      small_storage, DestTestTraits::AsPayload(small_storage), header->length, &actual));

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
  TEST(suite_name, type_name##CopyCreationByByteRange) {                        \
    ASSERT_NO_FATAL_FAILURE(TestCopyCreationByByteRange<TestTraits>(type));     \
  }                                                                             \
  TEST(suite_name, type_name##CopyCreationByIteratorRange) {                    \
    ASSERT_NO_FATAL_FAILURE(TestCopyCreationByIteratorRange<TestTraits>(type)); \
  }

#define TEST_COPY_CREATION(suite_name, TestTraits)                                          \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem) \
  TEST_COPY_CREATION_BY_TYPE_AND_MODE(suite_name, TestTraits, BadCrcItemZbi,                \
                                      TestDataZbiType::kBadCrcItem, ItemCopyMode::kRaw, )   \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                 \
                             TestDataZbiType::kMultipleSmallItems)                          \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,           \
                             TestDataZbiType::kSecondItemOnPageBoundary)

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopying(TestDataZbiType type, ItemCopyMode mode) {
  files::ScopedTempDir dir;

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
    };
    __UNREACHABLE;
  };

  auto do_copy = [&mode, &view ](auto&& storage, auto it) -> auto {
    using Storage = decltype(storage);
    switch (mode) {
      case ItemCopyMode::kRaw:
        return view.CopyRawItem(std::forward<Storage>(storage), it);
      case ItemCopyMode::kWithHeader:
        return view.CopyRawItemWithHeader(std::forward<Storage>(storage), it);
    };
    __UNREACHABLE;
  };

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *((*it).header);
    const size_t copy_size = get_size(header);

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(copy_size, &copy_context));
    auto copy = copy_context.TakeStorage();
    auto result = do_copy(std::move(copy), it);
    ASSERT_TRUE(result.is_ok()) << "item " << idx << ": "
                                << ViewCopyErrorString(result.error_value());

    Bytes actual;
    auto copy_payload = DestTestTraits::AsPayload(copy);
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Read(copy, copy_payload, copy_size, &actual));

    Bytes expected;
    switch (mode) {
      case ItemCopyMode::kRaw:
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
    uint32_t payload_size = (*it).header->length;

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(payload_size, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto result = view.Copy(copy, it.payload_offset(), payload_size);
    EXPECT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());

    Bytes actual;
    auto copy_payload = DestTestTraits::AsPayload(copy);
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

  // [begin(), begin() + 1)
  {
    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto first = view.begin();
    auto copy_result = view.Copy(copy, first, std::next(view.begin()));
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
    EXPECT_EQ(1u, idx);

    auto result = copy_view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  // [begin() + 1, end()).
  if (std::next(view.begin()) != view.end()) {
    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURE(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto first = std::next(view.begin());
    {
      auto result = view.Copy(copy, first, view.end());
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

template <typename SrcTestTraits, typename DestTestTraits>
void TestZeroCopying() {
  using SrcStorage = typename SrcTestTraits::storage_type;
  using DestStorage = typename DestTestTraits::storage_type;

  constexpr bool kCanZeroCopy = zbitl::View<SrcStorage>::template CanZeroCopy<DestStorage>();
  constexpr bool kExpectUnbufferedIo =
      SrcTestTraits::kExpectUnbufferedReads && DestTestTraits::kExpectUnbufferedWrites;
  static_assert(kCanZeroCopy == SrcTestTraits::kExpectOneshotReads || kExpectUnbufferedIo);
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
  TEST(suite_name, type_name##Copy##src_name##To##dest_name##ByByteRange) {                     \
    auto test = TestCopyingByByteRange<SrcTestTraits, DestTestTraits>;                          \
    ASSERT_NO_FATAL_FAILURE(test(type));                                                        \
  }                                                                                             \
  TEST(suite_name, type_name##Copy##src_name##To##dest_name##ByIteratorRange) {                 \
    auto test = TestCopyingByIteratorRange<SrcTestTraits, DestTestTraits>;                      \
    ASSERT_NO_FATAL_FAILURE(test(type));                                                        \
  }

// The macro indirection ensures that the relevant expansions in TEST_COPYING
// and those of its arguments happen as expected.
#define TEST_ZERO_COPYING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name) \
  TEST(suite_name, ZeroCopying##src_name##To##dest_name) {                                \
    auto test = TestZeroCopying<SrcTestTraits, DestTestTraits>;                           \
    ASSERT_NO_FATAL_FAILURE(test());                                                      \
  }
#define TEST_COPYING_INTO_SMALL_STORAGE(suite_name, SrcTestTraits, src_name, DestTestTraits, \
                                        dest_name)                                           \
  TEST(suite_name, Copying##src_name##to##dest_name##SmallStorage) {                         \
    auto test = TestCopyingIntoSmallStorage<SrcTestTraits, DestTestTraits>;                  \
    ASSERT_NO_FATAL_FAILURE(test());                                                         \
  }

#define TEST_COPYING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)               \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, OneItemZbi, \
                       TestDataZbiType::kOneItem)                                                  \
  TEST_COPYING_BY_TYPE_AND_MODE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,    \
                                BadCrcItemZbi, TestDataZbiType::kBadCrcItem, ItemCopyMode::kRaw, ) \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       MultipleSmallItemsZbi, TestDataZbiType::kMultipleSmallItems)                \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       SecondItemOnPageBoundaryZbi, TestDataZbiType::kSecondItemOnPageBoundary)    \
  TEST_ZERO_COPYING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)                \
  TEST_COPYING_INTO_SMALL_STORAGE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)

template <typename TestTraits>
void TestAppending() {
  using Storage = typename TestTraits::storage_type;

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

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(kInitialSize, &context));
  // Checking::kCrc will help ensure that we are appending items with valid
  // CRC32s in the append-with-payload API.
  zbitl::CrcCheckingImage<Storage> image(context.TakeStorage());

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

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
