// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.serversuite/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));
#define ASSERT_OK(value) ASSERT_EQ(ZX_OK, value)

class Reporter : public fidl::Server<fidl_serversuite::Reporter> {
 public:
  void ReceivedOneWayNoPayload(ReceivedOneWayNoPayloadRequest& request,
                               ReceivedOneWayNoPayloadCompleter::Sync& completer) override;

  bool received_one_way_no_payload() const { return received_one_way_no_payload_; }

 private:
  bool received_one_way_no_payload_ = false;
};

class ServerTest : private ::loop_fixture::RealLoop, public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;

  const Reporter& reporter() { return reporter_; }
  zx::channel& client_end() { return target_; }

  bool _wait_until(fit::function<bool()> condition) {
    constexpr zx::duration kTimeoutDuration = zx::sec(5);
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeoutDuration);
  }

 private:
  fidl::SyncClient<fidl_serversuite::Runner> runner_;

  zx::channel target_;

  Reporter reporter_;
};

#endif  // SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
