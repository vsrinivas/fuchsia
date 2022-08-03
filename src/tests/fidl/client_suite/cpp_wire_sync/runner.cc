// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/tests/fidl/client_suite/cpp_wire_sync/runner.h"

#include <lib/service/llcpp/service.h>

namespace client_suite {

fidl::WireSyncClient<fidl_clientsuite::Harness> ClientTest::harness_;

void ClientTest::SetUpTestSuite() {
  auto harness = service::Connect<fidl_clientsuite::Harness>();
  ZX_ASSERT(ZX_OK == harness.status_value());

  harness_.Bind(std::move(*harness));
}

void ClientTest::TearDownTestSuite() {}

void ClientTest::SetUp() {
  auto start_result = harness_->Start(test_);
  ASSERT_TRUE(start_result.ok()) << start_result.error().FormatDescription();

  ASSERT_OK(
      start_result->target.channel().get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr, nullptr));
  ASSERT_OK(start_result->finisher.channel().get_info(ZX_INFO_HANDLE_VALID, nullptr, 0, nullptr,
                                                      nullptr));

  target_.Bind(std::move(start_result->target));
  finisher_.Bind(std::move(start_result->finisher));
}

void ClientTest::TearDown() {
  auto finish_result = finisher_->Finish();
  ASSERT_TRUE(finish_result.ok()) << finish_result.error().FormatDescription();
  for (const fidl::StringView& error : finish_result->errors) {
    ADD_FAILURE() << "test harness reported failure: "
                  << std::string_view(error.data(), error.size());
  }
}

}  // namespace client_suite
