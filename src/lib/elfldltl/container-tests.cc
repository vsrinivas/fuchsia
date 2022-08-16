// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/container.h>
#include <lib/elfldltl/diagnostics.h>

#include <string_view>

#include <zxtest/zxtest.h>

using namespace std::literals;

namespace {

TEST(ElfldltlContainerTests, Basic) {
  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors);

  elfldltl::StdContainer<std::vector>::Container<int> list;

  EXPECT_TRUE(list.push_back(diag, "", 1));
  EXPECT_TRUE(list.emplace_back(diag, ""sv, 3));
  EXPECT_TRUE(list.emplace(diag, ""sv, list.begin() + 1, 2));
  EXPECT_TRUE(list.insert(diag, "", list.begin(), 0));

  auto expected = {0, 1, 2, 3};
  EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  EXPECT_EQ(diag.errors() + diag.warnings(), 0);
}

TEST(ElfldltlContainerTests, ForwardArgs) {
  std::vector<std::string> errors;
  auto diag = elfldltl::CollectStringsDiagnostics(errors);

  elfldltl::StdContainer<std::vector>::Container<std::pair<int, int>> list;

  EXPECT_TRUE(list.push_back(diag, "", std::pair<int, int>{1, 2}));
  EXPECT_TRUE(list.emplace_back(diag, ""sv, 2, 3));
  EXPECT_TRUE(list.emplace(diag, ""sv, list.end(), 3, 4));
  EXPECT_TRUE(list.insert(diag, "", list.end(), std::pair<int, int>{4, 5}));

  std::array<std::pair<int, int>, 4> expected{{{1, 2}, {2, 3}, {3, 4}, {4, 5}}};
  EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  EXPECT_EQ(diag.errors() + diag.warnings(), 0);
}

TEST(ElfldltlContainerTests, TemplateArgs) {
  static bool called = false;
  struct Allocator : public std::allocator<int> {
    int *allocate(size_t size) {
      called = true;
      return std::allocator<int>::allocate(size);
    }
  };

  ASSERT_FALSE(called);
  elfldltl::StdContainer<std::vector, Allocator>::Container<int> list;
  list.reserve(10);

  EXPECT_TRUE(called);
}

}  // namespace
