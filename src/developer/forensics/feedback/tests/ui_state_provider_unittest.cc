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
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/timekeeper/async_test_clock.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

class UIStateProviderTest : public UnitTestFixture {
 public:
  UIStateProviderTest()
      : ui_state_provider_(std::make_unique<timekeeper::AsyncTestClock>(loop_.dispatcher())) {}

 protected:
  async::TestLoop loop_;
  UIStateProvider ui_state_provider_;
};

TEST_F(UIStateProviderTest, GetKeys) {
  EXPECT_THAT(ui_state_provider_.GetKeys(),
              UnorderedElementsAreArray(
                  {kSystemUserActivityCurrentStateKey, kSystemUserActivityCurrentDurationKey}));
}

TEST_F(UIStateProviderTest, Get_NoStateChanges) {
  EXPECT_THAT(ui_state_provider_.Get(), IsEmpty());
}

TEST_F(UIStateProviderTest, Get) {
  EXPECT_THAT(ui_state_provider_.Get(), IsEmpty());

  ui_state_provider_.OnStateChanged(fuchsia::ui::activity::State::ACTIVE, zx::sec(1).get(),
                                    []() {});
  loop_.RunFor((zx::sec(3)));

  EXPECT_THAT(ui_state_provider_.Get(), UnorderedElementsAreArray({
                                            Pair(kSystemUserActivityCurrentDurationKey, "0d0h0m2s"),
                                        }));
}

TEST_F(UIStateProviderTest, GetOnUpdate) {
  Annotations annotations;
  ui_state_provider_.GetOnUpdate(
      [&annotations](const Annotations& cached_annotations) { annotations = cached_annotations; });

  EXPECT_THAT(annotations, IsEmpty());

  ui_state_provider_.OnStateChanged(fuchsia::ui::activity::State::UNKNOWN, zx::sec(1).get(),
                                    []() {});
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "unknown")}));

  ui_state_provider_.OnStateChanged(fuchsia::ui::activity::State::ACTIVE, zx::sec(2).get(),
                                    []() {});
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "active")}));

  ui_state_provider_.OnStateChanged(fuchsia::ui::activity::State::IDLE, zx::sec(3).get(), []() {});
  EXPECT_THAT(annotations,
              UnorderedElementsAreArray({Pair(kSystemUserActivityCurrentStateKey, "idle")}));
}

TEST_F(UIStateProviderTest, OnStateChangedExecutesCallback) {
  bool acknowledgement = false;
  ui_state_provider_.OnStateChanged(fuchsia::ui::activity::State::ACTIVE, zx::sec(1).get(),
                                    [&acknowledgement]() { acknowledgement = true; });
  EXPECT_TRUE(acknowledgement);
}

}  // namespace
}  // namespace forensics::feedback
