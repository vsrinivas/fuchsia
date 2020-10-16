// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_

#include <lib/zbitl/json.h>
#include <lib/zbitl/view.h>

#include <iterator>
#include <string>
#include <type_traits>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "copy-tests.h"
#include "src/lib/files/scoped_temp_dir.h"

#define ASSERT_IS_OK(result)                                              \
  do {                                                                    \
    const auto& result_ = result;                                         \
    ASSERT_TRUE(result_.is_ok(), "unexpected error: %.*s",                \
                static_cast<int>(result_.error_value().zbi_error.size()), \
                result_.error_value().zbi_error.data());                  \
  } while (0)

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

//
// Helpers for accessing test data.
//

size_t GetExpectedNumberOfItems(TestDataZbiType type);

void GetExpectedPayload(TestDataZbiType type, size_t idx, Bytes* contents);

void GetExpectedPayloadWithHeader(TestDataZbiType type, size_t idx, Bytes* contents);

std::string GetExpectedJson(TestDataZbiType type);

void OpenTestDataZbi(TestDataZbiType type, std::string_view work_dir, fbl::unique_fd* fd,
                     size_t* num_bytes);

// Annoyingly, we are unable to currently use std::next on zbitl::View's
// iterator, as that requires the class to define `value_type` in its namespace
// (among other things) - and neither GCC nor Clang acknowledge that it is
// ineherited from the parent namespace by default, and GCC can't abide by
// explicitly redefining it in the former.
//
// `auto it` ensures a decayed copy/move into the `it` argument, so a call
// should not modify the input iterator.
constexpr auto Next = [](auto it) { return ++it; };

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
//
// If the storage type is default-constructible, the trait must have a static
// constexpr Boolean member `kDefaultConstructedViewHasStorageError` indicating
// whether a default-constructed view of that storage type yields an error on
// iteration.
//
// If the storage type is writable, it must also provide:
//   *`static payload_type AsPayload(storage_type&)` that returns the payload
//     value representing the entire storage object.
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
    EXPECT_TRUE(false, "should not be reached");
  }

  auto error = view.take_error();
  ASSERT_TRUE(error.is_error(), "no error when header cannot be read??");
  EXPECT_FALSE(error.error_value().zbi_error.empty(), "empty zbi_error string!!");
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
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View<Storage, Checking> view(context.TakeStorage());

  ASSERT_IS_OK(view.container_header());

  size_t idx = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(kItemType, header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURES(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());

    const uint32_t flags = header->flags;
    EXPECT_TRUE(flags & ZBI_FLAG_VERSION, "flags: %#x", flags);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

#define TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, TestTraits, checking_name, checking, \
                                            type_name, type)                                 \
  TEST(suite_name, type_name##checking_name##Iteration) {                                    \
    auto test = TestIteration<TestTraits, checking>;                                         \
    ASSERT_NO_FATAL_FAILURES(test(type));                                                    \
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
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(TestDataZbiType::kBadCrcItem, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::CrcCheckingView<Storage> view(context.TakeStorage());

  ASSERT_IS_OK(view.container_header());

  for (auto [header, payload] : view) {
    EXPECT_EQ(header->type, header->type);
    EXPECT_TRUE(false, "should not be reached");
  }
  auto error = view.take_error();
  ASSERT_TRUE(error.is_error());
  // The error shouldn't be one of storage.
  EXPECT_FALSE(error.error_value().storage_error, "%.*s",
               static_cast<int>(error.error_value().zbi_error.size()),
               error.error_value().zbi_error.data());

  // For the file types with errno for error_type, print the storage error.
  if constexpr (std::is_same_v<std::decay_t<decltype(error.error_value().storage_error.value())>,
                               int>) {
    EXPECT_FALSE(error.error_value().storage_error, "%d (%s)", *error.error_value().storage_error,
                 strerror(*error.error_value().storage_error));
  }

  // This matches the exact error text, so it has to be kept in sync.
  // But otherwise we're not testing that the right error is diagnosed.
  EXPECT_STR_EQ(error.error_value().zbi_error, "item CRC32 mismatch");
}

template <typename TestTraits>
void TestMutation(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());  // Yay deduction guides.

  ASSERT_IS_OK(view.container_header());

  size_t expected_num_items = GetExpectedNumberOfItems(type);

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    auto [header, payload] = *it;

    EXPECT_EQ(kItemType, header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURES(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &expected));
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());

    ASSERT_TRUE(view.EditHeader(it, {.type = ZBI_TYPE_DISCARD}).is_ok());
  }
  EXPECT_EQ(expected_num_items, idx);

  {
    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  idx = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(ZBI_TYPE_DISCARD, header->type);

    Bytes actual;
    ASSERT_NO_FATAL_FAILURES(TestTraits::Read(view.storage(), payload, header->length, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
  }
  EXPECT_EQ(expected_num_items, idx);

  {
    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }
}

#define TEST_MUTATION_BY_TYPE(suite_name, TestTraits, type_name, type) \
  TEST(suite_name, type_name##Mutation) {                              \
    ASSERT_NO_FATAL_FAILURES(TestMutation<TestTraits>(type));          \
  }

#define TEST_MUTATION(suite_name, TestTraits)                                                \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem)       \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, BadCrcItemZbi, TestDataZbiType::kBadCrcItem) \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                       \
                        TestDataZbiType::kMultipleSmallItems)                                \
  TEST_MUTATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,                 \
                        TestDataZbiType::kSecondItemOnPageBoundary)

