// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_

#include <lib/zbitl/json.h>
#include <lib/zbitl/view.h>

#include <filesystem>
#include <string>
#include <type_traits>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "src/lib/files/scoped_temp_dir.h"

#define ASSERT_IS_OK(result)                                              \
  do {                                                                    \
    const auto& result_ = result;                                         \
    ASSERT_TRUE(result_.is_ok(), "unexpected error: %.*s",                \
                static_cast<int>(result_.error_value().zbi_error.size()), \
                result_.error_value().zbi_error.data());                  \
  } while (0)

constexpr size_t kMaxZbiSize = 4192;

constexpr uint32_t kItemType = ZBI_TYPE_IMAGE_ARGS;

enum class TestDataZbiType {
  kEmpty,
  kOneItem,
  kBadCrcItem,
  kMultipleSmallItems,
  kSecondItemOnPageBoundary,
};

size_t GetExpectedNumberOfItems(TestDataZbiType type);

void GetExpectedPayload(TestDataZbiType type, size_t idx, std::string* payload);

std::string GetExpectedJson(TestDataZbiType type);

void OpenTestDataZbi(TestDataZbiType type, std::string_view work_dir, fbl::unique_fd* fd,
                     size_t* num_bytes);

// Usage of StorageIo below is a default-constructible class that should look
// like
// * a namespace member of `storage_type`, giving the underlying storage type.
// * a means of creating a 'ZBI' of that type from file contents:
//   ```
//  void Create(fbl::unique_fd fd;, size_t size, storage_type* zbi)
//   ```
//   The StorageIo object can store state if storage_type is a non-owning
//   type referring to some different underlying type holding the contents.
//   Only one Create call will be made per StorageIo object, and the call takes
//   ownership of the descriptor.
// * a means of reading the payload of an item:
//   ```
//  void ReadPayload(const storage_type& zbi, const zbi_header_t& header, payload_type payload)
//   ```
// (where payload_type is that of the official storage traits associated with
// storage_type.)

template <typename StorageIo>
inline void TestDefaultConstructedView(bool expect_storage_error) {
  static_assert(std::is_default_constructible_v<typename StorageIo::storage_type>,
                "this test case only applies to default-constructible storage types");

  zbitl::View<typename StorageIo::storage_type> view;

  // This ensures that everything statically compiles when instantiating the
  // templates, even though the header/payloads are never used.
  for (auto [header, payload] : view) {
    EXPECT_EQ(header->flags, header->flags);
    EXPECT_TRUE(false, "should not be reached");
  }

  auto error = view.take_error();
  ASSERT_TRUE(error.is_error(), "no error when header cannot be read??");
  EXPECT_FALSE(error.error_value().zbi_error.empty(), "empty zbi_error string!!");
  if (expect_storage_error) {
    EXPECT_TRUE(error.error_value().storage_error.has_value());
  } else {
    EXPECT_FALSE(error.error_value().storage_error.has_value());
  }
}

template <typename StorageIo, zbitl::Checking checking>
inline void TestIteration(TestDataZbiType type) {
  StorageIo io;
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  typename StorageIo::storage_type zbi;
  ASSERT_NO_FATAL_FAILURES(io.Create(std::move(fd), size, &zbi));
  zbitl::View<typename StorageIo::storage_type, checking> view(std::move(zbi));

  ASSERT_IS_OK(view.container_header());

  size_t num_items = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(kItemType, header->type);

    std::string actual;
    ASSERT_NO_FATAL_FAILURES(io.ReadPayload(view.storage(), *header, payload, &actual));
    std::string expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, num_items++, &expected));
    EXPECT_STR_EQ(expected.c_str(), actual.c_str());

    const uint32_t flags = header->flags;
    EXPECT_TRUE(flags & ZBI_FLAG_VERSION, "flags: %#x", flags);
  }
  EXPECT_EQ(GetExpectedNumberOfItems(type), num_items);

  auto error = view.take_error();
  EXPECT_FALSE(error.is_error(), "%s at offset %#x",
               std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
               error.error_value().item_offset);
}

#define TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, checking_name, checking, \
                                            type_name, type)                                \
  TEST(suite_name, type_name##checking_name##Iteration) {                                   \
    auto test = TestIteration<StorageIo, checking>;                                         \
    ASSERT_NO_FATAL_FAILURES(test(type));                                                   \
  }

// Note: using the CRC32-checking in tests is a cheap and easy way to verify
// that the storage type is delivering the correct payload data.
#define TEST_ITERATIONS_BY_TYPE(suite_name, StorageIo, type_name, type)                        \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, Permissive,                       \
                                      zbitl::Checking::kPermissive, type_name, type)           \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, Strict, zbitl::Checking::kStrict, \
                                      type_name, type)                                         \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, Crc, zbitl::Checking::kCrc,       \
                                      type_name, type)

