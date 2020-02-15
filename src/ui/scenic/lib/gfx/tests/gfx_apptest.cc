// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
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

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
