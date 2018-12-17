// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace zxtest {
// Forward declaration.
class TestCase;
class TestInfo;

// Allows user to listen for lifecycle events. This allows injecting code at specific
// instants, for example when there is a global set up and tear down for a library,
// that is done at process start up.
// This interface mimicks gTest EventObserver, all methods are stubbed with empty body,
// so implementing classes, only override those they are interested in.
//
// Note: This interface will be expanded incrementally in a series of patches,
// so it becomes easier to review.
class LifecycleObserver {
public:
    virtual ~LifecycleObserver() = default;

    // Reports before every TestCase is set up.
    virtual void OnTestCaseStart(const TestCase& test_case) {}

    // Reports before every test starts.
    virtual void OnTestStart(const TestCase& test_case, const TestInfo& test) {}

    // Reports before every test starts.
    virtual void OnTestSkip(const TestCase& test_case, const TestInfo& test) {}

    // Reports before every TestCase is set up.
    virtual void OnTestFailure(const TestCase& test_case, const TestInfo& test) {}

    // Reports before every TestCase is set up.
    virtual void OnTestSuccess(const TestCase& test_case, const TestInfo& test) {}

    // Reports before every TestCase is torn down.
    virtual void OnTestCaseEnd(const TestCase& test_case) {}
};

} // namespace zxtest
