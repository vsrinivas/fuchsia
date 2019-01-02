// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/string.h>
#include <zxtest/base/types.h>

namespace zxtest {

// Helper class for handling the error information, plus some logic for printing the correct error
// messages.
class Assertion {
public:
    Assertion() = delete;
    Assertion(const fbl::String& expected, const fbl::String& expected_eval,
              const fbl::String& actual, const fbl::String& actual_eval,
              const SourceLocation& location, bool is_fatal);
    Assertion(const Assertion&) = delete;
    Assertion(Assertion&&);
    ~Assertion();

    Assertion& operator=(const Assertion&) = delete;
    Assertion& operator=(Assertion&&) = delete;

    // Returns the position at which the assertion happened.
    const SourceLocation& location() const { return location_; }

    // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
    // the text representation of expected, as it was captured on compile time.
    const fbl::String& expected() const { return expected_; }

    // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
    // the text representation of actual, as it was captured on compile time.
    const fbl::String& actual() const { return actual_; }

    // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
    // the text representation of expected as it is evaluated at runtime..
    const fbl::String& expected_eval() const { return expected_eval_; };

    // Returns the expected value of an equality. For example in ASSERT_EQ(actual, expected) returns
    // the text representation of actual, as it was captured on runtime.
    const fbl::String& actual_eval() const { return actual_eval_; }

    // Returns true if this assertion is fatal, and test should stop execution. Essentially if the
    // asserting macro is ASSERT_* or EXPECT_*.
    bool is_fatal() const { return is_fatal_; }

private:
    fbl::String expected_;
    fbl::String expected_eval_;
    fbl::String actual_;
    fbl::String actual_eval_;

    SourceLocation location_;

    bool is_fatal_;
};

} // namespace zxtest
