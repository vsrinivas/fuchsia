// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>

#include <zxtest/base/test-driver.h>
#include <zxtest/base/test-internal.h>

namespace zxtest {

// Instance of a test to be executed.
class Test : private internal::TestInternal {
public:
    // Default factory function for tests.
    template <typename Derived>
    static std::unique_ptr<Derived> Create(internal::TestDriver* driver) {
        static_assert(std::is_base_of<Test, Derived>::value,
                      "Must inherit from zxtest::TestInternal.");
        std::unique_ptr<Derived> derived = std::make_unique<Derived>();
        derived->driver_ = driver;
        return std::move(derived);
    }

    virtual ~Test() = default;

    // Dummy implementation for TestCase SetUp functions.
    static void SetUpTestCase() {}

    // Dummy implementation for TestCase TearDown functions.
    static void TearDownTestCase() {}

    // Dummy SetUp method.
    virtual void SetUp() {}

    // Dummy TearDown method.
    virtual void TearDown() {}

    // Executed the current test instance.
    virtual void Run();

private:
    // Actual test implementation.
    virtual void TestBody() = 0;
};

} // namespace zxtest