template <typename TestTraits>
void TestCopyCreation(TestDataZbiType type, bool with_header) {
  using Storage = typename TestTraits::storage_type;
  static_assert(zbitl::View<Storage>::CanCopyCreate());
  using CreationTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *((*it).header);
    const size_t created_size = header.length + (with_header ? sizeof(header) : 0);

    auto result = with_header ? view.CopyRawItemWithHeader(it) : view.CopyRawItem(it);
    ASSERT_TRUE(result.is_ok(), "item %zu, %s header: %s", idx, with_header ? "with" : "without",
                CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "item %zu, %s header: %s", idx,
                with_header ? "with" : "without", CopyResultErrorMsg(std::move(result)).c_str());

    auto created = std::move(result).value().value();

    Bytes actual;
    auto created_payload = CreationTraits::AsPayload(created);
    ASSERT_NO_FATAL_FAILURES(CreationTraits::Read(created, created_payload, created_size, &actual));

    Bytes expected;
    if (with_header) {
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayloadWithHeader(type, idx, &expected));
    } else {
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &expected));
    }
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

// We simply test in this case that we are able to copy-create the byte ranges
// associated with the item payloads. More strenuous exercise of the interface
// is done under-the-hood by the other copy-creation tests.
template <typename TestTraits>
void TestCopyCreationByByteRange(TestDataZbiType type) {
  using Storage = typename TestTraits::storage_type;
  static_assert(zbitl::View<Storage>::CanCopyCreate());
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    uint32_t payload_size = (*it).header->length;
    auto result = view.Copy(it.payload_offset(), payload_size);
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    auto created = std::move(result).value().value();

    Bytes actual;
    auto created_payload = CreationTestTraits::AsPayload(created);
    ASSERT_NO_FATAL_FAILURES(
        CreationTestTraits::Read(created, created_payload, payload_size, &actual));
    Bytes expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &expected));
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

