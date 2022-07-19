// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/ui_state_provider.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async-testing/test_loop.h>
#include <lib/zx/time.h>

#include <memory>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/gpretty_printers.h"
#include "src/developer/forensics/testing/stubs/ui_state_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/backoff/backoff.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

class MonotonicBackoff : public backoff::Backoff {
 public:
  zx::duration GetNext() override {
    const auto backoff = backoff_;
    backoff_ = backoff + zx::sec(1);
    return backoff;
  }
  void Reset() override { backoff_ = zx::sec(1); }

 private:
  zx::duration backoff_{zx::sec(1)};
};

class UIStateProviderTest : public UnitTestFixture {
 public:
  UIStateProviderTest()
      : server_(dispatcher(), fuchsia::ui::activity::State::UNKNOWN, zx::time(0)) {
    InjectServiceProvider(&server_);
    ui_state_provider_ = std::make_unique<UIStateProvider>(
        dispatcher(), services(), std::make_unique<timekeeper::AsyncTestClock>(dispatcher()),
        std::make_unique<MonotonicBackoff>());
  }

 protected:
  stubs::UIStateProvider server_;
  std::unique_ptr<UIStateProvider> ui_state_provider_;
};

TEST_F(UIStateProviderTest, GetKeys) {
  EXPECT_THAT(ui_state_provider_->GetKeys(),
              UnorderedElementsAreArray(
                  {kSystemUserActivityCurrentStateKey, kSystemUserActivityCurrentDurationKey}));
}

TEST_F(UIStateProviderTest, Get_NoStateChanges) {
  EXPECT_THAT(ui_state_provider_->Get(), IsEmpty());
}

TEST_F(UIStateProviderTest, Get) {
  EXPECT_THAT(ui_state_provider_->Get(), IsEmpty());

  server_.SetState(fuchsia::ui::activity::State::IDLE, zx::time(zx::sec(1).get()));
  RunLoopFor((zx::sec(3)));

  EXPECT_THAT(ui_state_provider_->Get(),
              UnorderedElementsAreArray({
                  Pair(kSystemUserActivityCurrentDurationKey, "0d0h0m2s"),
              }));
}

TEST_F(UIStateProviderTest, GetOnUpdate) {
  Annotations annotations;
  ui_state_provider_->GetOnUpdate(
      [&annotations](const Annotations& cached_annotations) { annotations = cached_annotations; });
  EXPECT_THAT(annotations, IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "unknown")}));

  server_.SetState(fuchsia::ui::activity::State::ACTIVE, zx::time(0));

  // The change hasn't propagated yet.
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "unknown")}));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "active")}));

  server_.SetState(fuchsia::ui::activity::State::IDLE, zx::time(0));

  // The change hasn't propagated yet.
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "active")}));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "idle")}));
}

TEST_F(UIStateProviderTest, OnStateChangedExecutesCallback) {
  bool acknowledgement = false;
  ui_state_provider_->OnStateChanged(fuchsia::ui::activity::State::ACTIVE, zx::sec(1).get(),
                                     [&acknowledgement]() { acknowledgement = true; });
  EXPECT_TRUE(acknowledgement);
}

TEST_F(UIStateProviderTest, ReconnectsOnProviderDisconnect) {
  Annotations annotations;

  ui_state_provider_->GetOnUpdate(
      [&annotations](const Annotations& cached_annotations) { annotations = cached_annotations; });

  EXPECT_THAT(annotations, IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, "unknown"),
                           }));

  server_.CloseConnection();
  ASSERT_FALSE(server_.IsBound());

  server_.SetState(fuchsia::ui::activity::State::ACTIVE, zx::time(0));

  // Connection should stay closed until Backoff allows it to reconnect
  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, Error::kConnectionError),
                           }));
  EXPECT_THAT(ui_state_provider_->Get(),
              UnorderedElementsAreArray({
                  Pair(kSystemUserActivityCurrentDurationKey, Error::kConnectionError),
              }));

  RunLoopFor(zx::sec(1));
  ASSERT_TRUE(server_.IsBound());
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, "active"),
                           }));
  EXPECT_THAT(ui_state_provider_->Get(),
              UnorderedElementsAreArray({
                  Pair(kSystemUserActivityCurrentDurationKey, "0d0h0m1s"),
              }));
}

TEST_F(UIStateProviderTest, ReconnectsOnListenerDisconnect) {
  Annotations annotations;

  ui_state_provider_->GetOnUpdate(
      [&annotations](const Annotations& cached_annotations) { annotations = cached_annotations; });

  EXPECT_THAT(annotations, IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, "unknown"),
                           }));

  server_.UnbindListener();
  server_.SetState(fuchsia::ui::activity::State::ACTIVE, zx::time(0));

  // Connection should stay closed until Backoff allows it to reconnect
  RunLoopUntilIdle();
  ASSERT_FALSE(server_.IsBound());
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, Error::kConnectionError),
                           }));
  EXPECT_THAT(ui_state_provider_->Get(),
              UnorderedElementsAreArray({
                  Pair(kSystemUserActivityCurrentDurationKey, Error::kConnectionError),
              }));

  RunLoopFor(zx::sec(1));
  ASSERT_TRUE(server_.IsBound());
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kSystemUserActivityCurrentStateKey, "active"),
                           }));
  EXPECT_THAT(ui_state_provider_->Get(),
              UnorderedElementsAreArray({
                  Pair(kSystemUserActivityCurrentDurationKey, "0d0h0m1s"),
              }));
}

}  // namespace
}  // namespace forensics::feedback
