// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TESTS_TRACE_MANAGER_TEST_H_
#define GARNET_BIN_TRACE_MANAGER_TESTS_TRACE_MANAGER_TEST_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/zx/socket.h>

#include "garnet/bin/trace_manager/app.h"
#include "garnet/bin/trace_manager/tests/fake_provider.h"

namespace tracing {
namespace test {

namespace controller = ::fuchsia::tracing::controller;

// TODO(dje): Temporary renaming to ease transition to new name.
using TraceConfig = controller::TraceOptions;

class TraceManagerTest : public gtest::TestLoopFixture {
 public:
  // |TraceSession| intentionally doesn't have something like |kNone| as that is
  // represented by the session being non-existent. However, it's helpful in
  // tests to have a value to represent this state so we have our own copy of
  // |TraceSession::State|. It is not named |State| to help avoid confusion.
  enum class SessionState {
    // These values are all copies of |TraceSession::State|.
    kReady, kStarted, kStopping, kStopped,
    // New value to represent state between kReady and kStarted.
    kStarting,
    // This is the new value to represent |TraceManager::session_| == nullptr.
    kNonexistent,
  };

  static constexpr unsigned kDefaultBufferSizeMegabytes = 1;

  // This is effectively infinite.
  static constexpr unsigned kDefaultStartTimeoutMilliseconds = 3600 * 1000;

  static constexpr char kConfigFile[] = "/pkg/data/tracing.config";

  static constexpr char kTestCategory[] = "test";

  static constexpr zx_koid_t kProvider1Pid = 1234;
  static constexpr char kProvider1Name[] = "test-provider1";

  static constexpr zx_koid_t kProvider2Pid = 1235;
  static constexpr char kProvider2Name[] = "test-provider2";

  static TraceConfig GetDefaultTraceConfig();

  TraceManagerTest();

  const TraceManager* trace_manager() const { return app_->trace_manager(); }

  sys::testing::ComponentContextProvider& context_provider() { return context_provider_; }

  const controller::ControllerPtr& controller() const { return controller_; }

  const std::vector<std::unique_ptr<FakeProviderBinding>>& fake_provider_bindings() const {
    return fake_provider_bindings_;
  }

  void ConnectToControllerService();
  void DisconnectFromControllerService();

  // The caller must run the loop to complete the registration.
  // If |*out_provider| is non-NULL, a borrowed copy of the pointer is
  // stored there, and is valid for the duration of the test.
  bool AddFakeProvider(zx_koid_t pid, const std::string& name,
                       FakeProvider** out_provider = nullptr);

  // Fetch the session's state.
  SessionState GetSessionState() const;

  // Wrappers to simplify the standard operations.
  // These are only to be called at times when they're expected to succeed.
  // E.g., Don't call |StopSession()| if session is already stopped,
  // and so on. If you want to perform these operations outside of this
  // constraint, roll your own or use something else.
  // These assume the controller connection is already made.
  bool StartSession(TraceConfig config = GetDefaultTraceConfig());
  bool StopSession();

  // Helpers to advance provider state.
  // These can only be called when all providers are in the immediately
  // preceding state (e.g., kReady->kStarted).
  void MarkAllProvidersStarted();
  void MarkAllProvidersStopped();

  // Publically accessible copies of test fixture methods.
  void QuitLoop() { gtest::TestLoopFixture::QuitLoop(); }
  void RunLoopUntilIdle() { gtest::TestLoopFixture::RunLoopUntilIdle(); }
  void RunLoopFor(zx::duration duration) {
    gtest::TestLoopFixture::RunLoopFor(std::move(duration));
  }

 private:
  void SetUp() override;
  void TearDown() override;

  // Helper functions to cope with functions calling |ASSERT_*|: they have to return void.
  void StartSessionWorker(TraceConfig config, bool* success);
  void StopSessionWorker(bool* success);

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<TraceManagerApp> app_;

  // Interfaces to make service requests.
  controller::ControllerPtr controller_;

  // Socket for communication with controller.
  zx::socket destination_;

  // Containers for provider bindings so that they get cleaned up at the end of the test.
  std::vector<std::unique_ptr<FakeProviderBinding>> fake_provider_bindings_;
};

}  // namespace test
}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TESTS_TRACE_MANAGER_TEST_H_
