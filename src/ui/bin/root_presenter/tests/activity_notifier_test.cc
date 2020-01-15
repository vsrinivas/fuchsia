// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/activity_notifier.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <fuchsia/ui/activity/cpp/fidl_test_base.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/async/time.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include "gtest/gtest.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace root_presenter {
namespace {

class FakeActivityTracker : public fuchsia::ui::activity::testing::Tracker_TestBase {
 public:
  void NotImplemented_(const std::string& name) final { ZX_DEBUG_ASSERT_IMPLEMENTED; }

  fidl::InterfaceRequestHandler<fuchsia::ui::activity::Tracker> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request) {
      binding_.Bind(std::move(request), dispatcher);
    };
  }

  void ReportDiscreteActivity(fuchsia::ui::activity::DiscreteActivity activity,
                              zx_time_t event_time,
                              ReportDiscreteActivityCallback callback) override {
    activities_.push_back(std::move(activity));
    callback();
  }

  const std::vector<fuchsia::ui::activity::DiscreteActivity>& activities() const {
    return activities_;
  }

 private:
  std::vector<fuchsia::ui::activity::DiscreteActivity> activities_;
  fidl::Binding<fuchsia::ui::activity::Tracker> binding_{this};
};

class ActivityNotifierImplTest : public gtest::TestLoopFixture {
 public:
  ActivityNotifierImplTest()
      : activity_notifier_(dispatcher(), ActivityNotifierImpl::kDefaultInterval,
                           *context_provider_.context()) {
    context_provider_.service_directory_provider()->AddService(
        fake_tracker_.GetHandler(dispatcher()));
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  ActivityNotifierImpl activity_notifier_;
  FakeActivityTracker fake_tracker_;
};

TEST_F(ActivityNotifierImplTest, KeyboardInput) {
  fuchsia::ui::input::InputEvent event;
  event.keyboard().phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  event.keyboard().code_point = 0x40;

  activity_notifier_.ReceiveInputEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_tracker_.activities().size(), 1u);
}

TEST_F(ActivityNotifierImplTest, PointerInput) {
  fuchsia::ui::input::InputEvent event;
  event.pointer().type = fuchsia::ui::input::PointerEventType::TOUCH;
  event.pointer().phase = fuchsia::ui::input::PointerEventPhase::ADD;

  activity_notifier_.ReceiveInputEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_tracker_.activities().size(), 1u);
}

TEST_F(ActivityNotifierImplTest, MediaButtonsInput) {
  fuchsia::ui::input::MediaButtonsEvent event;
  event.set_volume(10);

  activity_notifier_.ReceiveMediaButtonsEvent(event);
  RunLoopUntilIdle();
  EXPECT_EQ(fake_tracker_.activities().size(), 1u);
}

TEST_F(ActivityNotifierImplTest, FocusEventsIgnored) {
  fuchsia::ui::input::FocusEvent focus;
  fuchsia::ui::input::InputEvent event;
  event.set_focus(focus);

  activity_notifier_.ReceiveInputEvent(event);
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_tracker_.activities().empty());
}

TEST_F(ActivityNotifierImplTest, MultipleInputsWithinInterval) {
  fuchsia::ui::input::InputEvent event1, event2;
  event1.keyboard().phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  event1.keyboard().code_point = 0x40;
  event2.pointer().type = fuchsia::ui::input::PointerEventType::TOUCH;
  event2.pointer().phase = fuchsia::ui::input::PointerEventPhase::ADD;

  activity_notifier_.ReceiveInputEvent(event1);
  RunLoopUntilIdle();

  EXPECT_EQ(fake_tracker_.activities().size(), 1u);

  activity_notifier_.ReceiveInputEvent(event2);
  RunLoopUntilIdle();

  // Only one event ought to have been sent
  EXPECT_EQ(fake_tracker_.activities().size(), 1u);
}

TEST_F(ActivityNotifierImplTest, MultipleInputsAcrossInterval) {
  fuchsia::ui::input::InputEvent event1, event2;
  event1.keyboard().phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  event1.keyboard().code_point = 0x40;
  event2.pointer().type = fuchsia::ui::input::PointerEventType::TOUCH;
  event2.pointer().phase = fuchsia::ui::input::PointerEventPhase::ADD;

  activity_notifier_.ReceiveInputEvent(event1);
  RunLoopFor(ActivityNotifierImpl::kDefaultInterval);
  activity_notifier_.ReceiveInputEvent(event2);
  RunLoopUntilIdle();

  EXPECT_EQ(fake_tracker_.activities().size(), 2u);
}

}  // namespace
}  // namespace root_presenter
