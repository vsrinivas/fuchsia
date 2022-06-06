// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"

#include <fidl/fidl.dynsuite/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/llcpp/client.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/service/llcpp/service.h>
#include <zircon/assert.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

#include <gtest/gtest.h>

std::ostream& operator<<(std::ostream& os, const Observation::Kind& kind) {
  switch (kind) {
    case Observation::Kind::kOnBind:
      os << "kOnBind";
      break;
    case Observation::Kind::kOnUnbind:
      os << "kOnUnbind";
      break;
    case Observation::Kind::kOnComplete:
      os << "kOnComplete";
      break;
    case Observation::Kind::kOnError:
      os << "kOnError";
      break;
    case Observation::Kind::kOnMethodInvocation:
      os << "kOnMethodInvocation";
      break;
  }
  return os;
}

void ObserverOrchestrator::Observe(ObserveRequestView request, ObserveCompleter::Sync& _completer) {
  // Record the observation.
  if (to_record_) {
    switch (request->observation.Which()) {
      case fidl_dynsuite::wire::Observation::Tag::kOnBind:
        to_record_->emplace_back(Observation::Kind::kOnBind);
        break;
      case fidl_dynsuite::wire::Observation::Tag::kOnUnbind:
        to_record_->emplace_back(Observation::Kind::kOnUnbind);
        break;
      case fidl_dynsuite::wire::Observation::Tag::kOnComplete:
        to_record_->emplace_back(Observation::Kind::kOnComplete);
        break;
      case fidl_dynsuite::wire::Observation::Tag::kOnError:
        to_record_->emplace_back(Observation::Kind::kOnError);
        break;
      case fidl_dynsuite::wire::Observation::Tag::kOnMethodInvocation:
        to_record_->emplace_back(Observation::Kind::kOnMethodInvocation);
        break;
      default:
        ZX_PANIC("Unknown observation");
    }
  }

  switch (request->observation.Which()) {
    case fidl_dynsuite::wire::Observation::Tag::kOnBind:
      std::cout << "observed: on bind" << std::endl;
      break;
    case fidl_dynsuite::wire::Observation::Tag::kOnUnbind:
      std::cout << "observed: on unbind" << std::endl;
      break;
    case fidl_dynsuite::wire::Observation::Tag::kOnComplete:
      std::cout << "observed: on complete" << std::endl;
      on_completion_callback_();
      break;
    case fidl_dynsuite::wire::Observation::Tag::kOnMethodInvocation: {
      std::string name;
      switch (request->observation.on_method_invocation().method) {
        case fidl_dynsuite::wire::Method::kOneWayInteractionNoPayload:
          name = "OneWayInteractionNoPayload";
          break;
      }
      std::cout << "observed: on method invocation of " << name << " @ "
                << static_cast<int32_t>(request->observation.on_method_invocation().method_point)
                << std::endl;
    } break;
    case fidl_dynsuite::wire::Observation::Tag::kOnError:
      std::cout << "observed: context=" << request->observation.on_error().context.get()
                << ", error="
                << zx_status_get_string(
                       static_cast<zx_status_t>(request->observation.on_error().err))
                << std::endl;
      break;
  }
}

void ObserverOrchestrator::RecordInto(std::vector<Observation>* to_record) {
  to_record_ = to_record;
}

void TestContext::start_server_test() {
  ZX_ASSERT(stage_ == Stage::kInitial);
  stage_ = Stage::kStarting;

  auto endpoints_to_observer = fidl::CreateEndpoints<fidl_dynsuite::Observer>();
  ZX_ASSERT(endpoints_to_observer.is_ok());
  auto [client_end_to_observer, server_end_to_observer] = *std::move(endpoints_to_observer);
  observer_orchestrator_.emplace(dispatcher_, std::move(server_end_to_observer),
                                 [this]() { this->NotifyOfCompletion(); });

  auto endpoints = fidl::CreateEndpoints<fidl_dynsuite::ServerTest>();
  ZX_ASSERT(endpoints.is_ok());
  auto [client_end_to_test, server_end_to_test] = *std::move(endpoints);
  ZX_ASSERT(entry_client_
                ->StartServerTest(std::move(server_end_to_test), std::move(client_end_to_observer))
                .ok());
  client_end_to_test_ = std::move(client_end_to_test);

  stage_ = Stage::kStarted;
}

