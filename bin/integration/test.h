// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "apps/maxwell/src/application_environment_host_impl.h"
#include "apps/maxwell/src/user/agent_launcher.h"
#include "gtest/gtest.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

// 5s timeout for asyncs on signals (e.g. WaitForIncomingMethodCall).
constexpr auto kSignalDeadline = fxl::TimeDelta::FromSeconds(5);

// In practice, 100 ms is actually a bit short, so this may occasionally falsely
// succeed tests that should fail. Flakiness should thus be considered failure.
constexpr auto kAsyncCheckSteady = fxl::TimeDelta::FromMilliseconds(100);
constexpr auto kAsyncCheckMax = fxl::TimeDelta::FromSeconds(5);

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

#define PREDICATE(condition) [&] { return (bool)(condition); }
// Convenience macro that wraps |condition| in a |Predicate| and applies a
// timeout.
#define WAIT_UNTIL(condition)                                         \
  {                                                                   \
    auto deadline = Deadline(kAsyncCheckMax);                         \
    WaitUntil(PREDICATE(condition) || deadline);                      \
    if (deadline()) {                                                 \
      FAIL() << "Deadline exceeded while waiting for " << #condition; \
    }                                                                 \
  }

// Becomes true after |duration|.
Predicate Deadline(const fxl::TimeDelta& duration);

// Sleeps for a time while processing messages.
void Sleep(const fxl::TimeDelta& duration);

// Sleep for a default reasonable time for apps to start up.
void Sleep();

// Does a weak stability check on an async condition by waiting until the given
// condition is true (max 5s) and then ensuring that the condition remains true
// (for 100 ms).
//
// If the condition becomes true briefly but not over a 100 ms polling period,
// this check continues waiting until the deadline. Since the transient check
// is polling-based, the exact number of matches should not be relied upon.
//
// This is a macro rather than a function to preserve the file and line number
// of the failed assertion.
#define ASYNC_CHECK_DIAG(condition, diagnostic)                        \
  {                                                                    \
    auto deadline = Deadline(kAsyncCheckMax);                          \
    auto check = PREDICATE(condition);                                 \
    do {                                                               \
      WaitUntil(check || deadline);                                    \
      if (!(condition) && deadline()) {                                \
        FAIL() << "Deadline exceeded for async check: " << diagnostic; \
      }                                                                \
      auto steady = Deadline(kAsyncCheckSteady);                       \
      WaitUntil(steady || !check);                                     \
    } while (!(condition));                                            \
  }

#define ASYNC_CHECK(condition) ASYNC_CHECK_DIAG(condition, #condition)
#define ASYNC_EQ(expected, actual) \
  ASYNC_CHECK_DIAG(                \
      (expected) == (actual),      \
      #actual " == " #expected "; last known value: " << (actual))

extern app::ApplicationEnvironment* root_environment;

class MaxwellTestBase : public ::testing::Test {
 protected:
  MaxwellTestBase();
  virtual ~MaxwellTestBase() = default;

  void StartAgent(const std::string& url,
                  std::unique_ptr<app::ApplicationEnvironmentHost> env_host) {
    agent_launcher_->StartAgent(url, std::move(env_host));
  }

  app::ServiceProviderPtr StartServiceProvider(const std::string& url);

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(const std::string& url) {
    auto services = StartServiceProvider(url);
    return app::ConnectToService<Interface>(services.get());
  }

 private:
  maxwell::ApplicationEnvironmentHostImpl test_environment_host_;
  fidl::Binding<app::ApplicationEnvironmentHost> test_environment_host_binding_;
  app::ApplicationEnvironmentPtr test_environment_;
  // Hold a controller so that we kill all children when we go out of scope.
  app::ApplicationEnvironmentControllerPtr test_environment_controller_;
  app::ApplicationLauncherPtr test_launcher_;
  std::unique_ptr<maxwell::AgentLauncher> agent_launcher_;
};
