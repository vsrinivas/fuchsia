// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/flib/release_fence_signaller.h"
#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/gfx/tests/gfx_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

namespace scenic_impl {
namespace gfx {
namespace test {

static fuchsia::ui::scenic::Present2Args CreatePresent2Args(
    zx_time_t requested_presentation_time, std::vector<zx::event> acquire_fences,
    std::vector<zx::event> release_fences, zx_duration_t requested_prediction_span) {
  fuchsia::ui::scenic::Present2Args args;
  args.set_requested_presentation_time(requested_presentation_time);
  args.set_acquire_fences(std::move(acquire_fences));
  args.set_release_fences(std::move(release_fences));
  args.set_requested_prediction_span(requested_prediction_span);

  return args;
}

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
  session->Present2(CreatePresent2Args(1, CreateEventArray(1), CreateEventArray(1), 0),
                    [](auto) {});
  // Briefly flush the message loop. Expect that the session is not destroyed.
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  // Present with the same presentation time.
  session->Present2(CreatePresent2Args(1, CreateEventArray(1), CreateEventArray(1), 0),
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

bool IsFenceSignalled(const zx::event& fence) {
  zx_signals_t signals = 0u;
  zx_status_t status = fence.wait_one(escher::kFenceSignalled, zx::time(), &signals);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
  return signals & escher::kFenceSignalled;
}

TEST_F(GfxSystemTest, ReleaseFences) {
  // Tests creating a session, and calling Present with two release fences.
  // The release fences should be signalled after a subsequent Present.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  {
    std::vector<fuchsia::ui::scenic::Command> commands;
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(1, 50.f)));
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(2, 25.f)));

    session->Enqueue(std::move(commands));
  }
  RunLoopUntilIdle();
  // Create release fences
  std::vector<zx::event> release_fences = CreateEventArray(2);
  zx::event release_fence1 = CopyEvent(release_fences.at(0));
  zx::event release_fence2 = CopyEvent(release_fences.at(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present with release fences.
  session->Present(0u, std::vector<zx::event>(), std::move(release_fences),
                   [](fuchsia::images::PresentationInfo info) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present again with no release fences.
  session->Present(0u, std::vector<zx::event>(), std::vector<zx::event>(),
                   [](fuchsia::images::PresentationInfo info) {});
  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(IsFenceSignalled(release_fence1));
  EXPECT_TRUE(IsFenceSignalled(release_fence2));
}

TEST_F(GfxSystemTest, ReleaseFences_WithPresent2) {
  // Tests creating a session, and calling Present with two release fences.
  // The release fences should be signalled after a subsequent Present.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  {
    std::vector<fuchsia::ui::scenic::Command> commands;
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(1, 50.f)));
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(2, 25.f)));

    session->Enqueue(std::move(commands));
  }
  RunLoopUntilIdle();
  // Create release fences
  std::vector<zx::event> release_fences = CreateEventArray(2);
  zx::event release_fence1 = CopyEvent(release_fences.at(0));
  zx::event release_fence2 = CopyEvent(release_fences.at(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present with release fences.
  session->Present2(CreatePresent2Args(0u, std::vector<zx::event>(), std::move(release_fences), 0u),
                    [](auto) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present again with no release fences.
  session->Present2(CreatePresent2Args(0u, std::vector<zx::event>(), std::vector<zx::event>(), 0u),
                    [](auto) {});
  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(IsFenceSignalled(release_fence1));
  EXPECT_TRUE(IsFenceSignalled(release_fence2));
}

TEST_F(GfxSystemTest, AcquireAndReleaseFences) {
  // Tests creating a session, and calling Present with an acquire and a release
  // fence. The release fences should be signalled only after a subsequent
  // Present, and not until the acquire fence has been signalled.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  {
    std::vector<fuchsia::ui::scenic::Command> commands;
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(1, 50.f)));
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(2, 25.f)));

    session->Enqueue(std::move(commands));
  }
  RunLoopUntilIdle();
  // Create acquire and release fences
  zx::event acquire_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &acquire_fence));
  zx::event release_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &release_fence));
  std::vector<zx::event> acquire_fences;
  acquire_fences.push_back(CopyEvent(acquire_fence));
  std::vector<zx::event> release_fences;
  release_fences.push_back(CopyEvent(release_fence));
  // Call Present with both the acquire and release fences.
  session->Present(0u, std::move(acquire_fences), std::move(release_fences),
                   [](fuchsia::images::PresentationInfo info) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence));
  // Call Present again with no fences.
  session->Present(0u, std::vector<zx::event>(), std::vector<zx::event>(),
                   [](fuchsia::images::PresentationInfo info) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence));
  // Now signal the acquire fence.
  acquire_fence.signal(0u, escher::kFenceSignalled);
  // Now expect that the first frame was presented, and its release fence was
  // signalled.
  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(IsFenceSignalled(release_fence));
}

TEST_F(GfxSystemTest, AcquireAndReleaseFences_WithPresent2) {
  // Tests creating a session, and calling Present2 with an acquire and a release
  // fence. The release fences should be signalled only after a subsequent
  // Present2, and not until the acquire fence has been signalled.
  fuchsia::ui::scenic::SessionPtr session;
  EXPECT_EQ(0U, scenic()->num_sessions());
  scenic()->CreateSession(session.NewRequest(), nullptr);
  RunLoopUntilIdle();
  EXPECT_EQ(1U, scenic()->num_sessions());
  {
    std::vector<fuchsia::ui::scenic::Command> commands;
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(1, 50.f)));
    commands.push_back(scenic::NewCommand(scenic::NewCreateCircleCmd(2, 25.f)));

    session->Enqueue(std::move(commands));
  }
  RunLoopUntilIdle();
  // Create acquire and release fences
  zx::event acquire_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &acquire_fence));
  zx::event release_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &release_fence));
  std::vector<zx::event> acquire_fences;
  acquire_fences.push_back(CopyEvent(acquire_fence));
  std::vector<zx::event> release_fences;
  release_fences.push_back(CopyEvent(release_fence));
  // Call Present with both the acquire and release fences.
  session->Present2(
      CreatePresent2Args(0u, std::move(acquire_fences), std::move(release_fences), 0u),
      [](auto) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence));
  // Call Present again with no fences.
  session->Present2(CreatePresent2Args(0u, std::vector<zx::event>(), std::vector<zx::event>(), 0u),
                    [](auto) {});
  RunLoopFor(zx::sec(1));
  EXPECT_FALSE(IsFenceSignalled(release_fence));
  // Now signal the acquire fence.
  acquire_fence.signal(0u, escher::kFenceSignalled);
  // Now expect that the first frame was presented, and its release fence was
  // signalled.
  RunLoopFor(zx::sec(1));
  EXPECT_TRUE(IsFenceSignalled(release_fence));
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
        EXPECT_EQ(future_times.remaining_presents_in_flight_allowed, Session::kMaxPresentsInFlight);
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
    session->Present2(CreatePresent2Args(0, {}, {}, 0), [](auto) {});
  }
  EXPECT_TRUE(RunLoopUntilIdle());
  EXPECT_EQ(1U, scenic()->num_sessions());

  // Execute one more Present2, which should kill the session.
  session->Present2(CreatePresent2Args(0, {}, {}, 0), [](auto) {});
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
      CreatePresent2Args(0, {}, {}, 0),
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
