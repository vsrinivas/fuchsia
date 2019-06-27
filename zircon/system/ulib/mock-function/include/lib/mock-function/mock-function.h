// Copyright 2019 The Fuchsia Authors. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#pragma once

#include <zxtest/zxtest.h>

#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace mock_function {

// This class mocks a single function.  The Expect*() functions are used by the test to set
// expectations, and Call() is used by the code under test.  There are three variants:
//
// * ExpectCall(return_value, arg1, arg2, ...) sets the expecation that the call will be made with
//   arguments `arg1`, `arg2`, etc., each compared using operator==. `return_value` will be returned
//   unconditionally, or is omitted if the function returns void.
// * ExpectCallWithMatcher(matcher, return_value, arg1, arg2) uses a `matcher` functor to compare
//   the arguments. The matcher takes two tuple parameters, holding the expected and actual
//   arguments.
// * ExpectNoCall() expects that the function will not be called.
//
// Example:
//
// class SomeClassTest : SomeClass {
// public:
//     zx_status_t CallsSomeMethod();
//
//     mock_function::MockFunction<zx_status_t, uint32_t, uint32_t>& mock_SomeMethod() {
//         return mock_some_method_;
//     }
//
// private:
//     zx_status_t SomeMethod(uint32_t a, uint32_t b) override {
//         return mock_some_method_.Call(a, b);
//     }
//
//     mock_function::MockFunction<zx_status_t, uint32_t, uint32_t> mock_some_method_;
// };
//
// TEST(SomeDriver, SomeTest) {
//     SomeClassTest test;
//     test.mock_SomeMethod().ExpectCall(ZX_OK, 100, 30);
//
//     EXPECT_OK(test.CallsSomeMethod());
//
//     test.mock_SomeMethod().VerifyAndClear();
// }

template <typename R, typename... Ts>
class MockFunction {
 public:
  template <typename... As>
  MockFunction& ExpectCall(R ret, As&&... args) {
    static_assert(sizeof...(As) == sizeof...(Ts), "wrong number of arguments to ExpectCall");
    has_expectations_ = true;
    expectations_.emplace_back(
        std::make_unique<Expectation>(std::move(ret), std::forward<As>(args)...));
    return *this;
  }

  template <typename M, typename... As>
  MockFunction& ExpectCallWithMatcher(M matcher, R ret, As&&... args) {
    static_assert(sizeof...(As) == sizeof...(Ts),
                  "wrong number of arguments to ExpectCallWithMatcher");
    has_expectations_ = true;
    expectations_.emplace_back(std::make_unique<ExpectationWithMatcher<M>>(
        std::move(matcher), std::move(ret), std::forward<As>(args)...));
    return *this;
  }

  MockFunction& ExpectNoCall() {
    has_expectations_ = true;
    return *this;
  }

  template <typename... As>
  R Call(As&&... args) {
    std::unique_ptr<Expectation> exp;
    CallHelper(&exp);
    EXPECT_TRUE(exp->MatchArgs({std::forward<As>(args)...}));
    return std::move(exp->retval);
  }

  bool HasExpectations() const { return has_expectations_; }

  void VerifyAndClear() {
    EXPECT_EQ(expectation_index_, expectations_.size());
    expectations_.clear();
    expectation_index_ = 0;
  }

 private:
  struct Expectation {
    template <typename... As>
    Expectation(R ret, As&&... args)
        : retval(std::move(ret)), expected_args(std::forward<As>(args)...) {}
    virtual ~Expectation() = default;

    virtual bool MatchArgs(std::tuple<Ts...> actual_args) { return actual_args == expected_args; }

    R retval;
    std::tuple<Ts...> expected_args;
  };

  template <typename Matcher>
  struct ExpectationWithMatcher : public Expectation {
    template <typename M, typename... As>
    ExpectationWithMatcher(M&& mat, R ret, As&&... args)
        : Expectation(std::move(ret), std::forward<As>(args)...), matcher(std::forward<M>(mat)) {}

    bool MatchArgs(std::tuple<Ts...> actual_args) override {
      return matcher(actual_args, this->expected_args);
    }

    Matcher matcher;
  };

  void CallHelper(std::unique_ptr<Expectation>* exp) {
    ASSERT_LT(expectation_index_, expectations_.size());
    *exp = std::move(expectations_[expectation_index_++]);
  }

  bool has_expectations_ = false;
  std::vector<std::unique_ptr<Expectation>> expectations_;
  size_t expectation_index_ = 0;
};

template <typename... Ts>
class MockFunction<void, Ts...> {
 public:
  template <typename... As>
  MockFunction& ExpectCall(As&&... args) {
    static_assert(sizeof...(As) == sizeof...(Ts), "wrong number of arguments to ExpectCall");
    has_expectations_ = true;
    expectations_.emplace_back(std::make_unique<Expectation>(std::forward<As>(args)...));
    return *this;
  }

  template <typename M, typename... As>
  MockFunction& ExpectCallWithMatcher(M matcher, As&&... args) {
    static_assert(sizeof...(As) == sizeof...(Ts),
                  "wrong number of arguments to ExpectCallWithMatcher");
    has_expectations_ = true;
    expectations_.emplace_back(
        std::make_unique<ExpectationWithMatcher<M>>(std::move(matcher), std::forward<As>(args)...));
    return *this;
  }

  MockFunction& ExpectNoCall() {
    has_expectations_ = true;
    return *this;
  }

  template <typename... As>
  void Call(As&&... args) {
    std::unique_ptr<Expectation> exp;
    CallHelper(&exp);
    EXPECT_TRUE(exp->MatchArgs({std::forward<As>(args)...}));
  }

  bool HasExpectations() const { return has_expectations_; }

  void VerifyAndClear() {
    ASSERT_EQ(expectation_index_, expectations_.size());
    expectations_.clear();
    expectation_index_ = 0;
  }

 private:
  struct Expectation {
    template <typename... As>
    Expectation(As&&... args) : expected_args(std::forward<As>(args)...) {}
    virtual ~Expectation() = default;

    virtual bool MatchArgs(std::tuple<Ts...> actual_args) { return actual_args == expected_args; }

    std::tuple<Ts...> expected_args;
  };

  template <typename Matcher>
  struct ExpectationWithMatcher : public Expectation {
    template <typename M, typename... As>
    ExpectationWithMatcher(M&& mat, As&&... args)
        : Expectation(std::forward<As>(args)...), matcher(std::forward<M>(mat)) {}

    bool MatchArgs(std::tuple<Ts...> actual_args) override {
      return matcher(actual_args, this->expected_args);
    }

    Matcher matcher;
  };

  void CallHelper(std::unique_ptr<Expectation>* exp) {
    ASSERT_LT(expectation_index_, expectations_.size());
    *exp = std::move(expectations_[expectation_index_++]);
  }

  bool has_expectations_ = false;
  std::vector<std::unique_ptr<Expectation>> expectations_;
  size_t expectation_index_ = 0;
};

}  // namespace mock_function
