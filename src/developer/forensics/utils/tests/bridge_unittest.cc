// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fit/bridge.h"

#include <lib/async/cpp/executor.h>

#include "src/developer/forensics/utils/errors.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace forensics {
namespace fit {
namespace {

constexpr zx::duration kTimeout = zx::sec(10);

class BridgeTest : public gtest::TestLoopFixture {
 public:
  BridgeTest() : executor_(dispatcher()) {}

 protected:
  template <typename V = void>
  Bridge<V> CreateBridge() {
    return Bridge<V>(dispatcher(), "test");
  }

  template <typename V, typename E>
  ::fpromise::result<V, E> ExecutePromise(::fpromise::promise<V, E> promise,
                                          zx::duration run_time = zx::duration::infinite_past()) {
    ::fpromise::result<V, E> out_result;
    executor_.schedule_task(std::move(promise).then(
        [&](::fpromise::result<V, E>& result) { out_result = std::move(result); }));
    if (run_time == zx::duration::infinite_past()) {
      RunLoopUntilIdle();
    } else {
      RunLoopFor(run_time);
    }
    return out_result;
  }

  async::Executor executor_;
};

TEST_F(BridgeTest, CompletesAtTimeout) {
  auto bridge = CreateBridge<>();

  ASSERT_FALSE(bridge.IsAlreadyDone());

  bridge.WaitForDone(fit::Timeout(kTimeout));
  RunLoopFor(kTimeout);

  EXPECT_TRUE(bridge.IsAlreadyDone());
}

TEST_F(BridgeTest, ExecutesIfTimeout) {
  bool timeout_did_run = false;

  auto bridge = CreateBridge<>();
  Error error = Error::kNotSet;

  executor_.schedule_task(
      bridge.WaitForDone(Timeout(kTimeout, /*action=*/[&] { timeout_did_run = true; }))
          .or_else([&](const Error& result) { error = result; }));
  RunLoopFor(kTimeout);

  EXPECT_TRUE(timeout_did_run);
  EXPECT_EQ(error, Error::kTimeout);
}

TEST_F(BridgeTest, CompleteError) {
  bool timeout_did_run = false;

  auto bridge = CreateBridge<>();

  bridge.CompleteError(Error::kDefault);

  EXPECT_TRUE(bridge.IsAlreadyDone());
  EXPECT_TRUE(ExecutePromise(
                  bridge.WaitForDone(Timeout(kTimeout, /*action=*/[&] { timeout_did_run = true; })),
                  kTimeout)
                  .is_error());

  EXPECT_FALSE(timeout_did_run);
}

TEST_F(BridgeTest, CompleteOk) {
  auto bridge = CreateBridge<std::string>();

  bridge.CompleteOk("ok");

  EXPECT_TRUE(bridge.IsAlreadyDone());

  ::fpromise::result<std::string, Error> result = ExecutePromise(bridge.WaitForDone());
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.value(), "ok");
}

}  // namespace
}  // namespace fit
}  // namespace forensics
