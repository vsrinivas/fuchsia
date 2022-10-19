// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/elfldltl/container.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/static-vector.h>

#include <string_view>

#include <zxtest/zxtest.h>

#include "tests.h"

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

template <typename T>
struct Allocator : std::allocator<T> {
  static inline bool called;
  T *allocate(size_t size) {
    called = true;
    return std::allocator<T>::allocate(size);
  }

  template <typename U>
  struct rebind {
    using other = Allocator<U>;
  };
};

TEST(ElfldltlContainerTests, TemplateArgs) {
  ASSERT_FALSE(Allocator<int>::called);
  elfldltl::StdContainer<std::vector, Allocator<int>>::Container<int> list;
  list.reserve(10);

  EXPECT_TRUE(Allocator<int>::called);
}

template <typename List>
void CheckContainerAPI(List list) {
  EXPECT_EQ(list.max_size(), 10);
  EXPECT_EQ(list.capacity(), 10);
  cpp20::span<typename List::value_type> span = list.as_span();
  EXPECT_EQ(span.size(), 0);
  EXPECT_TRUE(list.data());
  EXPECT_EQ(list.size(), 0);
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(list.begin(), list.end());
  EXPECT_EQ(list.cbegin(), list.cend());
  EXPECT_EQ(list.rbegin(), list.rend());
  EXPECT_EQ(list.crbegin(), list.crend());
}

TEST(ElfldltlContainerTests, StaticVectorBasicApi) {
  elfldltl::StaticVector<10>::Container<int> list;
  CheckContainerAPI(list);
  CheckContainerAPI(std::cref(list).get());
}

TEST(ElfldltlContainerTests, StaticVectorCtor) {
  {
    elfldltl::StaticVector<10>::Container<int> list;
    EXPECT_EQ(list.size(), 0);
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
    auto expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    ExpectedSingleError expected("error", ": maximum 10 < requested ", 11);
    elfldltl::StaticVector<10>::Container<int> list{
        expected.diag(), "error", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10}};
  }
}

