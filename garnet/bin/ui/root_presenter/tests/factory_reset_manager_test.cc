// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/factory_reset_manager.h"

#include <fuchsia/recovery/cpp/fidl_test_base.h>
#include <lib/async/time.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/testing/test_with_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/status.h>

#include "gtest/gtest.h"

namespace root_presenter {
namespace testing {

class MockWatcher : public fuchsia::recovery::FactoryResetStateWatcher {
 public:
  MockWatcher(fidl::InterfaceRequest<fuchsia::recovery::FactoryResetStateWatcher> watcher_request)
      : binding_(this, std::move(watcher_request)) {}
  void OnStateChanged(fuchsia::recovery::FactoryResetState response,
                      OnStateChangedCallback callback) override {
    state_ = std::move(response);
    callback();
  }

  fuchsia::recovery::FactoryResetState& state() { return state_; }

 private:
  fuchsia::recovery::FactoryResetState state_;

  fidl::Binding<fuchsia::recovery::FactoryResetStateWatcher> binding_;
};

class FakeFactoryReset : public fuchsia::recovery::testing::FactoryReset_TestBase {
 public:
  void NotImplemented_(const std::string& name) final {}

  fidl::InterfaceRequestHandler<fuchsia::recovery::FactoryReset> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::recovery::FactoryReset> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  void Reset(ResetCallback callback) override {
    callback(ZX_OK);
    triggered_ = true;
  }

  bool triggered() const { return triggered_; }

 private:
  bool triggered_ = false;

  fidl::BindingSet<fuchsia::recovery::FactoryReset> bindings_;
};

class FactoryResetManagerTest : public component::testing::TestWithContext {
 public:
  void SetUp() final {
    controller().AddService(factory_reset_.GetHandler());

    startup_context_ = TakeContext();
    factory_reset_manager_ = std::make_unique<FactoryResetManager>(startup_context_.get());

    fidl::InterfaceHandle<fuchsia::recovery::FactoryResetStateWatcher> watcher_handle;
    watcher_ = std::make_unique<MockWatcher>(watcher_handle.NewRequest());
    factory_reset_manager_->SetWatcher(std::move(watcher_handle));
  }

  bool triggered() const { return factory_reset_.triggered(); }

 protected:
  std::unique_ptr<FactoryResetManager> factory_reset_manager_;

  std::unique_ptr<MockWatcher> watcher_;

 private:
  std::unique_ptr<component::StartupContext> startup_context_;
  FakeFactoryReset factory_reset_;
};

TEST_F(FactoryResetManagerTest, FactoryResetButtonPressedAndReleased) {
  EXPECT_FALSE(factory_reset_manager_->countdown_started());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  factory_reset_manager_->OnMediaButtonReport(report);
  EXPECT_TRUE(factory_reset_manager_->countdown_started());

  // Factory reset should cancel if the button is released.
  report.reset = false;
  factory_reset_manager_->OnMediaButtonReport(report);
  EXPECT_FALSE(factory_reset_manager_->countdown_started());

  RunLoopFor(kCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetButtonHeldAndTrigger) {
  EXPECT_FALSE(factory_reset_manager_->countdown_started());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;

  factory_reset_manager_->OnMediaButtonReport(report);
  EXPECT_TRUE(factory_reset_manager_->countdown_started());

  RunLoopFor(kCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_TRUE(triggered());
}

TEST_F(FactoryResetManagerTest, FactoryResetStateNotifierCancelCallback) {
  EXPECT_TRUE(watcher_->state().IsEmpty());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  factory_reset_manager_->OnMediaButtonReport(report);

  // The reset deadline should be set 10 seconds from now.
  const zx_time_t deadline = Now().get() + kCountdownDuration.get();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_->state().has_reset_deadline());
  EXPECT_EQ(deadline, watcher_->state().reset_deadline());
  EXPECT_TRUE(watcher_->state().has_counting_down());
  EXPECT_TRUE(watcher_->state().counting_down());

  // Factory reset should cancel if the button is released.
  report.reset = false;
  factory_reset_manager_->OnMediaButtonReport(report);

  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_->state().has_reset_deadline());
  EXPECT_TRUE(watcher_->state().has_counting_down());
  EXPECT_FALSE(watcher_->state().counting_down());

  // No changes after the countdown duration.
  RunLoopFor(kCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_FALSE(watcher_->state().has_reset_deadline());
  EXPECT_TRUE(watcher_->state().has_counting_down());
  EXPECT_FALSE(watcher_->state().counting_down());
}

TEST_F(FactoryResetManagerTest, FactoryResetStateNotifierTriggerCallback) {
  EXPECT_TRUE(watcher_->state().IsEmpty());

  fuchsia::ui::input::MediaButtonsReport report;
  report.reset = true;
  factory_reset_manager_->OnMediaButtonReport(report);

  // The reset deadline should be set 10 seconds from now.
  const zx_time_t deadline = Now().get() + kCountdownDuration.get();
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_->state().has_reset_deadline());
  EXPECT_EQ(deadline, watcher_->state().reset_deadline());
  EXPECT_TRUE(watcher_->state().has_counting_down());
  EXPECT_TRUE(watcher_->state().counting_down());

  // The deadline should not be changed if the factory reset was triggered.
  RunLoopFor(kCountdownDuration);
  RunLoopUntilIdle();
  EXPECT_TRUE(watcher_->state().has_reset_deadline());
  EXPECT_EQ(deadline, watcher_->state().reset_deadline());
  EXPECT_TRUE(watcher_->state().has_counting_down());
  EXPECT_TRUE(watcher_->state().counting_down());
}

}  // namespace testing
}  // namespace root_presenter
