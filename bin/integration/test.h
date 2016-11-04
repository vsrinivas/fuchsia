// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "gtest/gtest.h"
#include "apps/maxwell/src/application_environment_host_impl.h"
#include "apps/maxwell/src/launcher/agent_launcher.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

void Yield();

// Processes messages until the given predicate is true.
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
  return [side_effect] {
    side_effect();
    return true;
  };
}

#define PREDICATE(condition) [&] { return condition; }
// Convenience macro that wraps "condition" in a Predicate.
#define WAIT_UNTIL(condition) WaitUntil(PREDICATE(condition))

Predicate Deadline(const ftl::TimeDelta& duration);

// Sleeps for a time while processing messages.
void Sleep(const ftl::TimeDelta& duration);

// Sleep for a default reasonable time for apps to start up.
void Sleep();

// 2s timeout for asyncs on signals (e.g. WaitForIncomingMethodCall).
constexpr auto kSignalDeadline = ftl::TimeDelta::FromSeconds(2);

// In practice, 100 ms is actually a bit short, so this may occasionally falsely
// succeed tests that should fail. Flakiness should thus be considered failure.
constexpr auto kAsyncCheckSteady = ftl::TimeDelta::FromMilliseconds(100);
constexpr auto kAsyncCheckMax = ftl::TimeDelta::FromSeconds(5);

// Does a weak stability check on an async condition by waiting until the given
// condition is true (max 2s) and then ensuring that the condition remains true
// (for 100 ms).
//
// If the condition becomes true briefly but not over a 100 ms polling period,
// this check continues waiting until the deadline. Since the transient check
// is polling-based, the exact number of matches should not be relied upon.
//
// This is a macro rather than a function to preserve the file and line number
// of the failed assertion.
#define ASYNC_CHECK_DIAG(condition, diagnostic)                         \
  {                                                                     \
    auto deadline = Deadline(kAsyncCheckMax);                           \
    auto check = PREDICATE(condition);                                  \
    do {                                                                \
      WaitUntil(check || deadline && SideEffect([&] {                   \
                           FTL_LOG(FATAL)                               \
                               << "Deadline exceeded for async check: " \
                               << diagnostic;                           \
                         }));                                           \
      auto steady = Deadline(kAsyncCheckSteady);                        \
      WaitUntil(steady || !check);                                      \
    } while (!(condition));                                             \
  }

#define ASYNC_CHECK(condition) ASYNC_CHECK_DIAG(condition, #condition)
#define ASYNC_EQ(expected, actual) \
  ASYNC_CHECK_DIAG(                \
      (expected) == (actual),      \
      #actual " == " #expected "; last known value: " << (actual))

extern modular::ApplicationEnvironment* root_environment;

class MaxwellTestBase : public ::testing::Test {
 protected:
  MaxwellTestBase();
  virtual ~MaxwellTestBase() = default;

  void StartAgent(
      const std::string& url,
      std::unique_ptr<modular::ApplicationEnvironmentHost> env_host) {
    agent_launcher_->StartAgent(url, std::move(env_host));
  }

  modular::ServiceProviderPtr StartServiceProvider(const std::string& url);

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(const std::string& url) {
    auto services = StartServiceProvider(url);
    return modular::ConnectToService<Interface>(services.get());
  }

 private:
  maxwell::ApplicationEnvironmentHostImpl test_environment_host_;
  fidl::Binding<modular::ApplicationEnvironmentHost>
      test_environment_host_binding_;
  modular::ApplicationEnvironmentPtr test_environment_;
  modular::ApplicationLauncherPtr test_launcher_;
  std::unique_ptr<maxwell::AgentLauncher> agent_launcher_;
};
