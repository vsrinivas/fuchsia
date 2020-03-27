// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/bridge.h"

#include <lib/async/cpp/executor.h>

#include "lib/fit/function_traits.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

constexpr zx::duration kTimeout = zx::sec(10);

class BridgeTest : public gtest::TestLoopFixture {
 public:
  BridgeTest() : executor_(dispatcher()) {}

 protected:
  template <typename V = void, typename E = void>
  Bridge<V, E> CreateBridge() {
    return Bridge<V, E>(dispatcher(), "test");
  }

  template <typename V, typename E>
  fit::result<V, E> ExecutePromise(fit::promise<V, E> promise,
                                   zx::duration run_time = zx::duration::infinite_past()) {
    fit::result<V, E> out_result;
    executor_.schedule_task(std::move(promise).then(
        [&](fit::result<V, E>& result) { out_result = std::move(result); }));
    if (run_time == zx::duration::infinite_past()) {
      RunLoopUntilIdle();
    } else {
      RunLoopFor(run_time);
    }
    return out_result;
  }

 private:
  async::Executor executor_;
};

TEST_F(BridgeTest, CompletesAtTimeout) {
  auto bridge = CreateBridge<>();

  ASSERT_FALSE(bridge.IsAlreadyDone());

  bridge.WaitForDone(kTimeout);
  RunLoopFor(kTimeout);

  EXPECT_TRUE(bridge.IsAlreadyDone());
}

TEST_F(BridgeTest, ExecutesIfTimeout) {
  bool timeout_did_run = false;

  auto bridge = CreateBridge<>();

  EXPECT_TRUE(
      ExecutePromise(bridge.WaitForDone(kTimeout, /*if_timeout=*/[&] { timeout_did_run = true; }),
                     kTimeout)
          .is_error());

  EXPECT_TRUE(timeout_did_run);
}

TEST_F(BridgeTest, CompleteError) {
  bool timeout_did_run = false;

  auto bridge = CreateBridge<>();

  bridge.CompleteError();

  EXPECT_TRUE(bridge.IsAlreadyDone());
  EXPECT_TRUE(ExecutePromise(bridge.WaitForDone(kTimeout, /*if_timeout=*/[&] {
                timeout_did_run = true;
              })).is_error());

  EXPECT_FALSE(timeout_did_run);
}

TEST_F(BridgeTest, CompleteOk) {
  auto bridge = CreateBridge<std::string>();

  bridge.CompleteOk("ok");

  EXPECT_TRUE(bridge.IsAlreadyDone());

  fit::result<std::string> result = ExecutePromise(bridge.WaitForDone());
  EXPECT_TRUE(result.is_ok());
  EXPECT_EQ(result.value(), "ok");
}

}  // namespace
}  // namespace feedback