#define TEST_ITERATIONS(suite_name, StorageIo)                                                 \
  TEST_ITERATIONS_BY_TYPE(suite_name, StorageIo, EmptyZbi, TestDataZbiType::kEmpty)            \
  TEST_ITERATIONS_BY_TYPE(suite_name, StorageIo, OneItemZbi, TestDataZbiType::kOneItem)        \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, Permissive,                       \
                                      zbitl::Checking::kPermissive, BadCrcZbi,                 \
                                      TestDataZbiType::kBadCrcItem)                            \
  TEST_ITERATION_BY_CHECKING_AND_TYPE(suite_name, StorageIo, Strict, zbitl::Checking::kStrict, \
                                      BadCrcZbi, TestDataZbiType::kBadCrcItem)                 \
  TEST_ITERATIONS_BY_TYPE(suite_name, StorageIo, MultipleSmallItemsZbi,                        \
                          TestDataZbiType::kMultipleSmallItems)                                \
  TEST_ITERATIONS_BY_TYPE(suite_name, StorageIo, SecondItemOnPageBoundaryZbi,                  \
                          TestDataZbiType::kSecondItemOnPageBoundary)

template <typename StorageIo>
void TestCrcCheckFailure() {
  StorageIo io;
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(TestDataZbiType::kBadCrcItem, dir.path(), &fd, &size));

  typename StorageIo::storage_type zbi;
  ASSERT_NO_FATAL_FAILURES(io.Create(std::move(fd), size, &zbi));
  zbitl::View<typename StorageIo::storage_type, zbitl::Checking::kCrc> view(std::move(zbi));

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

template <typename StorageIo>
void TestMutation(TestDataZbiType type) {
  StorageIo io;
  files::ScopedTempDir dir;

  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURES(OpenTestDataZbi(type, dir.path(), &fd, &size));

  size_t expected_num_items = GetExpectedNumberOfItems(type);

  typename StorageIo::storage_type zbi;
  ASSERT_NO_FATAL_FAILURES(io.Create(std::move(fd), size, &zbi));
  zbitl::View view(std::move(zbi));  // Yay deduction guides.

  ASSERT_IS_OK(view.container_header());

  size_t num_items = 0;
  for (auto it = view.begin(); it != view.end(); ++it) {
    auto [header, payload] = *it;

    EXPECT_EQ(kItemType, header->type);

    std::string actual;
    ASSERT_NO_FATAL_FAILURES(io.ReadPayload(view.storage(), *header, payload, &actual));
    std::string expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, num_items++, &expected));
    EXPECT_STR_EQ(expected.c_str(), actual.c_str());

    ASSERT_TRUE(view.EditHeader(it, {.type = ZBI_TYPE_DISCARD}).is_ok());
  }
  EXPECT_EQ(expected_num_items, num_items);

  {
    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }

  num_items = 0;
  for (auto [header, payload] : view) {
    EXPECT_EQ(ZBI_TYPE_DISCARD, header->type);

    std::string actual;
    ASSERT_NO_FATAL_FAILURES(io.ReadPayload(view.storage(), *header, payload, &actual));
    std::string expected;
    ASSERT_NO_FATAL_FAILURES(GetExpectedPayload(type, num_items++, &expected));
    EXPECT_STR_EQ(expected.c_str(), actual.c_str());
  }
  EXPECT_EQ(expected_num_items, num_items);

  {
    auto error = view.take_error();
    EXPECT_FALSE(error.is_error(), "%s at offset %#x",
                 std::string(error.error_value().zbi_error).c_str(),  // No '\0'.
                 error.error_value().item_offset);
  }
}

#define TEST_MUTATION_BY_TYPE(suite_name, StorageIo, type_name, type) \
  TEST(suite_name, type_name##Mutation) { ASSERT_NO_FATAL_FAILURES(TestMutation<StorageIo>(type)); }

#define TEST_MUTATIONS(suite_name, StorageIo)                                               \
  TEST_MUTATION_BY_TYPE(suite_name, StorageIo, OneItemZbi, TestDataZbiType::kOneItem)       \
  TEST_MUTATION_BY_TYPE(suite_name, StorageIo, BadCrcItemZbi, TestDataZbiType::kBadCrcItem) \
  TEST_MUTATION_BY_TYPE(suite_name, StorageIo, MultipleSmallItemsZbi,                       \
                        TestDataZbiType::kMultipleSmallItems)                               \
  TEST_MUTATION_BY_TYPE(suite_name, StorageIo, SecondItemOnPageBoundaryZbi,                 \
                        TestDataZbiType::kSecondItemOnPageBoundary)

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_TESTS_H_