void TestContext::start_client_test() {
  ZX_ASSERT(stage_ == Stage::kInitial);
  stage_ = Stage::kStarting;

  auto endpoints_to_observer = fidl::CreateEndpoints<fidl_dynsuite::Observer>();
  ZX_ASSERT(endpoints_to_observer.is_ok());
  auto [client_end_to_observer, server_end_to_observer] = *std::move(endpoints_to_observer);
  observer_orchestrator_.emplace(dispatcher_, std::move(server_end_to_observer),
                                 [this]() { this->NotifyOfCompletion(); });

  auto endpoints = fidl::CreateEndpoints<fidl_dynsuite::ClientTest>();
  ZX_ASSERT(endpoints.is_ok());
  auto [client_end_to_test, server_end_to_test] = *std::move(endpoints);
  ZX_ASSERT(entry_client_
                ->StartClientTest(std::move(client_end_to_test), std::move(client_end_to_observer))
                .ok());
  server_end_to_test_ = std::move(server_end_to_test);

  stage_ = Stage::kStarted;
}

zx::channel TestContext::TakeClientEndToTest() {
  ZX_ASSERT(Stage::kStarted <= stage_);
  return client_end_to_test_.TakeChannel();
}

zx::channel TestContext::TakeServerEndToTest() {
  ZX_ASSERT(Stage::kStarted <= stage_);
  return server_end_to_test_.TakeChannel();
}

bool TestContext::has_completed() { return (stage_ == Stage::kStopped); }

void TestContext::run(std::function<void()> when, std::function<bool(const Observations)> wait_for,
                      std::function<void(const Observations)> then_observe) {
  // Start recording.
  observations_being_recorded_ = std::make_unique<std::vector<Observation>>();
  observer_orchestrator_->RecordInto(observations_being_recorded_.get());

  // Execute the `when` clause.
  when();

  // Wait for.
  //
  // Note that timeout management is managed by run_until.
  bool collected_observations = test_base_->run_until(
      [&]() { return wait_for(Observations(*observations_being_recorded_)); });
  ASSERT_TRUE(collected_observations) << "timed out collecting observations";

  // Then observe.
  then_observe(Observations(*observations_being_recorded_));

  // Stop recording.
  observer_orchestrator_->RecordInto(nullptr);
  observations_being_recorded_.reset();
}

std::vector<Observation> TestContext::when_then_observe(std::function<void()> fn) {
  observations_being_recorded_ = std::make_unique<std::vector<Observation>>();

  // Before.
  observer_orchestrator_->RecordInto(observations_being_recorded_.get());

  // Do.
  fn();

  // After.
  observer_orchestrator_->RecordInto(nullptr);

  auto recorded = std::move(observations_being_recorded_);
  return *recorded;
}

bool TestContext::HasNNewObservations(uint32_t n) {
  ZX_ASSERT(observations_being_recorded_ && "must be in a when_then_observe block");
  return observations_being_recorded_->size() > n;
}

void TestContext::NotifyOfCompletion() {
  ZX_ASSERT(stage_ == Stage::kStarted);
  stage_ = Stage::kStopped;
}

[[nodiscard]] WaitFor When::wait_for(std::function<bool(const Observations)> wait_for_fn) {
  return WaitFor(context_, when_fn_, wait_for_fn);
}

void WaitFor::then_observe(std::function<void(const Observations)> then_observe_fn) {
  context_.run(when_fn_, wait_for_fn_, then_observe_fn);
}

void TestBase::SetUp() {
  ::testing::Test::SetUp();
  auto client_end_to_entry = service::Connect<fidl_dynsuite::Entry>();
  EXPECT_EQ(ZX_OK, client_end_to_entry.status_value());
  entry_client_.emplace(std::move(*client_end_to_entry), dispatcher());
  context_.emplace(this, dispatcher(), entry_client_.value());
}

void TestBase::TearDown() {
  ::testing::Test::TearDown();
  context_.reset();
  entry_client_.reset();
}

TestContext& TestBase::context() {
  EXPECT_TRUE(context_);
  return context_.value();
}

bool TestBase::run_until(std::function<bool()> condition) {
  constexpr zx::duration kTimeoutDuration = zx::sec(5);
  return RunLoopWithTimeoutOrUntil(condition, kTimeoutDuration);
}

When TestBase::when(std::function<void()> when_fn) { return When(context_.value(), when_fn); }

void ServerTest::SetUp() {
  ::TestBase::SetUp();
  context().start_server_test();
  client_end = context().TakeClientEndToTest().release();
}

void ServerTest::TearDown() {
  ::TestBase::Test::TearDown();
  client_end = ZX_HANDLE_INVALID;
}

void ClientTest::SetUp() {
  ::TestBase::SetUp();
  context().start_client_test();
  server_end = context().TakeServerEndToTest().release();
}

void ClientTest::TearDown() {
  ::TestBase::Test::TearDown();
  server_end = ZX_HANDLE_INVALID;
}
