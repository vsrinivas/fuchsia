// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.serversuite/cpp/fidl.h>
#include <lib/sys/component/cpp/service_client.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "src/tests/fidl/channel_util/channel.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));

// Defines a new server test. Relies on gtest under the hood.
// Tests must use upper camel case names and be defined in the |Test| enum in
// serversuite.test.fidl.
#define SERVER_TEST(test_name, target_type)                                \
  struct ServerTestWrapper##test_name : public ServerTest {                \
    ServerTestWrapper##test_name()                                         \
        : ServerTest(fidl_serversuite::Test::k##test_name, target_type) {} \
  };                                                                       \
  TEST_F(ServerTestWrapper##test_name, test_name)

#define CLOSED_SERVER_TEST(test_name) \
  SERVER_TEST(test_name, fidl_serversuite::AnyTarget::Tag::kClosedTarget)
#define AJAR_SERVER_TEST(test_name) \
  SERVER_TEST(test_name, fidl_serversuite::AnyTarget::Tag::kAjarTarget)
#define OPEN_SERVER_TEST(test_name) \
  SERVER_TEST(test_name, fidl_serversuite::AnyTarget::Tag::kOpenTarget)

namespace server_suite {

class Reporter : public fidl::Server<fidl_serversuite::Reporter> {
 public:
  void ReceivedOneWayNoPayload(ReceivedOneWayNoPayloadCompleter::Sync& completer) override;

  bool received_one_way_no_payload() const { return received_one_way_no_payload_; }

  void ReceivedUnknownMethod(ReceivedUnknownMethodRequest& request,
                             ReceivedUnknownMethodCompleter::Sync& completer) override;

  std::optional<fidl_serversuite::UnknownMethodInfo> received_unknown_method() const {
    return unknown_method_info_;
  }

  void ReceivedStrictOneWay(ReceivedStrictOneWayCompleter::Sync& completer) override;
  bool received_strict_one_way() const { return received_strict_one_way_; }

  void ReceivedFlexibleOneWay(ReceivedFlexibleOneWayCompleter::Sync& completer) override;
  bool received_flexible_one_way() const { return received_flexible_one_way_; }

 private:
  bool received_one_way_no_payload_ = false;
  std::optional<fidl_serversuite::UnknownMethodInfo> unknown_method_info_;
  bool received_strict_one_way_ = false;
  bool received_flexible_one_way_ = false;
};

class ServerTest : private ::loop_fixture::RealLoop, public ::testing::Test {
 protected:
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

  explicit ServerTest(fidl_serversuite::Test test, fidl_serversuite::AnyTarget::Tag target_type)
      : test_(test), target_type_(target_type) {}

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

  fidl_serversuite::AnyTarget::Tag target_type_;

  fidl::SyncClient<fidl_serversuite::Runner> runner_;

  channel_util::Channel target_;

  Reporter reporter_;
};

}  // namespace server_suite

#endif  // SRC_TESTS_FIDL_SERVER_SUITE_HARNESS_HARNESS_H_
