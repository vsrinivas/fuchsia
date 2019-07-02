// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-registry.h"

#include <cstdlib>

#include <fbl/function.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/event-broadcaster.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-case.h>
#include <zxtest/base/test-info.h>
#include <zxtest/base/types.h>

#define ITERATION_EVENT_OBSERVER(Event)                                                            \
  class FakeObserver : public LifecycleObserver {                                                  \
   public:                                                                                         \
    void OnIteration##Event(const Runner& runner, int iter) final {                                \
      on_notify(runner, iter);                                                                     \
      called = true;                                                                               \
    }                                                                                              \
                                                                                                   \
    fbl::Function<void(const Runner& runner, int iter)> on_notify;                                 \
    bool called = false;                                                                           \
  }

#define ASSERTION_OBSERVER                                                                         \
  class FakeObserver : public LifecycleObserver {                                                  \
   public:                                                                                         \
    void OnAssertion(const Assertion& assertion) final {                                           \
      on_notify(assertion);                                                                        \
      called = true;                                                                               \
    }                                                                                              \
                                                                                                   \
    fbl::Function<void(const Assertion& assertion)> on_notify;                                     \
    bool called = false;                                                                           \
  }

#define RUNNER_EVENT_OBSERVER(Event)                                                               \
  class FakeObserver : public LifecycleObserver {                                                  \
   public:                                                                                         \
    void On##Event(const Runner& runner) final {                                                   \
      on_notify(runner);                                                                           \
      called = true;                                                                               \
    }                                                                                              \
                                                                                                   \
    fbl::Function<void(const Runner& runner)> on_notify;                                           \
    bool called = false;                                                                           \
  }

// Defines a FakeObserver class which tracks call to OnTestCaseEvent methods.
#define TESTCASE_EVENT_OBSERVER(Event)                                                             \
  class FakeObserver : public LifecycleObserver {                                                  \
   public:                                                                                         \
    void OnTestCase##Event(const TestCase& test_case) final {                                      \
      on_notify(test_case);                                                                        \
      called = true;                                                                               \
    }                                                                                              \
                                                                                                   \
    fbl::Function<void(const TestCase& test_case)> on_notify;                                      \
    bool called = false;                                                                           \
  }

// Defines a FakeObserver class which tracks call to OnTestEvent methods.
#define TEST_EVENT_OBSERVER(Event)                                                                 \
  class FakeObserver : public LifecycleObserver {                                                  \
   public:                                                                                         \
    void OnTest##Event(const TestCase& test_case, const TestInfo& info) final {                    \
      on_notify(test_case, info);                                                                  \
      called = true;                                                                               \
    }                                                                                              \
                                                                                                   \
    fbl::Function<void(const TestCase& test_case, const TestInfo& info)> on_notify;                \
    bool called = false;                                                                           \
  }

// Fills ObserverList with |kNumObservers| instances of |FakeObserver|, which should be defined
// for the scope. And sets on_notify of each observer to on_notify_def and registers them
// with |event_broadcaster|.
#define REGISTER_OBSERVERS(observer_list, event_broadcaster, on_notify_def)                        \
  for (int i = 0; i < kNumObservers; ++i) {                                                        \
    observer_list.push_back({});                                                                   \
    auto& observer = observer_list[observer_list.size() - 1];                                      \
    observer.on_notify = on_notify_def;                                                            \
    event_broadcaster.Subscribe(&observer);                                                        \
  }

namespace zxtest {
namespace test {
namespace {

constexpr char kTestCaseName[] = "TestCase";
constexpr char kTestName[] = "Test";

constexpr int kNumObservers = 100;

const SourceLocation kLocation = {.filename = "filename", .line_number = 20};

void Stub() {
}

template <typename T>
void ValidateAllObserversNotified(const T& observers) {
  for (auto& observer : observers) {
    ZX_ASSERT_MSG(observer.called, "EventBroadcaster failed to propagate event.\n");
  }
}

Reporter MakeSilentReporter() {
  return Reporter(std::make_unique<FileLogSink>(nullptr));
}

}  // namespace

void EventBroadcasterOnProgramStart() {
  RUNNER_EVENT_OBSERVER(ProgramStart);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(observers, event_broadcaster, [&runner](const Runner& actual_runner) {
    ZX_ASSERT_MSG(&actual_runner == &runner,
                  "EventBroadcaster::OnProgramStart propagated the wrong runner.\n");
  });

  event_broadcaster.OnProgramStart(runner);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnIterationStart() {
  ITERATION_EVENT_OBSERVER(Start);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&runner](const Runner& actual_runner, int iteration) {
        ZX_ASSERT_MSG(&actual_runner == &runner,
                      "EventBroadcaster::OnIterationStart propagated the wrong runner.\n");
        ZX_ASSERT_MSG(iteration == 4,
                      "EventBroadcaster::OnIterationStart propagated the wron iteration number.\n");
      });

