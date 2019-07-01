// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_EVENT_BROADCASTER_H_
#define ZXTEST_BASE_EVENT_BROADCASTER_H_

#include <fbl/vector.h>
#include <zxtest/base/observer.h>

namespace zxtest {
// Forward declaration.
class Assertion;
class TestCase;
class TestInfo;
class Runner;

namespace internal {

class EventBroadcaster : public LifecycleObserver {
 public:
  EventBroadcaster();
  EventBroadcaster(const EventBroadcaster&) = delete;
  EventBroadcaster(EventBroadcaster&&);
  ~EventBroadcaster() final;

  EventBroadcaster& operator=(const EventBroadcaster&) = delete;
  EventBroadcaster& operator=(EventBroadcaster&&) = delete;

  // Reports before any test is executed.
  void OnProgramStart(const Runner& runner) final;

  // Reports before every iteration starts.
  void OnIterationStart(const Runner& runner, int iteration) final;

  // Reports before any environment is set up.
  void OnEnvironmentSetUp(const Runner& runner) final;

  // Reports before every TestCase is set up.
  void OnTestCaseStart(const TestCase& test_case) final;

  // Reports before every test starts.
  void OnTestStart(const TestCase& test_case, const TestInfo& test) final;

  // Reports when an assertion on the running tests fails.
  virtual void OnAssertion(const Assertion& assertion) final;

  // Reports before every test is skipped.
  void OnTestSkip(const TestCase& test_case, const TestInfo& test) final;

  // Reports on Test failure.
  void OnTestFailure(const TestCase& test_case, const TestInfo& test) final;

  // Reports on Test passed.
  void OnTestSuccess(const TestCase& test_case, const TestInfo& test) final;

  // Reports before every TestCase is torn down.
  void OnTestCaseEnd(const TestCase& test_case) final;

  // Adds a lifecycle observer to the registered list of observers.
  void Subscribe(LifecycleObserver* observer);

  // Reports before any environment is torn down.
  void OnEnvironmentTearDown(const Runner& runner) final;

  // Reports before every iteration starts.
  void OnIterationEnd(const Runner& runner, int iteration) final;

  // Reports after all test executed.
  void OnProgramEnd(const Runner& runner) final;

 private:
  fbl::Vector<LifecycleObserver*> lifecycle_observers_;
};

}  // namespace internal
}  // namespace zxtest

#endif  // ZXTEST_BASE_EVENT_BROADCASTER_H_
