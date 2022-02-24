// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_TEST_H_
#define ZXTEST_BASE_TEST_H_

#include <memory>
#include <utility>

#include <zxtest/base/test-driver.h>
#include <zxtest/base/test-internal.h>

namespace zxtest {

namespace internal {
// Access `SetUpTestSuite` and `TearDownTestSuite` regardless of scope.
//
// Gtest allows `SetUpTestSuite` and `TearDownTestSuite` to be protected functions.
// The architecture of zxtest prevents it from running these functions if they're protected. So
// to ease porting tests to zxtest, we are wrapping the functions into a getter.
template <typename T>
struct Accessor : T {
  static constexpr auto SetUpTestSuite() { return &T::SetUpTestSuite; }
  static constexpr auto TearDownTestSuite() { return &T::TearDownTestSuite; }
};
}  // namespace internal

// Instance of a test to be executed.
class Test : private internal::TestInternal {
 public:
  // Default factory function for tests.
  template <typename Derived>
  static std::unique_ptr<Derived> Create(internal::TestDriver* driver) {
    static_assert(std::is_base_of<Test, Derived>::value, "Must inherit from zxtest::TestInternal.");
    std::unique_ptr<Derived> derived = std::make_unique<Derived>();
    derived->driver_ = driver;
    return derived;
  }

  virtual ~Test() = default;

  // Blocking use of SetUpTestCase. Use SetUpTestSuite instead.
  virtual void SetUpTestCase() final {}

  // Blocking use of TearDownTestCase. Use TearDownTestSuite instead.
  virtual void TearDownTestCase() final {}

  // Dummy implementation for TestCase SetUp functions.
  static void SetUpTestSuite() {}

  // Dummy implementation for TestCase TearDown functions.
  static void TearDownTestSuite() {}

  // Dummy SetUp method.
  virtual void SetUp() {}

  // Dummy TearDown method.
  virtual void TearDown() {}

  // Executed the current test instance.
  virtual void Run();

  // Check if this test has been skipped.
  virtual bool IsSkipped();

 private:
  // Actual test implementation.
  virtual void TestBody() = 0;
};

}  // namespace zxtest

#endif  // ZXTEST_BASE_TEST_H_
