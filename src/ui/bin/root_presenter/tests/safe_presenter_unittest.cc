// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/safe_presenter.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/session.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_scenic.h"
#include "src/ui/bin/root_presenter/tests/fakes/fake_session.h"

namespace root_presenter {
namespace testing {

class SafePresenterTest : public gtest::TestLoopFixture {
 public:
  SafePresenterTest() {}

  void SetUp() override {
    // Create Scenic, Session and SafePresenter.
    fuchsia::ui::scenic::SessionPtr session_ptr;
    fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener_handle;
    fidl::InterfaceRequest<fuchsia::ui::scenic::SessionListener> listener_request =
        listener_handle.NewRequest();
    fake_scenic_.CreateSession(session_ptr.NewRequest(), std::move(listener_handle));
    fake_session_ = fake_scenic_.fakeSession();
    session_ =
        std::make_unique<scenic::Session>(std::move(session_ptr), std::move(listener_request));
    safe_presenter_ = std::make_unique<SafePresenter>(session_.get());
  }

  std::unique_ptr<scenic::Session> session_;
  FakeSession* fake_session_ = nullptr;  // Owned by fake_scenic_.
  FakeScenic fake_scenic_;
  std::unique_ptr<SafePresenter> safe_presenter_;
};

TEST_F(SafePresenterTest, SinglePresent) {
  bool callback_fired = false;

  ASSERT_EQ(fake_session_->PresentsCalled(), 0);
  safe_presenter_->QueuePresent([&callback_fired] { callback_fired = true; });

  RunLoopUntilIdle();
  ASSERT_EQ(fake_session_->PresentsCalled(), 1);
  ASSERT_TRUE(callback_fired);
}

TEST_F(SafePresenterTest, MultiplePresents) {
  constexpr int NUM_PRESENTS = 3;

  std::array<bool, NUM_PRESENTS> callback_fired_array = {};

  ASSERT_EQ(fake_session_->PresentsCalled(), 0);
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    safe_presenter_->QueuePresent([&callback_fired_array, i] { callback_fired_array[i] = true; });
  }

  RunLoopUntilIdle();
  ASSERT_TRUE(fake_session_->PresentWasCalled());

  std::array<bool, NUM_PRESENTS> expected_callback_fired_array = {};
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    expected_callback_fired_array[i] = true;
  }
  EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
}

TEST_F(SafePresenterTest, OverbudgetPresents) {
  constexpr int NUM_PRESENTS = 100;

  std::array<bool, NUM_PRESENTS> callback_fired_array = {};

  ASSERT_EQ(fake_session_->PresentsCalled(), 0);
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    safe_presenter_->QueuePresent([&callback_fired_array, i] { callback_fired_array[i] = true; });
  }

  RunLoopUntilIdle();
  ASSERT_TRUE(fake_session_->PresentWasCalled());

  std::array<bool, NUM_PRESENTS> expected_callback_fired_array = {};
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    expected_callback_fired_array[i] = true;
  }
  EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
}

TEST_F(SafePresenterTest, CallbacksExecuteInOrder) {
  constexpr int NUM_PRESENTS = 10;

  std::array<int, NUM_PRESENTS> callback_fired_array = {};

  ASSERT_EQ(fake_session_->PresentsCalled(), 0);

  // This turns an array of [0, 0, ... 0] to [0, 1, 2, ... n] if and only if we execute in ascending
  // order.
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    safe_presenter_->QueuePresent([&callback_fired_array, i] {
      if (i > 0)
        callback_fired_array[i] = callback_fired_array[i - 1] + 1;
    });
  }

  RunLoopUntilIdle();
  ASSERT_TRUE(fake_session_->PresentWasCalled());

  std::array<int, NUM_PRESENTS> expected_callback_fired_array = {};
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    expected_callback_fired_array[i] = i;
  }

  EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
}

TEST_F(SafePresenterTest, MultipleBurstsOfPresents) {
  constexpr int NUM_PRESENTS_PER_BURST = 10;
  constexpr int NUM_BURSTS = 3;
  ASSERT_EQ(fake_session_->PresentsCalled(), 0);

  for (int i = 0; i < NUM_BURSTS; ++i) {
    std::array<bool, NUM_PRESENTS_PER_BURST> callback_fired_array = {};

    for (int i = 0; i < NUM_PRESENTS_PER_BURST; ++i) {
      safe_presenter_->QueuePresent([&callback_fired_array, i] { callback_fired_array[i] = true; });
    }

    RunLoopUntilIdle();

    std::array<bool, NUM_PRESENTS_PER_BURST> expected_callback_fired_array = {};
    for (int i = 0; i < NUM_PRESENTS_PER_BURST; ++i) {
      expected_callback_fired_array[i] = true;
    }
    EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
  }

  ASSERT_TRUE(fake_session_->PresentWasCalled());
}

}  // namespace testing
}  // namespace root_presenter
