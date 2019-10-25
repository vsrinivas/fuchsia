// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "garnet/bin/trace_manager/tests/trace_manager_test.h"

namespace tracing {
namespace test {

TraceManagerTest::TraceManagerTest() {
}

void TraceManagerTest::SetUp() {
  TestLoopFixture::SetUp();

  Config config;
  ASSERT_TRUE(config.ReadFrom(kConfigFile));

  std::unique_ptr<sys::ComponentContext> context{context_provider_.TakeContext()};
  app_.reset(new TraceManagerApp(std::move(context), std::move(config)));
}

void TraceManagerTest::TearDown() {
  fake_provider_bindings_.clear();
  app_.reset();
  TestLoopFixture::TearDown();
}

void TraceManagerTest::ConnectToControllerService() {
  context_provider().ConnectToPublicService(controller_.NewRequest());
}

void TraceManagerTest::DisconnectFromControllerService() {
  controller_.Unbind();
}

bool TraceManagerTest::AddFakeProvider(zx_koid_t pid,
                                       const std::string& name,
                                       FakeProvider** out_provider) {
  provider::RegistryPtr registry;
  context_provider().ConnectToPublicService(registry.NewRequest());

  auto provider_impl = std::make_unique<FakeProvider>(pid, name);
  auto provider = std::make_unique<FakeProviderBinding>(std::move(provider_impl));

  fidl::InterfaceHandle<provider::Provider> provider_client{
      provider->NewBinding()};
  if (!provider_client.is_valid()) {
    return false;
  }

  registry->RegisterProvider(std::move(provider_client),
                             provider->impl()->pid(),
                             provider->impl()->name());
  if (out_provider) {
    *out_provider = provider->impl().get();
  }
  fake_provider_bindings_.push_back(std::move(provider));
  return true;
}

TraceManagerTest::SessionState
TraceManagerTest::GetSessionState() const {
  if (trace_manager()->session()) {
    switch (trace_manager()->session()->state()) {
#define TRANSLATE_STATE(state) \
    case TraceSession::State::state: return SessionState::state;
    TRANSLATE_STATE(kReady);
    TRANSLATE_STATE(kStarted);
    TRANSLATE_STATE(kStopping);
    TRANSLATE_STATE(kStopped);
#undef TRANSLATE_STATE
    }
  }
  return SessionState::kNonexistent;
}

// static
TraceConfig TraceManagerTest::GetDefaultTraceConfig() {
  std::vector<std::string> categories{kTestCategory};
  TraceConfig config;
  config.set_categories(std::move(categories));
  config.set_buffer_size_megabytes_hint(kDefaultBufferSizeMegabytes);
  config.set_start_timeout_milliseconds(kDefaultStartTimeoutMilliseconds);
  config.set_buffering_mode(controller::BufferingMode::ONESHOT);
  return config;
}

void TraceManagerTest::StartSessionWorker(TraceConfig config, bool* success) {
  // Require a mode to be set, no default here.
  FXL_CHECK(config.has_buffering_mode());

  zx::socket our_socket, their_socket;
  zx_status_t status = zx::socket::create(0u, &our_socket, &their_socket);
  ASSERT_EQ(status, ZX_OK);

  bool start_completed = false;
  auto callback = [&start_completed]() {
    start_completed = true;
  };
  controller_->StartTracing(std::move(config), std::move(their_socket), callback);

  RunLoopUntilIdle();

  // The loop will exit for the transition to kStarting.
  // If there are no tracees then it will also subsequently transition to
  // kStarted before the loop exits. If there are tracees then we need to
  // wait for them to start.
  if (fake_provider_bindings().size() > 0) {
    FXL_VLOG(2) << "Loop done, expecting session starting";
    ASSERT_EQ(GetSessionState(), SessionState::kStarting);

    // Make sure all providers are marked kStarting.
    // The loop exits when we transition to kStarting, but providers won't have
    // processed their Start requests yet.
    RunLoopUntilIdle();

    MarkAllProvidersStarted();

    // Wait until all providers are started.
    RunLoopUntilIdle();
  }

  // The loop will exit for the transition to kStarted.
  FXL_VLOG(2) << "Loop done, expecting all providers started";
  ASSERT_EQ(GetSessionState(), SessionState::kStarted);

  // Run the loop one more time to ensure we pick up the result.
  RunLoopUntilIdle();
  ASSERT_TRUE(start_completed);

  destination_ = std::move(our_socket);

  *success = true;
}

bool TraceManagerTest::StartSession(TraceConfig config) {
  bool success{};
  FXL_VLOG(2) << "Starting session";
  StartSessionWorker(std::move(config), &success);
  if (success) {
    FXL_VLOG(2) << "Session started";
  }
  return success;
}

void TraceManagerTest::StopSessionWorker(bool* success) {
  controller_->StopTracing();

  RunLoopUntilIdle();

  if (fake_provider_bindings().size() > 0) {
    FXL_VLOG(2) << "Loop done, expecting session stopping";
    ASSERT_EQ(GetSessionState(), SessionState::kStopping);

    MarkAllProvidersStopped();

    // Wait until all providers are stopped.
    RunLoopUntilIdle();
  }

  FXL_VLOG(2) << "Loop done, expecting session stopped";
  ASSERT_EQ(GetSessionState(), SessionState::kStopped);

  destination_.reset();

  *success = true;
}

bool TraceManagerTest::StopSession() {
  bool success{};
  FXL_VLOG(2) << "Stopping session";
  StopSessionWorker(&success);
  if (success) {
    FXL_VLOG(2) << "Session stopped";
  }
  return success;
}

void TraceManagerTest::MarkAllProvidersStarted() {
  FXL_VLOG(2) << "Marking all providers started";
  for (const auto& p : fake_provider_bindings()) {
    p->impl()->MarkStarted();
  }
}

void TraceManagerTest::MarkAllProvidersStopped() {
  FXL_VLOG(2) << "Marking all providers stopped";
  for (const auto& p : fake_provider_bindings()) {
    p->impl()->MarkStopped();
  }
}

}  // namespace test
}  // namespace tracing
