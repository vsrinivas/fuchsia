// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"

#include <lib/sys/component/cpp/service_client.h>

#include <future>

namespace client_suite {

void ClientTest::SetUp() {
  auto runner_service = component::Connect<fidl_clientsuite::Runner>();
  ASSERT_OK(runner_service.status_value());
  runner_ = fidl::Client<fidl_clientsuite::Runner>(std::move(*runner_service), dispatcher());

  // Ensure the process hasn't crashed from a previous iteration.
  auto check_alive_result = WaitFor(runner_->CheckAlive());
  ASSERT_TRUE(check_alive_result.is_ok()) << check_alive_result.error_value();

  auto is_test_enabled_result = WaitFor(runner_->IsTestEnabled(test_));
  ASSERT_TRUE(is_test_enabled_result.is_ok()) << is_test_enabled_result.error_value();
  if (!is_test_enabled_result->is_enabled()) {
    GTEST_SKIP() << "(test skipped by binding server)";
    return;
  }

  ASSERT_OK(zx::channel::create(0, &client_, &server_.get()));
}

void ClientTest::TearDown() {
  // Ensure there are no pending events left from tests.
  RunLoopUntilIdle();

  // Ensure the process hasn't crashed unexpectedly.
  auto result = WaitFor(runner_->CheckAlive());
  ASSERT_TRUE(result.is_ok()) << result.error_value();

  if (closed_target_reporter_binding_.has_value()) {
    closed_target_reporter_binding_->Close(ZX_OK);
  }
  if (ajar_target_reporter_binding_.has_value()) {
    ajar_target_reporter_binding_->Close(ZX_OK);
  }
  if (open_target_reporter_binding_.has_value()) {
    open_target_reporter_binding_->Close(ZX_OK);
  }
}

std::shared_ptr<ClosedTargetEventReporter> ClientTest::ReceiveClosedEvents() {
  auto reporter_endpoints = fidl::CreateEndpoints<fidl_clientsuite::ClosedTargetEventReporter>();
  EXPECT_TRUE(reporter_endpoints.is_ok()) << reporter_endpoints.status_string();
  if (!reporter_endpoints.is_ok()) {
    return nullptr;
  }
  auto reporter = std::make_shared<ClosedTargetEventReporter>();
  closed_target_reporter_binding_ =
      fidl::BindServer(dispatcher(), std::move(reporter_endpoints->server), reporter,
                       [](auto*, fidl::UnbindInfo info, auto) {
                         EXPECT_TRUE(info.is_dispatcher_shutdown() || info.is_user_initiated() ||
                                     info.is_peer_closed())
                             << "ClosedTargetEventReporter unbound with error: " << info;
                       });

  auto result = WaitFor(runner()->ReceiveClosedEvents({{
      .target = TakeClosedClient(),
      .reporter = std::move(reporter_endpoints->client),
  }}));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  if (!result.is_ok()) {
    return nullptr;
  }

  return reporter;
}

std::shared_ptr<AjarTargetEventReporter> ClientTest::ReceiveAjarEvents() {
  auto reporter_endpoints = fidl::CreateEndpoints<fidl_clientsuite::AjarTargetEventReporter>();
  EXPECT_TRUE(reporter_endpoints.is_ok()) << reporter_endpoints.status_string();
  if (!reporter_endpoints.is_ok()) {
    return nullptr;
  }
  auto reporter = std::make_shared<AjarTargetEventReporter>();
  ajar_target_reporter_binding_ =
      fidl::BindServer(dispatcher(), std::move(reporter_endpoints->server), reporter,
                       [](auto*, fidl::UnbindInfo info, auto) {
                         EXPECT_TRUE(info.is_dispatcher_shutdown() || info.is_user_initiated() ||
                                     info.is_peer_closed())
                             << "AjarTargetEventReporter unbound with error: " << info;
                       });

  auto result = WaitFor(runner()->ReceiveAjarEvents({{
      .target = TakeAjarClient(),
      .reporter = std::move(reporter_endpoints->client),
  }}));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  if (!result.is_ok()) {
    return nullptr;
  }

  return reporter;
}

std::shared_ptr<OpenTargetEventReporter> ClientTest::ReceiveOpenEvents() {
  auto reporter_endpoints = fidl::CreateEndpoints<fidl_clientsuite::OpenTargetEventReporter>();
  EXPECT_TRUE(reporter_endpoints.is_ok()) << reporter_endpoints.status_string();
  if (!reporter_endpoints.is_ok()) {
    return nullptr;
  }
  auto reporter = std::make_shared<OpenTargetEventReporter>();
  open_target_reporter_binding_ =
      fidl::BindServer(dispatcher(), std::move(reporter_endpoints->server), reporter,
                       [](auto*, fidl::UnbindInfo info, auto) {
                         EXPECT_TRUE(info.is_dispatcher_shutdown() || info.is_user_initiated() ||
                                     info.is_peer_closed())
                             << "OpenTargetEventReporter unbound with error: " << info;
                       });

  auto result = WaitFor(runner()->ReceiveOpenEvents({{
      .target = TakeOpenClient(),
      .reporter = std::move(reporter_endpoints->client),
  }}));
  EXPECT_TRUE(result.is_ok()) << result.error_value();
  if (!result.is_ok()) {
    return nullptr;
  }

  return reporter;
}
}  // namespace client_suite
