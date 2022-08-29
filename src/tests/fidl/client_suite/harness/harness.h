// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_
#define SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_

#include <fidl/fidl.clientsuite/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "src/tests/fidl/channel_util/channel.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));

namespace client_suite {

#define CLIENT_TEST(test_name)                                                           \
  struct ClientTestWrapper##test_name : public ClientTest {                              \
    ClientTestWrapper##test_name() : ClientTest(fidl_clientsuite::Test::k##test_name) {} \
  };                                                                                     \
  TEST_F(ClientTestWrapper##test_name, test_name)

class ClientTest : public ::loop_fixture::RealLoop, public ::testing::Test {
 protected:
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

  explicit ClientTest(fidl_clientsuite::Test test) : test_(test) {}

  void SetUp() override;
  void TearDown() override;

  fidl::Client<fidl_clientsuite::Runner>& runner() { return runner_; }
  channel_util::Channel& server_end() { return server_; }

  // Take the client end of the channel corresponding to |server_end| as a
  // |ClosedTarget| |ClientEnd|.
  fidl::ClientEnd<fidl_clientsuite::ClosedTarget> TakeClosedClient() {
    return fidl::ClientEnd<fidl_clientsuite::ClosedTarget>(std::move(client_));
  }

  // Use WAIT_UNTIL instead of calling |_wait_until| directly.
  bool _wait_until(fit::function<bool()> condition) {
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeoutDuration);
  }

  // Wait for a NaturalThenable to complete. Call this on an async method to
  // finish waiting for it synchronously.
  template <typename FidlMethod>
  fidl::Result<FidlMethod> WaitFor(fidl::internal::NaturalThenable<FidlMethod>&& thenable) {
    std::optional<fidl::Result<FidlMethod>> result_out;
    std::move(thenable).ThenExactlyOnce(
        [&result_out](auto& result) { result_out = std::move(result); });
    RunLoopUntil([&result_out]() { return result_out.has_value(); });
    return std::move(result_out.value());
  }

 private:
  fidl_clientsuite::Test test_;

  fidl::Client<fidl_clientsuite::Runner> runner_;

  zx::channel client_;
  channel_util::Channel server_;
};

}  // namespace client_suite

#endif  // SRC_TESTS_FIDL_CLIENT_SUITE_HARNESS_HARNESS_H_
