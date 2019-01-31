// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace zxtest {
namespace internal {

// Teest Status.
enum class TestStatus : std::uint8_t {
    kRunning,
    kPassed,
    kFailed,
    kSkipped,
};

// Interface for driving the test, and propagating failures.
class TestDriver {
public:
    virtual ~TestDriver() = default;

    // Called when a test is skipped..
    virtual void Skip() = 0;

    // Return true if the is allowed to continue execution.
    virtual bool Continue() const = 0;

    // Returns the current status of the test.
    virtual TestStatus Status() const = 0;

protected:
    TestDriver() = default;
};

} // namespace internal
} // namespace zxtest
