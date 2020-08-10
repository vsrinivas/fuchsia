// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/default.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/bin/root_presenter/safe_presenter.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace gfx {
namespace test {

TEST_F(GfxSystemTest, CreateAndDestroySession) {
  EXPECT_EQ(0U, scenic()->num_sessions());

  fuchsia::ui::scenic::SessionPtr session;

  EXPECT_EQ(0U, scenic()->num_sessions());

  scenic()->CreateSession(session.NewRequest(), nullptr);

  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  session = nullptr;
  RunLoopUntilIdle();
  EXPECT_EQ(0U, scenic()->num_sessions());
}

TEST_F(GfxSystemTest, ScheduleUpdateInOrder) {
  // Create a session.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present on the session with presentation_time = 1.
  session->Present(1, CreateEventArray(1), CreateEventArray(1), [](auto) {});
  // Briefly pump the message loop. Expect that the session is not destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present with the same presentation time.
  session->Present(1, CreateEventArray(1), CreateEventArray(1), [](auto) {});
  // Briefly pump the message loop. Expect that the session is not destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
}

TEST_F(GfxSystemTest, SchedulePresent2UpdateInOrder) {
  // Create a session.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present on the session with presentation_time = 1.
  session->Present2(utils::CreatePresent2Args(1, CreateEventArray(1), CreateEventArray(1), 0),
                    [](auto) {});
  // Briefly flush the message loop. Expect that the session is not destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present with the same presentation time.
  session->Present2(utils::CreatePresent2Args(1, CreateEventArray(1), CreateEventArray(1), 0),
                    [](auto) {});
  // Briefly flush the message loop. Expect that the session is not destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
}

TEST_F(GfxSystemTest, SchedulePresent2UpdateWithMissingFields) {
  // Create a session.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present on the session with presentation_time = 1.
  session->Present2({}, [](auto) {});
  // Briefly flush the message loop. Expect that the session is destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(0U, scenic()->num_sessions());
}

TEST_F(GfxSystemTest, RequestPresentationTimes) {
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Call RequestPresentationTimes() and expect the maximum amount of presents in flight since we
  // never called Present2().
  session->RequestPresentationTimes(
      0, [](fuchsia::scenic::scheduling::FuturePresentationTimes future_times) {
        EXPECT_EQ(future_times.remaining_presents_in_flight_allowed,
                  scheduling::FrameScheduler::kMaxPresentsInFlight);
      });

  EXPECT_TRUE(RunLoopUntilIdle());
}

TEST_F(GfxSystemTest, TooManyPresent2sInFlight_ShouldKillSession) {
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Max out our budget of Present2s.
  for (int i = 0; i < 5; i++) {
    session->Present2(utils::CreatePresent2Args(0, {}, {}, 0), [](auto) {});
  }
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Execute one more Present2, which should kill the session.
  session->Present2(utils::CreatePresent2Args(0, {}, {}, 0), [](auto) {});
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(0U, scenic()->num_sessions());
}

// Ensure Present2's immediate callback is functionally equivalent to RequestPresentationTimes'
// callback.
TEST_F(GfxSystemTest, RequestPresentationTimesResponse_ShouldMatchPresent2CallbackResponse) {
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());

  fuchsia::scenic::scheduling::FuturePresentationTimes present2_response = {};
  fuchsia::scenic::scheduling::FuturePresentationTimes rpt_response = {};

  session->Present2(
      utils::CreatePresent2Args(0, {}, {}, 0),
      [&present2_response](fuchsia::scenic::scheduling::FuturePresentationTimes future_times) {
        present2_response = std::move(future_times);
      });
  EXPECT_TRUE(RunLoopUntilIdle());

  session->RequestPresentationTimes(
      0, [&rpt_response](fuchsia::scenic::scheduling::FuturePresentationTimes future_times) {
        rpt_response = std::move(future_times);
      });
  EXPECT_TRUE(RunLoopUntilIdle());

  EXPECT_EQ(rpt_response.remaining_presents_in_flight_allowed,
            present2_response.remaining_presents_in_flight_allowed);
  EXPECT_EQ(rpt_response.future_presentations.size(),
            present2_response.future_presentations.size());

  for (size_t i = 0; i < rpt_response.future_presentations.size(); ++i) {
    auto rpt_elem = std::move(rpt_response.future_presentations[i]);
    auto present2_elem = std::move(present2_response.future_presentations[i]);

    EXPECT_EQ(rpt_elem.latch_point(), present2_elem.latch_point());
    EXPECT_EQ(rpt_elem.presentation_time(), present2_elem.presentation_time());
  }
}

TEST_F(GfxSystemTest, SafePresenter_OverbudgetPresents) {
  EXPECT_EQ(0U, scenic()->num_sessions());

  // Create Session
  auto session = std::make_unique<scenic::Session>(scenic(), nullptr);
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Create SafePresenter
  auto safe_presenter = std::make_unique<root_presenter::SafePresenter>(session.get());

  constexpr int NUM_PRESENTS = 100;

  std::array<bool, NUM_PRESENTS> callback_fired_array = {};

  for (int i = 0; i < NUM_PRESENTS; ++i) {
    safe_presenter->QueuePresent([&callback_fired_array, i] { callback_fired_array[i] = true; });
    RunLoopFor(zx::msec(3));
  }

  RunLoopFor(zx::sec(1));

  std::array<bool, NUM_PRESENTS> expected_callback_fired_array = {};
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    expected_callback_fired_array[i] = true;
  }
  EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
}

