// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <tuple>

#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace mock_function {

// This class mocks a single function. The first template argument is the return type (or void), and
// the rest are the function arguments. ExpectCall(return_value, arg1, arg2, ...) and
// ExpectNoCall() are used by the test to set expectations, and Call(arg1, arg2, ...) is used by
// the code under test. See the following example:
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
    MockFunction& ExpectCall(R ret, Ts... args) {
        has_expectations_ = true;
        std::tuple<Ts...> args_tuple = std::make_tuple(std::forward<Ts>(args)...);
        expectations_.push_back(Expectation{std::move(ret), std::move(args_tuple)});
        return *this;
    }

    MockFunction& ExpectNoCall() {
        has_expectations_ = true;
        return *this;
    }

    R Call(Ts... args) {
        R ret = {};
        CallHelper(&ret, std::forward<Ts>(args)...);
        return ret;
    }

    bool HasExpectations() const { return has_expectations_; }

    void VerifyAndClear() {
        EXPECT_EQ(expectation_index_, expectations_.size());

        expectations_.reset();
        expectation_index_ = 0;
    }

private:
    struct Expectation {
        R ret_value;
        std::tuple<Ts...> args;
    };

    void CallHelper(R* ret, Ts... args) {
        ASSERT_LT(expectation_index_, expectations_.size());

        Expectation exp = std::move(expectations_[expectation_index_++]);
        EXPECT_TRUE(exp.args == std::make_tuple(std::forward<Ts>(args)...));
        *ret = std::move(exp.ret_value);
    }

    bool has_expectations_ = false;
    fbl::Vector<Expectation> expectations_;
    size_t expectation_index_ = 0;
};

template <typename... Ts>
class MockFunction<void, Ts...> {
public:
    MockFunction& ExpectCall(Ts... args) {
        has_expectations_ = true;
        std::tuple<Ts...> args_tuple = std::make_tuple(std::forward<Ts>(args)...);
        expectations_.push_back(std::move(args_tuple));
        return *this;
    }

    MockFunction& ExpectNoCall() {
        has_expectations_ = true;
        return *this;
    }

    void Call(Ts... args) { CallHelper(std::forward<Ts>(args)...); }

    bool HasExpectations() const { return has_expectations_; }

    void VerifyAndClear() {
        ASSERT_EQ(expectation_index_, expectations_.size());

        expectations_.reset();
        expectation_index_ = 0;
    }

private:
    void CallHelper(Ts... args) {
        ASSERT_LT(expectation_index_, expectations_.size());

        std::tuple<Ts...> exp = std::move(expectations_[expectation_index_++]);
        EXPECT_TRUE(exp == std::make_tuple(std::forward<Ts>(args)...));
    }

    bool has_expectations_ = false;
    fbl::Vector<std::tuple<Ts...>> expectations_;
    size_t expectation_index_ = 0;
};

}  // namespace mock_function
