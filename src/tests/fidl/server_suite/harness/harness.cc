// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"

#include <lib/service/llcpp/service.h>

namespace server_suite {

void Reporter::ReceivedOneWayNoPayload(ReceivedOneWayNoPayloadRequest& request,
                                       ReceivedOneWayNoPayloadCompleter::Sync& completer) {
  received_one_way_no_payload_ = true;
}

void ServerTest::SetUp() {
  auto runner_service = service::Connect<fidl_serversuite::Runner>();
  ASSERT_OK(runner_service.status_value());
  runner_ = fidl::SyncClient<fidl_serversuite::Runner>(std::move(*runner_service));

  // Ensure the process hasn't crashed from a previous iteration.
  auto checkAliveResult = runner_->CheckAlive();
  ASSERT_TRUE(checkAliveResult.is_ok()) << checkAliveResult.error_value();

  auto isTestEnabledResult = runner_->IsTestEnabled(test_);
  ASSERT_TRUE(isTestEnabledResult.is_ok()) << isTestEnabledResult.error_value();
  if (!isTestEnabledResult->is_enabled()) {
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
  auto startResult =
      runner_->Start(fidl_serversuite::RunnerStartRequest(std::move(reporter_endpoints->client)));
  ASSERT_TRUE(startResult.is_ok()) << startResult.error_value();
  target_ = channel_util::Channel(startResult->target().TakeHandle());
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
