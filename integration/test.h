// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <chrono>

#include "apps/maxwell/debug.h"
#include "mojo/public/cpp/application/application_test_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"

typedef std::function<void(mojo::Shell*)> TestRoutine;

struct Test {
  Test(const std::string& name, TestRoutine run) : name(name), run(run) {}
  const std::string name;
  const TestRoutine run;
};

void Yield();

// Processes Mojo messages until the given predicate is true.
template <typename Predicate>
void WaitUntil(Predicate until) {
  do {
    Yield();
  } while (!until());
}

using Predicate = std::function<bool()>;

Predicate operator&&(const Predicate& a, const Predicate& b);
Predicate operator||(const Predicate& a, const Predicate& b);
Predicate operator!(const Predicate& a);

// Converts a closure to a tautology. This is useful in combination with the
// Boolean operators above to add side effects to predicates, for example
// fataling on a deadline.
template <typename Closure>
Predicate SideEffect(Closure side_effect) {
  return [&side_effect] {
    side_effect();
    return true;
  };
}

#define PREDICATE(condition) [&] { return condition; }
// Convenience macro that wraps "condition" in a Predicate.
#define WAIT_UNTIL(condition) WaitUntil(PREDICATE(condition))

using namespace std::chrono_literals;

template <class Rep, class Period>
Predicate Deadline(const std::chrono::duration<Rep, Period>& duration) {
  using std::chrono::steady_clock;
  const auto deadline = steady_clock::now() + duration;
  return [deadline] { return steady_clock::now() >= deadline; };
}

// Sleeps for a time while processing Mojo messages.
template <class Rep, class Period>
void Sleep(const std::chrono::duration<Rep, Period>& duration) {
  WaitUntil(Deadline(duration));
}

// Sleep for a default reasonable time for Mojo apps to start up.
void Sleep();

// 2s Mojo timeout for asyncs on signals (e.g. WaitForIncomingMethodCall).
constexpr MojoDeadline kMojoDeadline = 2 * 1000 * 1000;

// In practice, 100 ms is actually a bit short, so this may occasionally falsely
// succeed tests that should fail. Flakiness should thus be considered failure.
constexpr auto kAsyncCheckSteady = 100ms;
constexpr auto kAsyncCheckMax = 5s;

// Does a weak stability check on an async condition by waiting until the given
// condition is true (max 2s) and then ensuring that the condition remains true
// (for 100 ms).
//
// If the condition becomes true briefly but not over a 100 ms polling period,
// this check continues waiting until the deadline. Since the transient check
// is polling-based, the exact number of matches should not be relied upon.
#define ASYNC_CHECK(condition)                                             \
  {                                                                        \
    auto deadline = Deadline(kAsyncCheckMax);                              \
    auto check = PREDICATE(condition);                                     \
    do {                                                                   \
      WaitUntil(check ||                                                   \
                deadline && SideEffect([] {                                \
                  MOJO_LOG(FATAL)                                          \
                      << "Deadline exceeded for async check: " #condition; \
                }));                                                       \
      auto steady = Deadline(kAsyncCheckSteady);                           \
      WaitUntil(steady || !check);                                         \
    } while (!(condition));                                                \
  }

class DebuggableAppTestBase : public mojo::test::ApplicationTestBase {
 protected:
  void StartComponent(const std::string& url) {
    maxwell::DebugPtr debug;
    mojo::ConnectToService(shell(), url, GetProxy(&debug));
    dependencies_.AddInterfacePtr(std::move(debug));
  }

  template <typename Interface>
  void ConnectToService(const std::string& url,
                        mojo::InterfaceRequest<Interface> request) {
    dependencies_.AddInterfacePtr(
        maxwell::ConnectToDebuggableService(shell(), url, std::move(request)));
  }

  virtual void KillAllDependencies() {
    dependencies_.ForAllPtrs([](maxwell::Debug* debug) { debug->Kill(); });
    WAIT_UNTIL(dependencies_.size() == 0);
  }

  void TearDown() override {
    KillAllDependencies();
    ApplicationTestBase::TearDown();
  }

 private:
  mojo::InterfacePtrSet<maxwell::Debug> dependencies_;
};