template <typename TestTraits>
void TestCopyCreationByIteratorRange(TestDataZbiType type) {
  using Storage = typename TestTraits::storage_type;
  static_assert(zbitl::View<Storage>::CanCopyCreate());
  using CreationTestTraits = typename TestTraits::creation_traits;

  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename TestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(TestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  // [begin(), begin() + 1).
  {
    auto first = view.begin();
    auto result = view.Copy(first, Next(first));
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    auto created = std::move(result).value().value();

    zbitl::View created_view(std::move(created));
    ASSERT_IS_OK(created_view.container_header());

    size_t idx = 0;
    for (auto [header, payload] : created_view) {
      EXPECT_EQ(kItemType, header->type);

      Bytes actual;
      ASSERT_NO_FATAL_FAILURES(
          CreationTestTraits::Read(created_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
      ASSERT_EQ(expected.size(), actual.size());
      EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());

      const uint32_t flags = header->flags;
      EXPECT_TRUE(flags & ZBI_FLAG_VERSION, "flags: %#x", flags);
    }
    EXPECT_EQ(1, idx);

    auto error = created_view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  // [begin() + 1, end()).
  if (Next(view.begin()) != view.end()) {
    auto result = view.Copy(Next(view.begin()), view.end());
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    auto created = std::move(result).value().value();

    zbitl::View created_view(std::move(created));
    ASSERT_IS_OK(created_view.container_header());

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
      ASSERT_NO_FATAL_FAILURES(
          CreationTestTraits::Read(created_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &expected));
      ASSERT_EQ(expected.size(), actual.size());
      EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());

      const uint32_t flags = header->flags;
      EXPECT_TRUE(flags & ZBI_FLAG_VERSION, "flags: %#x", flags);
    }
    EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

    auto error = created_view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

#define TEST_COPY_CREATION_BY_TYPE_AND_OPTION(suite_name, TestTraits, type_name, type, \
                                              with_header, with_header_name)           \
  TEST(suite_name, type_name##CopyCreation##with_header_name) {                        \
    ASSERT_NO_FATAL_FAILURES(TestCopyCreation<TestTraits>(type, with_header));         \
  }

#define TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, type_name, type)                        \
  TEST_COPY_CREATION_BY_TYPE_AND_OPTION(suite_name, TestTraits, type_name, type, true, WithHeader) \
  TEST_COPY_CREATION_BY_TYPE_AND_OPTION(suite_name, TestTraits, type_name, type, false, )          \
  TEST(suite_name, type_name##CopyCreationByByteRange) {                                           \
    ASSERT_NO_FATAL_FAILURES(TestCopyCreationByByteRange<TestTraits>(type));                       \
  }                                                                                                \
  TEST(suite_name, type_name##CopyCreationByIteratorRange) {                                       \
    ASSERT_NO_FATAL_FAILURES(TestCopyCreationByIteratorRange<TestTraits>(type));                   \
  }

#define TEST_COPY_CREATION(suite_name, TestTraits)                                          \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, OneItemZbi, TestDataZbiType::kOneItem) \
  TEST_COPY_CREATION_BY_TYPE_AND_OPTION(suite_name, TestTraits, BadCrcItemZbi,              \
                                        TestDataZbiType::kBadCrcItem, false, )              \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, MultipleSmallItemsZbi,                 \
                             TestDataZbiType::kMultipleSmallItems)                          \
  TEST_COPY_CREATION_BY_TYPE(suite_name, TestTraits, SecondItemOnPageBoundaryZbi,           \
                             TestDataZbiType::kSecondItemOnPageBoundary)

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopying(TestDataZbiType type, bool with_header) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it, ++idx) {
    const zbi_header_t header = *((*it).header);
    const size_t copy_size = header.length + (with_header ? sizeof(header) : 0);

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Create(copy_size, &copy_context));
    auto copy = copy_context.TakeStorage();
    auto result = with_header ? view.CopyRawItemWithHeader(std::move(copy), it)
                              : view.CopyRawItem(std::move(copy), it);
    ASSERT_TRUE(result.is_ok(), "item %zu, %s header: %s", idx, with_header ? "with" : "without",
                CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "item %zu, %s header: %s", idx,
                with_header ? "with" : "without", CopyResultErrorMsg(std::move(result)).c_str());

    Bytes actual;
    auto copy_payload = DestTestTraits::AsPayload(copy);
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Read(copy, copy_payload, copy_size, &actual));

    Bytes expected;
    if (with_header) {
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayloadWithHeader(type, idx, &expected));
    } else {
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx, &expected));
    }
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

