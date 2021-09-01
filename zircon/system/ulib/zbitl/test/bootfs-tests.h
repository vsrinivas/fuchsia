// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZBITL_TEST_BOOTFS_TESTS_H_
#define ZIRCON_SYSTEM_ULIB_ZBITL_TEST_BOOTFS_TESTS_H_

#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>

#include <iterator>

#include <fbl/unique_fd.h>
#include <gtest/gtest.h>

#include "array-tests.h"
#include "fd-tests.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "tests.h"

template <typename TestTraits>
void TestBootfsIteration() {
  using namespace std::string_view_literals;
  using Storage = typename TestTraits::storage_type;

  files::ScopedTempDir dir;
  fbl::unique_fd fd;
  size_t size = 0;
  ASSERT_NO_FATAL_FAILURE(OpenTestDataZbi(TestDataZbiType::kBootfs, dir.path(), &fd, &size));

  // Read the ZBI containing the BOOTFS into memory.
  typename FblByteArrayTestTraits::Context zbi_context;
  ASSERT_NO_FATAL_FAILURE(FblByteArrayTestTraits::Create(std::move(fd), size, &zbi_context));
  zbitl::View view(zbi_context.TakeStorage());

  auto it = view.begin();
  ASSERT_EQ(std::next(it), view.end()) << "expected a single BOOTFS item";
  ASSERT_EQ(uint32_t{ZBI_TYPE_STORAGE_BOOTFS}, it->header->type);

  // Ultimately we want to create an object of storage_type containing the
  // BOOTFS - and the preferred choice of test traits for creating storage
  // objects with prescribed contents is to use a fbl::unique_fd. Accordingly,
  // we decompress the BOOTFS into this form.
  const uint32_t bootfs_size = zbitl::UncompressedLength(*it->header);
  typename FdTestTraits::Context decompressed_context;
  ASSERT_NO_FATAL_FAILURE(FdTestTraits::Create(bootfs_size, &decompressed_context));

  fbl::unique_fd bootfs_fd = decompressed_context.TakeStorage();
  {
    auto result = view.CopyStorageItem(bootfs_fd, it);
    ASSERT_FALSE(result.is_error()) << ViewCopyErrorString(result.error_value());
  }

  typename TestTraits::Context bootfs_context;
  ASSERT_NO_FATAL_FAILURE(TestTraits::Create(std::move(bootfs_fd), bootfs_size, &bootfs_context));

  zbitl::BootfsView<Storage> bootfs;
  {
    auto result = zbitl::BootfsView<Storage>::Create(bootfs_context.TakeStorage());
    ASSERT_FALSE(result.is_error()) << BootfsErrorString(result.error_value());
    bootfs = std::move(result.value());
  }

  auto test_find = [&bootfs](auto expected_it, std::initializer_list<std::string_view> path_parts) {
    auto match = bootfs.find(path_parts);
    auto result = bootfs.take_error();
    ASSERT_FALSE(result.is_error()) << BootfsErrorString(result.error_value());
    EXPECT_EQ(expected_it, match);
  };

  uint32_t idx = 0;
  for (auto it = bootfs.begin(); it != bootfs.end(); ++it) {
    Bytes contents;
    ASSERT_NO_FATAL_FAILURE(TestTraits::Read(bootfs.storage(), it->data, it->size, &contents));

    ASSERT_NO_FATAL_FAILURE(test_find(it, {it->name}));
    ASSERT_NO_FATAL_FAILURE(test_find(it, {it->name}));
    switch (idx) {
      case 0:
        EXPECT_EQ(it->name, "A.txt"sv);
        EXPECT_EQ(contents,
                  "Four score and seven years ago our fathers brought forth on this "
                  "continent, a new nation, conceived in Liberty, and dedicated to the "
                  "proposition that all men are created equal.");
        break;
      case 1:
        EXPECT_EQ(it->name, "nested/B.txt"sv);
        ASSERT_NO_FATAL_FAILURE(test_find(it, {"nested"sv, "B.txt"sv}));
        EXPECT_EQ(contents,
                  "Now we are engaged in a great civil war, testing whether that nation, "
                  "or any nation so conceived and so dedicated, can long endure.");
        break;
      case 2:
        EXPECT_EQ(it->name, "nested/again/C.txt"sv);
        ASSERT_NO_FATAL_FAILURE(test_find(it, {"nested/again"sv, "C.txt"sv}));
        ASSERT_NO_FATAL_FAILURE(test_find(it, {"nested"sv, "again/C.txt"sv}));
        ASSERT_NO_FATAL_FAILURE(test_find(it, {"nested"sv, "again"sv, "C.txt"sv}));
        EXPECT_EQ(contents, "We are met on a great battle-field of that war.");
        break;
      default:
        __UNREACHABLE;
    }

    ++idx;
  }
  EXPECT_EQ(3u, idx) << "we expect three files in the BOOTFS";

  {
    auto result = view.take_error();
    EXPECT_FALSE(result.is_error()) << ViewErrorString(result.error_value());
  }

  {
    auto result = bootfs.take_error();
    EXPECT_FALSE(result.is_error()) << BootfsErrorString(result.error_value());
  }
}

#endif  // ZIRCON_SYSTEM_ULIB_ZBITL_TEST_BOOTFS_TESTS_H_
