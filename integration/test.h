// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <chrono>

#include "gtest/gtest.h"
#include "apps/maxwell/agent_environment_host.h"
#include "apps/modular/lib/app/application_context.h"
#include "apps/modular/lib/app/connect.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/time/time_delta.h"

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

using namespace std::chrono_literals;

template <class Rep, class Period>
Predicate Deadline(const std::chrono::duration<Rep, Period>& duration) {
  using std::chrono::steady_clock;
  const auto deadline = steady_clock::now() + duration;
  return [deadline] { return steady_clock::now() >= deadline; };
}

// Sleeps for a time while processing messages.
template <class Rep, class Period>
void Sleep(const std::chrono::duration<Rep, Period>& duration) {
  WaitUntil(Deadline(duration));
}

// Sleep for a default reasonable time for apps to start up.
void Sleep();

// 2s timeout for asyncs on signals (e.g. WaitForIncomingMethodCall).
constexpr ftl::TimeDelta kSignalDeadline = ftl::TimeDelta::FromSeconds(2);

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
  MaxwellTestBase() {
    root_environment->CreateNestedEnvironment(
        test_environment_host_.PassBoundHandle(), GetProxy(&test_environment_),
        NULL);
    test_environment_host_.SetEnvironment(test_environment_.get());
    test_environment_->GetApplicationLauncher(GetProxy(&test_launcher_));
  };

  virtual ~MaxwellTestBase() = default;

  void StartAgent(const std::string& url,
                  std::unique_ptr<maxwell::AgentEnvironmentHost> env_host) {
    fidl::InterfaceHandle<modular::ApplicationEnvironmentHost> env_host_handle =
        agent_host_bindings_.AddBinding(std::move(env_host));

    modular::ApplicationEnvironmentPtr agent_env;
    test_environment_->CreateNestedEnvironment(std::move(env_host_handle),
                                               GetProxy(&agent_env), NULL);

    modular::ApplicationLauncherPtr agent_launcher;
    agent_env->GetApplicationLauncher(GetProxy(&agent_launcher));

    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = url;
    agent_launcher->CreateApplication(std::move(launch_info), NULL);
  }

  modular::ServiceProviderPtr StartEngine(const std::string& url) {
    modular::ServiceProviderPtr services;
    auto launch_info = modular::ApplicationLaunchInfo::New();
    launch_info->url = url;
    launch_info->services = GetProxy(&services);
    test_launcher_->CreateApplication(std::move(launch_info), NULL);
    return services;
  }

  template <typename Interface>
  fidl::InterfacePtr<Interface> ConnectToService(const std::string& url) {
    modular::ServiceProviderPtr services = StartEngine(url);

    fidl::InterfacePtr<Interface> service =
        modular::ConnectToService<Interface>(services.get());
    return service;
  }

 private:
  class TestEnvironmentHost : public modular::ApplicationEnvironmentHost,
                              public modular::ServiceProvider {
   public:
    TestEnvironmentHost() : binding_(this) {}

    void GetApplicationEnvironmentServices(
        fidl::InterfaceRequest<modular::ServiceProvider> environment_services)
        override {
      svc_bindings_.AddBinding(this, std::move(environment_services));
    }

    void ConnectToService(const fidl::String& interface_name,
                          mx::channel channel) override {
      if (interface_name == modular::ApplicationEnvironment::Name_) {
        env_bindings_.AddBinding(
            environment_,
            fidl::InterfaceRequest<modular::ApplicationEnvironment>(
                std::move(channel)));
      }
    }

    // Sets the environment hosted by this host, which should be the result
    // obtained by CreateNestedEnvironment after passing this host.
    void SetEnvironment(modular::ApplicationEnvironment* environment) {
      environment_ = environment;
    }

    fidl::InterfaceHandle<modular::ApplicationEnvironmentHost>
    PassBoundHandle() {
      fidl::InterfaceHandle<modular::ApplicationEnvironmentHost> handle;
      binding_.Bind(&handle);
      return handle;
    }

   private:
    fidl::Binding<modular::ApplicationEnvironmentHost> binding_;
    fidl::BindingSet<modular::ServiceProvider> svc_bindings_;
    fidl::BindingSet<modular::ApplicationEnvironment> env_bindings_;
    modular::ApplicationEnvironment* environment_;
  };

  TestEnvironmentHost test_environment_host_;
  modular::ApplicationEnvironmentPtr test_environment_;
  modular::ApplicationLauncherPtr test_launcher_;

  fidl::BindingSet<modular::ApplicationEnvironmentHost,
                   std::unique_ptr<maxwell::AgentEnvironmentHost>>
      agent_host_bindings_;
};
