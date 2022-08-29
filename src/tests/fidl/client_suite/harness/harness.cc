// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harness.h"

#include <lib/service/llcpp/service.h>

#include <future>

namespace client_suite {

void ClientTest::SetUp() {
  auto runner_service = service::Connect<fidl_clientsuite::Runner>();
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
  // Ensure the process hasn't crashed unexpectedly.
  auto result = WaitFor(runner_->CheckAlive());
  ASSERT_TRUE(result.is_ok()) << result.error_value();
}

}  // namespace client_suite
