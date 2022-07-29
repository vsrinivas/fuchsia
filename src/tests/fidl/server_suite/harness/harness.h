// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.serversuite/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "src/tests/fidl/channel_util/channel.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));

// Defines a new server test. Relies on gtest under the hood.
// Tests must use upper camel case names and be defined in the |Test| enum in
// serversuite.test.fidl.
#define SERVER_TEST(test_name)                                                           \
  struct ServerTestWrapper##test_name : public ServerTest {                              \
    ServerTestWrapper##test_name() : ServerTest(fidl_serversuite::Test::k##test_name) {} \
  };                                                                                     \
  TEST_F(ServerTestWrapper##test_name, test_name)

namespace server_suite {

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
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

  explicit ServerTest(fidl_serversuite::Test test) : test_(test) {}

  void SetUp() override;
  void TearDown() override;

  const Reporter& reporter() { return reporter_; }
  channel_util::Channel& client_end() { return target_; }

  // Use WAIT_UNTIL instead of calling |_wait_until| directly.
  bool _wait_until(fit::function<bool()> condition) {
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeoutDuration);
  }

 private:
  fidl_serversuite::Test test_;

  fidl::SyncClient<fidl_serversuite::Runner> runner_;

  channel_util::Channel target_;

  Reporter reporter_;
};

}  // namespace server_suite

#endif  // SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
