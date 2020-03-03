// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stddef.h>

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/inline_array.h>
#include <zxtest/zxtest.h>

namespace {

struct TestType {
  TestType() { ctor_run_count++; }

  ~TestType() { dtor_run_count++; }

  static void ResetRunCounts() {
    ctor_run_count = 0u;
    dtor_run_count = 0u;
  }

  static size_t ctor_run_count;
  static size_t dtor_run_count;
};

size_t TestType::ctor_run_count = 0u;
size_t TestType::dtor_run_count = 0u;

TEST(InlineArrayTest, inline_test) {
  for (size_t sz = 0u; sz <= 3u; sz++) {
    TestType::ResetRunCounts();
    {
      fbl::AllocChecker ac;
      fbl::InlineArray<TestType, 3u> ia(&ac, sz);
      EXPECT_TRUE(ac.check());
    }
    EXPECT_EQ(sz, TestType::ctor_run_count);
    EXPECT_EQ(sz, TestType::dtor_run_count);
  }
}

TEST(InlineArrayTest, non_inline_test) {
  static const size_t test_sizes[] = {4u, 5u, 6u, 10u, 100u, 1000u};

  for (size_t i = 0u; i < fbl::count_of(test_sizes); i++) {
    size_t sz = test_sizes[i];

    TestType::ResetRunCounts();
    {
      fbl::AllocChecker ac;
      fbl::InlineArray<TestType, 3u> ia(&ac, sz);
      EXPECT_TRUE(ac.check());
    }
    EXPECT_EQ(sz, TestType::ctor_run_count);
    EXPECT_EQ(sz, TestType::dtor_run_count);
  }
}

}  // namespace
