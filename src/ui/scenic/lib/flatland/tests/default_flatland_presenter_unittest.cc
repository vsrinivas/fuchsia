// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/default_flatland_presenter.h"

#include <lib/syslog/cpp/macros.h>

#include <chrono>
#include <cstddef>
#include <mutex>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "lib/gtest/real_loop_fixture.h"
#include "src/ui/scenic/lib/scheduling/tests/mocks/frame_scheduler_mocks.h"

using flatland::DefaultFlatlandPresenter;

namespace flatland {
namespace test {

namespace {

// TODO(fxbug.dev/56879): consolidate the following 3 helper functions when splitting escher into
// multiple libraries.

zx::event CreateEvent() {
  zx::event event;
  FX_CHECK(zx::event::create(0, &event) == ZX_OK);
  return event;
}

std::vector<zx::event> CreateEventArray(size_t n) {
  std::vector<zx::event> events;
  for (size_t i = 0; i < n; i++) {
    events.push_back(CreateEvent());
  }
  return events;
}

zx::event CopyEvent(const zx::event& event) {
  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK) {
    FX_LOGS(ERROR) << "Copying zx::event failed.";
  }
  return event_copy;
}

bool IsEventSignaled(const zx::event& fence, zx_signals_t signal) {
  zx_signals_t pending = 0u;
  fence.wait_one(signal, zx::time(), &pending);
  return (pending & signal) != 0u;
}

// This harness uses a real loop instead of a test loop since the multithreading test requires the
// tasks posted by the DefaultFlatlandPresenter to run without blocking the worker threads.
class DefaultFlatlandPresenterTest : public gtest::RealLoopFixture {
 public:
  DefaultFlatlandPresenter CreateDefaultFlatlandPresenter() {
    return DefaultFlatlandPresenter(dispatcher());
  }
};

}  // namespace

TEST_F(DefaultFlatlandPresenterTest, NoFrameSchedulerSet) {
  auto presenter = CreateDefaultFlatlandPresenter();

  const scheduling::SessionId kSessionId = 1;
  const scheduling::PresentId kPresentId = 2;

  // Neither function should crash, even though there is no FrameScheduler.
  scheduling::PresentId present_id = presenter.RegisterPresent(kSessionId, /*release_fences=*/{});
  RunLoopUntilIdle();

  EXPECT_EQ(present_id, scheduling::kInvalidPresentId);

  presenter.ScheduleUpdateForSession(zx::time(123), {kSessionId, kPresentId});
  RunLoopUntilIdle();
}

TEST_F(DefaultFlatlandPresenterTest, FrameSchedulerExpired) {
  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  auto presenter = CreateDefaultFlatlandPresenter();
  presenter.SetFrameScheduler(frame_scheduler);

  frame_scheduler.reset();

  const scheduling::SessionId kSessionId = 1;
  const scheduling::PresentId kPresentId = 2;

  // Neither function should crash, even though the FrameScheduler has expired.
  scheduling::PresentId present_id = presenter.RegisterPresent(kSessionId, /*release_fences=*/{});
  RunLoopUntilIdle();

  EXPECT_EQ(present_id, scheduling::kInvalidPresentId);

  presenter.ScheduleUpdateForSession(zx::time(123), {kSessionId, kPresentId});
  RunLoopUntilIdle();
}

TEST_F(DefaultFlatlandPresenterTest, RegisterPresentForwardsToFrameScheduler) {
  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  // Capture the relevant arguments of the RegisterPresent() call.
  scheduling::SessionId last_session_id = scheduling::kInvalidSessionId;
  std::vector<zx::event> last_release_fences;

  frame_scheduler->set_register_present_callback(
      [&last_session_id, &last_release_fences](
          scheduling::SessionId session_id,
          std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info>
              present_information,
          std::vector<zx::event> release_fences, scheduling::PresentId present_id) {
        last_session_id = session_id;
        last_release_fences = std::move(release_fences);
      });

  auto presenter = CreateDefaultFlatlandPresenter();
  presenter.SetFrameScheduler(frame_scheduler);

  const scheduling::SessionId kSessionId = 2;

  // Create release fences to verify they are plumbed through to the FrameScheduler.
  std::vector<zx::event> release_fences = CreateEventArray(2);
  zx::event release1_dup = CopyEvent(release_fences[0]);
  zx::event release2_dup = CopyEvent(release_fences[1]);

  scheduling::PresentId present_id =
      presenter.RegisterPresent(kSessionId, std::move(release_fences));
  RunLoopUntilIdle();

  EXPECT_NE(present_id, scheduling::kInvalidPresentId);
  EXPECT_EQ(last_session_id, kSessionId);
  EXPECT_EQ(last_release_fences.size(), 2ul);

  // Signal the fences one at a time and verify they are in the correct order.
  EXPECT_FALSE(IsEventSignaled(last_release_fences[0], ZX_EVENT_SIGNALED));
  EXPECT_FALSE(IsEventSignaled(last_release_fences[1], ZX_EVENT_SIGNALED));

  release1_dup.signal(0, ZX_EVENT_SIGNALED);
  EXPECT_TRUE(IsEventSignaled(last_release_fences[0], ZX_EVENT_SIGNALED));
  EXPECT_FALSE(IsEventSignaled(last_release_fences[1], ZX_EVENT_SIGNALED));

  release2_dup.signal(0, ZX_EVENT_SIGNALED);
  EXPECT_TRUE(IsEventSignaled(last_release_fences[0], ZX_EVENT_SIGNALED));
  EXPECT_TRUE(IsEventSignaled(last_release_fences[1], ZX_EVENT_SIGNALED));
}

TEST_F(DefaultFlatlandPresenterTest, ScheduleUpdateForSessionForwardsToFrameScheduler) {
  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  // Capture the relevant arguments of the ScheduleUpdateForSession() call.
  zx::time last_presentation_time = zx::time(0);
  auto last_id_pair = scheduling::SchedulingIdPair({
      .session_id = scheduling::kInvalidSessionId,
      .present_id = scheduling::kInvalidPresentId,
  });

  frame_scheduler->set_schedule_update_for_session_callback(
      [&last_presentation_time, &last_id_pair](zx::time presentation_time,
                                               scheduling::SchedulingIdPair id_pair) {
        last_presentation_time = presentation_time;
        last_id_pair = id_pair;
      });

  auto presenter = CreateDefaultFlatlandPresenter();
  presenter.SetFrameScheduler(frame_scheduler);

  const auto kIdPair = scheduling::SchedulingIdPair({
      .session_id = 1,
      .present_id = 2,
  });
  const zx::time kPresentationTime = zx::time(123);

  presenter.ScheduleUpdateForSession(kPresentationTime, kIdPair);
  RunLoopUntilIdle();

  EXPECT_EQ(last_presentation_time, kPresentationTime);
  EXPECT_EQ(last_id_pair, kIdPair);
}

TEST_F(DefaultFlatlandPresenterTest, MultithreadedAccess) {
  auto frame_scheduler = std::make_shared<scheduling::test::MockFrameScheduler>();

  // The FrameScheduler will be accessed in a thread-safe way, so the test instead collects the
  // registered presents and scheduled updates and ensures each function was called the correct
  // number of times with the correct set of ID pairs.
  std::set<scheduling::SchedulingIdPair> registered_presents;
  std::set<scheduling::SchedulingIdPair> scheduled_updates;

  // Also use a generic function call counter to test mutual exclusion between function calls.
  size_t function_count = 0;

  frame_scheduler->set_register_present_callback(
      [&registered_presents, &function_count](
          scheduling::SessionId session_id,
          std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info>
              present_information,
          std::vector<zx::event> release_fences, scheduling::PresentId present_id) {
        registered_presents.insert({session_id, present_id});
        ++function_count;
      });

  frame_scheduler->set_schedule_update_for_session_callback(
      [&scheduled_updates, &function_count](zx::time presentation_time,
                                            scheduling::SchedulingIdPair id_pair) {
        scheduled_updates.insert(id_pair);

        ++function_count;
      });

  auto presenter = CreateDefaultFlatlandPresenter();
  presenter.SetFrameScheduler(frame_scheduler);

  // Start 10 "sessions", each of which registers 100 presents and schedules 100 updates.
  static constexpr uint64_t kNumSessions = 10;
  static constexpr uint64_t kNumPresents = 100;

  std::vector<std::thread> threads;

  std::mutex mutex;
  std::unordered_set<scheduling::PresentId> present_ids;

  const auto now = std::chrono::steady_clock::now();
  const auto then = now + std::chrono::milliseconds(50);

  for (uint64_t session_id = 1; session_id <= kNumSessions; ++session_id) {
    std::thread thread([then, session_id, &mutex, &present_ids, &presenter]() {
      // Because each of the threads do a fixed amount of work, they may trigger in succession
      // without overlap. In order to bombard the system with concurrent requests, stall thread
      // execution until a specific time.
      std::this_thread::sleep_until(then);
      std::vector<scheduling::PresentId> presents;
      for (uint64_t i = 0; i < kNumPresents; ++i) {
        // RegisterPresent() is one of the two functions being tested.
        auto present_id = presenter.RegisterPresent(session_id, /*release_fences=*/{});
        presents.push_back(present_id);

        // Yield with some randomness so the threads get jumbled up a bit.
        if (std::rand() % 4 == 0) {
          std::this_thread::yield();
        }

        // ScheduleUpdateForSession() is the other function being tested.
        presenter.ScheduleUpdateForSession(zx::time(0), {session_id, present_id});

        // Yield with some randomness so the threads get jumbled up a bit.
        if (std::rand() % 4 == 0) {
          std::this_thread::yield();
        }
      }

      // Acquire the test mutex and insert all IDs for later evaluation.
      {
        std::scoped_lock lock(mutex);
        for (const auto& present_id : presents) {
          present_ids.insert(present_id);
        }
      }
    });

    threads.push_back(std::move(thread));
  }

  // Make calls directly to the FrameScheduler to mimic GFX, which runs on the "main" looper, which
  // in this test is just this thread.
  static constexpr scheduling::SessionId kGfxSessionId = kNumSessions + 1;
  static constexpr uint64_t kNumGfxPresents = 500;

  std::vector<scheduling::PresentId> gfx_presents;

  std::this_thread::sleep_until(then);

  for (uint64_t i = 0; i < kNumGfxPresents; ++i) {
    // RegisterPresent() is one of the two functions being tested.
    auto present_id = scheduling::GetNextPresentId();
    present_id = frame_scheduler->RegisterPresent(
        kGfxSessionId, /*present_information=*/[](auto...) {}, /*release_fences=*/{}, present_id);
    gfx_presents.push_back(present_id);

    // ScheduleUpdateForSession() is the other function being tested.
    frame_scheduler->ScheduleUpdateForSession(zx::time(0), {kGfxSessionId, present_id});
  }

  {
    std::scoped_lock lock(mutex);
    for (const auto& present_id : gfx_presents) {
      present_ids.insert(present_id);
    }
  }

  for (auto& t : threads) {
    t.join();
  }

  // Flush all the tasks posted by the presenter.
  RunLoopUntilIdle();

  // Verify that all the PresentIds are unique and that the sets from both mock functions have the
  // same number of ID pairs.
  static constexpr uint64_t kTotalNumPresents = (kNumSessions * kNumPresents) + kNumGfxPresents;

  EXPECT_EQ(present_ids.size(), kTotalNumPresents);
  EXPECT_EQ(registered_presents.size(), kTotalNumPresents);
  EXPECT_EQ(scheduled_updates.size(), kTotalNumPresents);

  // Verify that the correct total number of function calls were made.
  EXPECT_EQ(function_count, kTotalNumPresents * 2ul);

  // Verify that the sets from both mock functions are identical.
  EXPECT_THAT(registered_presents, ::testing::ElementsAreArray(scheduled_updates));
}

}  // namespace test
}  // namespace flatland
