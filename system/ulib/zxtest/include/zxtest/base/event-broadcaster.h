// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/vector.h>
#include <zxtest/base/observer.h>

namespace zxtest {
// Forward declaration.
class TestCase;
class TestInfo;

namespace internal {

class EventBroadcaster : public LifecycleObserver {
public:
    EventBroadcaster();
    EventBroadcaster(const EventBroadcaster&) = delete;
    EventBroadcaster(EventBroadcaster&&);
    ~EventBroadcaster() final;

    EventBroadcaster& operator=(const EventBroadcaster&) = delete;
    EventBroadcaster& operator=(EventBroadcaster&&) = delete;

    // Reports before every TestCase is set up.
    void OnTestCaseStart(const TestCase& test_case) final;

    // Reports before every test starts.
    void OnTestStart(const TestCase& test_case, const TestInfo& test) final;

    // Reports before every test starts.
    void OnTestSkip(const TestCase& test_case, const TestInfo& test) final;

    // Reports before every TestCase is set up.
    void OnTestFailure(const TestCase& test_case, const TestInfo& test) final;

    // Reports before every TestCase is set up.
    void OnTestSuccess(const TestCase& test_case, const TestInfo& test) final;

    // Reports before every TestCase is torn down.
    void OnTestCaseEnd(const TestCase& test_case) final;

    // Adds a lifecycle observer to the registered list of observers.
    void Subscribe(LifecycleObserver* observer);

private:
    fbl::Vector<LifecycleObserver*> lifecycle_observers_;
};

} // namespace internal
} // namespace zxtest
