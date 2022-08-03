// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.clientsuite/cpp/wire_messaging.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/lib/testing/predicates/status.h"
#include "src/tests/fidl/channel_util/channel.h"

#define WAIT_UNTIL(condition) ASSERT_TRUE(_wait_until(condition));

// Defines a new client test. Relies on gtest under the hood.
// Tests must use upper camel case names and be defined in the |Test| enum in
// clientsuite.test.fidl.
#define CLIENT_TEST(test_name)                                                           \
  struct ClientTestWrapper##test_name : public ::client_suite::ClientTest {              \
    ClientTestWrapper##test_name() : ClientTest(fidl_clientsuite::Test::k##test_name) {} \
  };                                                                                     \
  TEST_F(ClientTestWrapper##test_name, test_name)

namespace client_suite {

class ClientTest : private ::loop_fixture::RealLoop, public ::testing::Test {
 public:
  static constexpr zx::duration kTimeoutDuration = zx::sec(5);

  explicit ClientTest(fidl_clientsuite::Test test) : test_(test) {}

  static void SetUpTestSuite();
  static void TearDownTestSuite();

  // Use WAIT_UNTIL instead of calling |_wait_until| directly.
  bool _wait_until(fit::function<bool()> condition) {
    return RunLoopWithTimeoutOrUntil(std::move(condition), kTimeoutDuration);
  }

  fidl::WireSyncClient<fidl_clientsuite::Target>& target() { return target_; }

 protected:
  void SetUp() override;
  void TearDown() override;

 private:
  fidl::WireSyncClient<fidl_clientsuite::Target> target_;
  fidl::WireSyncClient<fidl_clientsuite::Finisher> finisher_;
  fidl_clientsuite::Test test_;

  static fidl::WireSyncClient<fidl_clientsuite::Harness> harness_;
};

}  // namespace client_suite
