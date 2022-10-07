// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"

#include <lib/sys/component/cpp/service_client.h>

namespace server_suite {

void Reporter::ReceivedOneWayNoPayload(ReceivedOneWayNoPayloadCompleter::Sync& completer) {
  received_one_way_no_payload_ = true;
}

void Reporter::ReceivedUnknownMethod(ReceivedUnknownMethodRequest& request,
                                     ReceivedUnknownMethodCompleter::Sync& completer) {
  unknown_method_info_ = std::move(request);
}

void Reporter::ReceivedStrictOneWay(ReceivedStrictOneWayCompleter::Sync& completer) {
  received_strict_one_way_ = true;
}

void Reporter::ReceivedFlexibleOneWay(ReceivedFlexibleOneWayCompleter::Sync& completer) {
  received_flexible_one_way_ = true;
}

void ServerTest::SetUp() {
  auto runner_service = component::Connect<fidl_serversuite::Runner>();
  ASSERT_OK(runner_service.status_value());
  runner_ = fidl::SyncClient<fidl_serversuite::Runner>(std::move(*runner_service));

  // Ensure the process hasn't crashed from a previous iteration.
  auto check_alive_result = runner_->CheckAlive();
  ASSERT_TRUE(check_alive_result.is_ok()) << check_alive_result.error_value();

  auto is_test_enabled_result = runner_->IsTestEnabled(test_);
  ASSERT_TRUE(is_test_enabled_result.is_ok()) << is_test_enabled_result.error_value();
  if (!is_test_enabled_result->is_enabled()) {
    GTEST_SKIP() << "(test skipped by binding server)";
    return;
  }

  // Create Reporter, which will allow the binding server to report test progress.
  auto reporter_endpoints = fidl::CreateEndpoints<fidl_serversuite::Reporter>();
  ASSERT_OK(reporter_endpoints.status_value());
  fidl::BindServer(dispatcher(), std::move(reporter_endpoints->server), &reporter_,
                   [](auto*, fidl::UnbindInfo info, auto) {
                     ASSERT_TRUE(info.is_dispatcher_shutdown() || info.is_user_initiated() ||
                                 info.is_peer_closed())
                         << "reporter server unbound with error: "
                         << info.FormatDescription().c_str();
                   });

  // Create a Target on the test server, to run tests against.
  std::optional<fidl_serversuite::AnyTarget> target_server;
  switch (target_type_) {
    case fidl_serversuite::AnyTarget::Tag::kClosedTarget: {
      auto target_endpoints = fidl::CreateEndpoints<fidl_serversuite::ClosedTarget>();
      ASSERT_TRUE(target_endpoints.is_ok()) << target_endpoints.status_string();
      target_ = channel_util::Channel(target_endpoints->client.TakeChannel());
      target_server =
          fidl_serversuite::AnyTarget::WithClosedTarget(std::move(target_endpoints->server));
      break;
    }
    case fidl_serversuite::AnyTarget::Tag::kAjarTarget: {
      auto target_endpoints = fidl::CreateEndpoints<fidl_serversuite::AjarTarget>();
      ASSERT_TRUE(target_endpoints.is_ok()) << target_endpoints.status_string();
      target_ = channel_util::Channel(target_endpoints->client.TakeChannel());
      target_server =
          fidl_serversuite::AnyTarget::WithAjarTarget(std::move(target_endpoints->server));
      break;
    }
    case fidl_serversuite::AnyTarget::Tag::kOpenTarget: {
      auto target_endpoints = fidl::CreateEndpoints<fidl_serversuite::OpenTarget>();
      ASSERT_TRUE(target_endpoints.is_ok()) << target_endpoints.status_string();
      target_ = channel_util::Channel(target_endpoints->client.TakeChannel());
      target_server =
          fidl_serversuite::AnyTarget::WithOpenTarget(std::move(target_endpoints->server));
      break;
    }
  }
  auto start_result = runner_->Start(fidl_serversuite::RunnerStartRequest(
      std::move(reporter_endpoints->client), std::move(target_server.value())));
  ASSERT_TRUE(start_result.is_ok()) << start_result.error_value();

  ASSERT_OK(target_.get().get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
}

void ServerTest::TearDown() {
  // Close the Target channel so it will not continue waiting for requests.
  target_.reset();

  // Ensure the process hasn't crashed unexpectedly.
  auto result = runner_->CheckAlive();
  ASSERT_TRUE(result.is_ok()) << result.error_value();
}

}  // namespace server_suite