// We simply test in this case that we are able to copy the byte ranges
// associated with the item payloads. More strenuous exercise of the
// interface is done under-the-hood by the other copy tests.
template <typename SrcTestTraits, typename DestTestTraits>
void TestCopyingByByteRange(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  size_t idx = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    uint32_t payload_size = (*it).header->length;

    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Create(payload_size, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto result = view.Copy(copy, it.payload_offset(), payload_size);
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    Bytes actual;
    auto copy_payload = DestTestTraits::AsPayload(copy);
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Read(copy, copy_payload, payload_size, &actual));

    Bytes expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
    ASSERT_EQ(expected.size(), actual.size());
    EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

template <typename SrcTestTraits, typename DestTestTraits>
void TestCopyingByIteratorRange(TestDataZbiType type) {
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename SrcTestTraits::Context context;
  ASSERT_NO_FATAL_FAILURES(SrcTestTraits::Create(std::move(fd), size, &context));
  zbitl::View view(context.TakeStorage());

  // [begin(), begin() + 1)
  {
    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto first = view.begin();
    auto result = view.Copy(copy, first, Next(view.begin()));
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    zbitl::View copy_view(std::move(copy));
    ASSERT_IS_OK(copy_view.container_header());

    size_t idx = 0;
    for (auto [header, payload] : copy_view) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURES(
          DestTestTraits::Read(copy_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
      ASSERT_EQ(expected.size(), actual.size());
      EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
    }
    EXPECT_EQ(1, idx);

    auto error = copy_view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  // [begin() + 1, end()).
  if (Next(view.begin()) != view.end()) {
    typename DestTestTraits::Context copy_context;
    ASSERT_NO_FATAL_FAILURES(DestTestTraits::Create(kMaxZbiSize, &copy_context));
    auto copy = copy_context.TakeStorage();

    auto first = Next(view.begin());
    auto result = view.Copy(copy, first, view.end());
    ASSERT_TRUE(result.is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());
    ASSERT_TRUE(result.value().is_ok(), "%s", CopyResultErrorMsg(std::move(result)).c_str());

    zbitl::View copy_view(std::move(copy));
    ASSERT_IS_OK(copy_view.container_header());

    size_t idx = 1;  // Corresponding to begin() + 1.
    for (auto [header, payload] : copy_view) {
      Bytes actual;
      ASSERT_NO_FATAL_FAILURES(
          DestTestTraits::Read(copy_view.storage(), payload, header->length, &actual));
      Bytes expected;
      ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, idx++, &expected));
      ASSERT_EQ(expected.size(), actual.size());
      EXPECT_BYTES_EQ(expected.data(), actual.data(), expected.size());
    }
    EXPECT_EQ(GetExpectedNumberOfItems(type), idx);

    auto error = copy_view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

#define TEST_COPYING_BY_TYPE_AND_OPTION(suite_name, SrcTestTraits, src_name, DestTestTraits,       \
                                        dest_name, type_name, type, with_header, with_header_name) \
  TEST(suite_name, type_name##Copy##src_name##to##dest_name##with_header_name) {                   \
    auto test = TestCopying<SrcTestTraits, DestTestTraits>;                                        \
    ASSERT_NO_FATAL_FAILURES(test(type, with_header));                                             \
  }

#define TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,      \
                             type_name, type)                                                     \
  TEST_COPYING_BY_TYPE_AND_OPTION(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, \
                                  type_name, type, true, WithHeader)                              \
  TEST_COPYING_BY_TYPE_AND_OPTION(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, \
                                  type_name, type, false, )                                       \
  TEST(suite_name, type_name##Copy##src_name##to##dest_name##ByByteRange) {                       \
    auto test = TestCopyingByByteRange<SrcTestTraits, DestTestTraits>;                            \
    ASSERT_NO_FATAL_FAILURES(test(type));                                                         \
  }                                                                                               \
  TEST(suite_name, type_name##Copy##src_name##to##dest_name##ByIteratorRange) {                   \
    auto test = TestCopyingByIteratorRange<SrcTestTraits, DestTestTraits>;                        \
    ASSERT_NO_FATAL_FAILURES(test(type));                                                         \
  }

#define TEST_COPYING(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name)               \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name, OneItemZbi, \
                       TestDataZbiType::kOneItem)                                                  \
  TEST_COPYING_BY_TYPE_AND_OPTION(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,  \
                                  BadCrcItemZbi, TestDataZbiType::kBadCrcItem, false, )            \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       MultipleSmallItemsZbi, TestDataZbiType::kMultipleSmallItems)                \
  TEST_COPYING_BY_TYPE(suite_name, SrcTestTraits, src_name, DestTestTraits, dest_name,             \
                       SecondItemOnPageBoundaryZbi, TestDataZbiType::kSecondItemOnPageBoundary)

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