  event_broadcaster.OnIterationStart(runner, 4);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnEnvironmentSetUp() {
  RUNNER_EVENT_OBSERVER(EnvironmentSetUp);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(observers, event_broadcaster, [&runner](const Runner& actual_runner) {
    ZX_ASSERT_MSG(&actual_runner == &runner,
                  "EventBroadcaster::OnEnvironmentSetUp propagated the wrong runner.\n");
  });

  event_broadcaster.OnEnvironmentSetUp(runner);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestCaseStart() {
  TESTCASE_EVENT_OBSERVER(Start);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(observers, event_broadcaster, [&test_case](const TestCase& actual) {
    ZX_ASSERT_MSG(&actual == &test_case,
                  "EventBroadcaster::OnTestCaseStart propagated the wrong test case\n");
  });

  event_broadcaster.OnTestCaseStart(test_case);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestStart() {
  TEST_EVENT_OBSERVER(Start);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  TestInfo test_info(kTestName, kLocation, nullptr);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&](const TestCase& actual, const TestInfo& actual_info) {
        ZX_ASSERT_MSG(&actual == &test_case,
                      "EventBroadcaster::OnTestStart propagated the wrong test case\n");
        ZX_ASSERT_MSG(&actual_info == &test_info,
                      "EventBroadcaster::OnTestStart propagated the wrong test info\n");
      });

  event_broadcaster.OnTestStart(test_case, test_info);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnAssertion() {
  ASSERTION_OBSERVER;

  internal::EventBroadcaster event_broadcaster;
  Assertion assertion("Value should be equal", "kExpectedValue", "5", "actual_value", "10",
                      {.filename = "test.cpp", .line_number = 99999}, /*is_fatal*/ false);
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(observers, event_broadcaster, [&](const Assertion& actual) {
    ZX_ASSERT_MSG(&actual == &assertion,
                  "EventBroadcaster::OnAssertion propagated wrong assertion.\n");
  });

  event_broadcaster.OnAssertion(assertion);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestSkip() {
  TEST_EVENT_OBSERVER(Skip);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  TestInfo test_info(kTestName, kLocation, nullptr);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&](const TestCase& actual, const TestInfo& actual_info) {
        ZX_ASSERT_MSG(&actual == &test_case,
                      "EventBroadcaster::OnTestSkip propagated the wrong test case\n");
        ZX_ASSERT_MSG(&actual_info == &test_info,
                      "EventBroadcaster::OnTestSkip propagated the wrong test info\n");
      });

  event_broadcaster.OnTestSkip(test_case, test_info);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestSuccess() {
  TEST_EVENT_OBSERVER(Success);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  TestInfo test_info(kTestName, kLocation, nullptr);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&](const TestCase& actual, const TestInfo& actual_info) {
        ZX_ASSERT_MSG(&actual == &test_case,
                      "EventBroadcaster::OnTestSuccess propagated the wrong test case\n");
        ZX_ASSERT_MSG(&actual_info == &test_info,
                      "EventBroadcaster::OnTestSuccess propagated the wrong test info\n");
      });

  event_broadcaster.OnTestSuccess(test_case, test_info);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestFailure() {
  TEST_EVENT_OBSERVER(Failure);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  TestInfo test_info(kTestName, kLocation, nullptr);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&](const TestCase& actual, const TestInfo& actual_info) {
        ZX_ASSERT_MSG(&actual == &test_case,
                      "EventBroadcaster::OnTestFailure propagated the wrong test case\n");
        ZX_ASSERT_MSG(&actual_info == &test_info,
                      "EventBroadcaster::OnTestFailure propagated the wrong test info\n");
      });

  event_broadcaster.OnTestFailure(test_case, test_info);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnTestCaseEnd() {
  TESTCASE_EVENT_OBSERVER(End);

  TestCase test_case(kTestCaseName, &Stub, &Stub);
  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);

  REGISTER_OBSERVERS(observers, event_broadcaster, [&test_case](const TestCase& actual) {
    ZX_ASSERT_MSG(&actual == &test_case,
                  "EventBroadcaster::OnTestCaseEnd propagated the wrong test case\n");
  });

  event_broadcaster.OnTestCaseEnd(test_case);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnEnvironmentTearDown() {
  RUNNER_EVENT_OBSERVER(EnvironmentTearDown);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(observers, event_broadcaster, [&runner](const Runner& actual_runner) {
    ZX_ASSERT_MSG(&actual_runner == &runner,
                  "EventBroadcaster::OnIterationStart propagated the wrong runner.\n");
  });

  event_broadcaster.OnEnvironmentTearDown(runner);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnIterationEnd() {
  ITERATION_EVENT_OBSERVER(End);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(
      observers, event_broadcaster, [&runner](const Runner& actual_runner, int iteration) {
        ZX_ASSERT_MSG(&actual_runner == &runner,
                      "EventBroadcaster::OnEnvironmentTearDown propagated the wrong runner.\n");
        ZX_ASSERT_MSG(
            iteration == 4,
            "EventBroadcaster::OnEnvironmentTearDown propagated the wron iteration number.\n");
      });

  event_broadcaster.OnIterationEnd(runner, 4);

  ValidateAllObserversNotified(observers);
}

void EventBroadcasterOnProgramEnd() {
  RUNNER_EVENT_OBSERVER(ProgramEnd);

  internal::EventBroadcaster event_broadcaster;
  fbl::Vector<FakeObserver> observers;
  observers.reserve(kNumObservers);
  Runner runner(MakeSilentReporter());

  REGISTER_OBSERVERS(observers, event_broadcaster, [&runner](const Runner& actual_runner) {
    ZX_ASSERT_MSG(&actual_runner == &runner,
                  "EventBroadcaster::OnProgramEnd propagated the wrong runner.\n");
  });

  event_broadcaster.OnProgramEnd(runner);

  ValidateAllObserversNotified(observers);
}

}  // namespace test
}  // namespace zxtest