TEST(ElfldltlContainerTests, StaticVectorPushBack) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list;

    for (int i = 0; i < 10; i++) {
      ASSERT_TRUE(list.push_back(diag, "", i));
    }

    auto expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list;
    int ref = 15;
    EXPECT_TRUE(list.push_back(diag, "", ref));
    auto expected = {15};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorEmplaceBack) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list;

    for (int i = 0; i < 10; i++) {
      ASSERT_TRUE(list.emplace_back(diag, "", i));
    }

    auto expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list;
    int ref = 15;
    EXPECT_TRUE(list.emplace_back(diag, "", ref));
    auto expected = {15};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorErase) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
    auto to_erase = list.begin() + 5;
    EXPECT_EQ(list.erase(to_erase), to_erase);
    auto expected = {0, 1, 2, 3, 4, 6, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 6, 7, 8, 9}};
    auto to_erase = list.begin() + 5;
    auto to_erase_end = list.begin() + 7;
    EXPECT_EQ(*to_erase, 6);
    EXPECT_EQ(*to_erase_end, 8);
    auto expected = {0, 1, 2, 3, 4, 9};
    EXPECT_EQ(list.erase(to_erase, to_erase_end), to_erase);
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {-2, -1, 0, 1, 2, 15, 18}};

    auto to_erase_begin = list.begin() + 1;
    auto to_erase_end = list.begin() + 3;
    EXPECT_EQ(list.erase(to_erase_begin, to_erase_end), list.begin() + 1);
    auto expected = {-2, 2, 15, 18};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorEmplace) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 8, 9}};
    auto to_insert = list.begin() + 5;
    EXPECT_EQ(*to_insert, 8);
    auto it_or_err = list.emplace(diag, "", to_insert, 7);
    EXPECT_EQ(diag.errors() + diag.warnings(), 0);
    ASSERT_TRUE(it_or_err);
    EXPECT_EQ(**it_or_err, 7);
    auto expected = {0, 1, 2, 3, 4, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorInsert) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 9}};
    auto to_insert = list.end() - 1;
    auto it_or_err = list.insert(diag, "", to_insert, 8);
    EXPECT_EQ(diag.errors() + diag.warnings(), 0);
    ASSERT_TRUE(it_or_err);
    EXPECT_EQ(**it_or_err, 8);
    auto expected = {0, 1, 2, 3, 4, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 7, 8, 9}};
    auto to_insert = list.begin() + 5;
    EXPECT_EQ(*to_insert, 7);
    auto insert_range = {5, 6};
    auto it_or_err = list.insert(diag, "", to_insert, insert_range.begin(), insert_range.end());
    EXPECT_EQ(diag.errors() + diag.warnings(), 0);
    ASSERT_TRUE(it_or_err);
    EXPECT_EQ(**it_or_err, 5);
    auto expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 5, 6, 7}};

    ExpectedSingleError error("error", ": maximum 10 < requested ", 12);

    auto insert_range = {-4, -3, -2, -1};
    auto it_or_err =
        list.insert(error.diag(), "error", list.begin(), insert_range.begin(), insert_range.end());
    EXPECT_FALSE(it_or_err);
    auto expected = {0, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 5, 6, 7}};
    auto insert_range = {-2, -1};
    auto it_or_err = list.insert(diag, "", list.begin(), insert_range.begin(), insert_range.end());
    ASSERT_TRUE(it_or_err);
    EXPECT_EQ(**it_or_err, -2);
    auto expected = {-2, -1, 0, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorPopBack) {
  auto diag = ExpectOkDiagnostics();
  elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}};
  list.pop_back();
  {
    auto expected = {0, 1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  list.pop_back();
  {
    auto expected = {0, 1, 2, 3, 4, 5, 6, 7};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorResize) {
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {0, 1, 2, 3, 4}};
    ExpectedSingleError error("error", ": maximum 10 < requested ", 13);
    list.resize(error.diag(), "error", 13);
    auto expected = {0, 1, 2, 3, 4};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {-2, -1, 0, 1, 2, 3, 4}};
    EXPECT_TRUE(list.resize(diag, "", 9));
    auto expected = {-2, -1, 0, 1, 2, 3, 4, 0, 0};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
  {
    auto diag = ExpectOkDiagnostics();
    elfldltl::StaticVector<10>::Container<int> list{diag, "", {-2, -1, 0, 1, 2, 3, 4}};
    list.resize(5);
    auto expected = {-2, -1, 0, 1, 2};
    EXPECT_TRUE(std::equal(list.begin(), list.end(), expected.begin()));
  }
}

TEST(ElfldltlContainerTests, StaticVectorUnusedNoDtor) {
  struct S {
    S() { FAIL(); }
    ~S() { FAIL(); }
  };
  elfldltl::StaticVector<10>::Container<S> list;
}

TEST(ElfldltlContainerTests, StaticVectorCorrectDestruction) {
  static int count = 0;
  struct S {
    S() { count++; }
    ~S() { count--; }
  };

  auto diag = ExpectOkDiagnostics();

  ASSERT_EQ(count, 0);
  {
    elfldltl::StaticVector<10>::Container<S> list;
    for (int i = 0; i < 10; i++) {
      list.emplace_back(diag, "");
    }
    EXPECT_EQ(count, 10);
  }
  ASSERT_EQ(count, 0);
  {
    elfldltl::StaticVector<10>::Container<S> list;
    for (int i = 0; i < 5; i++) {
      list.emplace_back(diag, "");
    }
    EXPECT_EQ(count, 5);
  }
  EXPECT_EQ(count, 0);
}

TEST(ElfldltlContainerTests, StaticVectorCorrectlyMoves) {
  static int count = 0;
  struct S {
    S() { count++; }
    ~S() { count--; }
  };

  auto diag = ExpectOkDiagnostics();
  ExpectedSingleError expected("error", ": maximum 10");

  elfldltl::StaticVector<10>::Container<S> list;

  for (int i = 0; i < 10; i++) {
    list.emplace(diag, "", list.begin());
    EXPECT_EQ(count, i + 1);
  }
  EXPECT_EQ(diag.errors() + diag.warnings(), 0);

  list.emplace(expected.diag(), "error", list.begin());

  for (int i = 0; i < 10; i++) {
    list.erase(list.begin());
    EXPECT_EQ(count, 10 - i - 1);
  }
}

}  // namespace