TEST_F(GfxSystemTest, SafePresenter_CallbacksExecuteInOrder) {
  EXPECT_EQ(0U, scenic()->num_sessions());

  // Create Session
  auto session = std::make_unique<scenic::Session>(scenic(), nullptr);
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Create SafePresenter
  auto safe_presenter = std::make_unique<root_presenter::SafePresenter>(session.get());

  constexpr int NUM_PRESENTS = 50;

  std::array<int, NUM_PRESENTS> callback_fired_array = {};

  // This turns an array of [0, 0, ... 0] to [0, 1, 2, ... n] if and only if we execute in ascending
  // order.
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    safe_presenter->QueuePresent([&callback_fired_array, i] {
      if (i > 0)
        callback_fired_array[i] = callback_fired_array[i - 1] + 1;
    });
    RunLoopFor(zx::msec(3));
  }

  RunLoopFor(zx::sec(1));

  std::array<int, NUM_PRESENTS> expected_callback_fired_array = {};
  for (int i = 0; i < NUM_PRESENTS; ++i) {
    expected_callback_fired_array[i] = i;
  }

  EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
}

TEST_F(GfxSystemTest, SafePresenter_MultipleBurstsOfPresents) {
  EXPECT_EQ(0U, scenic()->num_sessions());

  // Create Session
  auto session = std::make_unique<scenic::Session>(scenic(), nullptr);
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Create SafePresenter
  auto safe_presenter = std::make_unique<root_presenter::SafePresenter>(session.get());

  constexpr int NUM_PRESENTS_PER_BURST = 10;
  constexpr int NUM_BURSTS = 3;

  for (int i = 0; i < NUM_BURSTS; ++i) {
    std::array<bool, NUM_PRESENTS_PER_BURST> callback_fired_array = {};

    for (int i = 0; i < NUM_PRESENTS_PER_BURST; ++i) {
      safe_presenter->QueuePresent([&callback_fired_array, i] { callback_fired_array[i] = true; });
      RunLoopFor(zx::msec(3));
    }

    RunLoopFor(zx::sec(1));

    std::array<bool, NUM_PRESENTS_PER_BURST> expected_callback_fired_array = {};
    for (int i = 0; i < NUM_PRESENTS_PER_BURST; ++i) {
      expected_callback_fired_array[i] = true;
    }
    EXPECT_THAT(callback_fired_array, ::testing::ElementsAreArray(expected_callback_fired_array));
  }
}

// There is a tricky race condition where if there is an OnFramePresented event in between
// QueuePresent() and Present2 handled on the Scenic side, SafePresenter's tracking of how many
// times it can present can fall out of sync and lead to it going over budget.
TEST_F(GfxSystemTest, SafePresenter_OnFramePresentedRace) {
  // Create the session's test loop. Scenic relies on the default dispatcher so we set that here.
  async::TestLoop scenic_loop;
  async_set_default_dispatcher(scenic_loop.dispatcher());

  // Create Session
  auto session = std::make_unique<scenic::Session>(scenic(), scenic_loop.dispatcher());
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Create SafePresenter
  auto safe_presenter = std::make_unique<root_presenter::SafePresenter>(session.get());

  int count = 0;
  int expected_count = 0;

  // SafePresenter calls Present2 and Scenic receives it.
  safe_presenter->QueuePresent([&count] { ++count; });
  ++expected_count;
  scenic_loop.RunUntilIdle();

  // When we advance this loop, Scenic renders and reaches vsync, thereby firing the
  // OnFramePresented event.
  RunLoopFor(zx::sec(1));

  // Before SafePresenter receives the OnFramePresented event, it fires 4 more Present2s.
  // SafePresenter now knows it has 0 presents left.
  for (int i = 0; i < 4; ++i) {
    safe_presenter->QueuePresent([&count] { ++count; });
    ++expected_count;
  }

  // When we advance this loop, SafePresenter finally receives the OnFramePresented event.
  scenic_loop.RunUntilIdle();

  // At this point, SafePresenter thinks it has 5 Present2s left because of the OnFramePresented()
  // event that fired at the end of the last loop run, which did not take into account the 4
  // QueuePresent()s that happened simultaneously.

  // Scenic knows there is actually only 1 Present2 left, however. Let's enqueue 2 more Present2s to
  // cause the session to potentially get killed.
  for (int i = 0; i < 2; ++i) {
    safe_presenter->QueuePresent([&count] { ++count; });
    ++expected_count;
  }

  // If SafePresenter did not handle its bookkeeping correctly, it would crash on this next line.
  scenic_loop.RunUntilIdle();

  // Finish up to fire the callbacks.
  for (int i = 0; i < 2; ++i) {
    RunLoopFor(zx::sec(1));
    scenic_loop.RunUntilIdle();
  }

  EXPECT_EQ(1U, scenic()->num_sessions());
  EXPECT_EQ(count, expected_count);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
