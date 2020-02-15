// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include <variant>

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/scenic/session.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace test {

class MockGfxSession : public gfx::Session {
 public:
  MockGfxSession(scheduling::SessionId session_id = 1) : Session(session_id, {}){};

  void DispatchCommand(fuchsia::ui::scenic::Command command, scheduling::PresentId) override {
    ++num_commands_dispatched_;
  };

  int num_commands_dispatched_ = 0;
};

class MockFrameScheduler : public scheduling::FrameScheduler {
 public:
  MockFrameScheduler() = default;

  // |FrameScheduler|
  void SetFrameRenderer(fxl::WeakPtr<scheduling::FrameRenderer> frame_renderer) override {}
  // |FrameScheduler|
  void AddSessionUpdater(fxl::WeakPtr<scheduling::SessionUpdater> session_updater) override {}
  // |FrameScheduler|
  void SetRenderContinuously(bool render_continuously) override {}

  // |FrameScheduler|
  scheduling::PresentId RegisterPresent(
      scheduling::SessionId session_id,
      std::variant<scheduling::OnPresentedCallback, scheduling::Present2Info> present_information,
      std::vector<zx::event> release_fences, scheduling::PresentId present_id = 0) override {
    if (auto present1_callback =
            std::get_if<scheduling::OnPresentedCallback>(&present_information)) {
      present1_callbacks_.emplace_back(std::move(*present1_callback));
    } else if (auto present2_info = std::get_if<scheduling::Present2Info>(&present_information)) {
      last_present2_info_.emplace(std::move(*present2_info));
    }
    return 0;
  }

  // |FrameScheduler|
  void SetOnUpdateFailedCallbackForSession(
      scheduling::SessionId session,
      OnSessionUpdateFailedCallback update_failed_callback) override {}

  // |FrameScheduler|
  void ScheduleUpdateForSession(zx::time requested_presentation_time,
                                scheduling::SchedulingIdPair id_pair) override {
    ++schedule_called_count_;
  }

  // |FrameScheduler|
  void GetFuturePresentationInfos(
      zx::duration requested_prediction_span,
      FrameScheduler::GetFuturePresentationInfosCallback presentation_infos_callback) override {}

  // |FrameScheduler|
  void SetOnFramePresentedCallbackForSession(
      scheduling::SessionId session,
      scheduling::OnFramePresentedCallback frame_presented_callback) override {
    on_frame_presented_callback_ = std::move(frame_presented_callback);
  }

  // |FrameScheduler|
  void RemoveSession(SessionId session_id) override {}

  std::vector<scheduling::OnPresentedCallback> present1_callbacks_;
  scheduling::OnFramePresentedCallback on_frame_presented_callback_;
  std::optional<scheduling::Present2Info> last_present2_info_;

  int64_t schedule_called_count_ = 0;
};

class ScenicSessionTest : public ::gtest::TestLoopFixture {
 public:
  ScenicSessionTest() = default;
  ~ScenicSessionTest() override = default;

  void InitializeSession(Session& session) {
    std::array<CommandDispatcherUniquePtr, System::TypeId::kMaxSystems> dispatchers;
    dispatchers[System::TypeId::kGfx] = CommandDispatcherUniquePtr(dispatcher_.get(), [](auto) {});
    session.SetCommandDispatchers(std::move(dispatchers));
    session.SetFrameScheduler(scheduler_);
  }

  std::unique_ptr<MockGfxSession> dispatcher_;
  std::shared_ptr<MockFrameScheduler> scheduler_;

 protected:
  void SetUp() override {
    dispatcher_ = std::make_unique<MockGfxSession>();
    scheduler_ = std::make_shared<MockFrameScheduler>();
  }

  void TearDown() override {
    scheduler_.reset();
    dispatcher_.reset();
  }

  class TestSessionListener : public fuchsia::ui::scenic::SessionListener {
   public:
    TestSessionListener() = default;
    // |fuchsia::ui::scenic::SessionListener|
    void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
      events_.reserve(events_.size() + events.size());
      std::move(events.begin(), events.end(), std::inserter(events_, events_.end()));
    }

    void OnScenicError(std::string error) override {}

    std::vector<fuchsia::ui::scenic::Event> events_;
  };
};

TEST_F(ScenicSessionTest, EventReporterFiltersViewDetachedAndAttachedEvents) {
  TestSessionListener test_session_listener;
  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener(&test_session_listener);
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener_handle;
  session_listener.Bind(session_listener_handle.NewRequest());

  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               std::move(session_listener_handle),
                               /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Check single Attach event.
  const uint32_t kViewId1 = 12;
  fuchsia::ui::gfx::Event attached_event_1;
  attached_event_1.set_view_attached_to_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(attached_event_1));
  RunLoopUntilIdle();
  EXPECT_EQ(test_session_listener.events_.size(), 1u);
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene,
            test_session_listener.events_[0].gfx().Which());

  // Check single Attach event.
  fuchsia::ui::gfx::Event attached_event_2;
  attached_event_2.set_view_attached_to_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(attached_event_2));
  fuchsia::ui::gfx::Event detached_event_1;
  detached_event_1.set_view_detached_from_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(detached_event_1));
  RunLoopUntilIdle();
  EXPECT_EQ(test_session_listener.events_.size(), 1u);

  // Check Detach-Attach pair.
  fuchsia::ui::gfx::Event detached_event_2;
  detached_event_2.set_view_detached_from_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(detached_event_2));
  fuchsia::ui::gfx::Event attached_event_3;
  attached_event_3.set_view_attached_to_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(attached_event_3));
  fuchsia::ui::input::InputEvent input_event;
  session.event_reporter()->EnqueueEvent(std::move(input_event));
  RunLoopUntilIdle();
  EXPECT_EQ(test_session_listener.events_.size(), 1u);

  // Check Detach-Attach pair with different view ids.
  fuchsia::ui::gfx::Event detached_event_3;
  detached_event_3.set_view_detached_from_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(detached_event_3));
  const uint32_t kViewId2 = 23;
  fuchsia::ui::gfx::Event attached_event_4;
  attached_event_4.set_view_attached_to_scene({.view_id = kViewId2});
  session.event_reporter()->EnqueueEvent(std::move(attached_event_4));
  RunLoopUntilIdle();
  EXPECT_EQ(test_session_listener.events_.size(), 3u);
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene,
            test_session_listener.events_[1].gfx().Which());
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene,
            test_session_listener.events_[2].gfx().Which());

  // Check Detach-Attach-Detach sequence.
  fuchsia::ui::gfx::Event detached_event_4;
  detached_event_4.set_view_detached_from_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(detached_event_4));
  fuchsia::ui::gfx::Event attached_event_5;
  attached_event_5.set_view_attached_to_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(attached_event_5));
  fuchsia::ui::gfx::Event detached_event_5;
  detached_event_5.set_view_detached_from_scene({.view_id = kViewId1});
  session.event_reporter()->EnqueueEvent(std::move(detached_event_5));
  RunLoopUntilIdle();
  EXPECT_EQ(test_session_listener.events_.size(), 4u);
  EXPECT_EQ(fuchsia::ui::gfx::Event::Tag::kViewDetachedFromScene,
            test_session_listener.events_[1].gfx().Which());
}

TEST_F(ScenicSessionTest, ScheduleUpdateOutOfOrder_ShouldGiveErrorAndDestroySession) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  session.Present(1, {}, {}, [](auto) {});
  session.Present(0, {}, {}, [](auto) {});
  EXPECT_EQ(last_error,
            "scenic_impl::Session: Present called with out-of-order presentation time. "
            "requested presentation time=0, last scheduled presentation time=1.");
  EXPECT_TRUE(session_destroyed);
}

TEST_F(ScenicSessionTest, SchedulePresent2UpdatesOutOfOrder_ShouldGiveErrorAndDestroySession) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  session.Present2(utils::CreatePresent2Args(0, {}, {}, 0), [](auto) {});
  EXPECT_EQ(last_error,
            "scenic_impl::Session: Present called with out-of-order presentation time. "
            "requested presentation time=0, last scheduled presentation time=1.");
  EXPECT_TRUE(session_destroyed);
}

TEST_F(ScenicSessionTest, ScheduleUpdateInOrder_ShouldBeFine) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  session.Present(1, {}, {}, [](auto) {});
  session.Present(1, {}, {}, [](auto) {});
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);
}

TEST_F(ScenicSessionTest, SchedulePresent2UpdateInOrder_ShouldBeFine) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);
}

TEST_F(ScenicSessionTest, PresentMoreThanAllowed_ShouldGiveError) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  // Max out the maximum allotted presents in flight.
  for (int i = 0; i < scheduling::FrameScheduler::kMaxPresentsInFlight; i++) {
    session.Present(/*presentation_time*/ 0, /*acquire_fences=*/{}, /*release_fences=*/{},
                    [](auto) {});
  }

  // Exceed limit.
  session.Present(/*presentation_time*/ 0, /*acquire_fences=*/{}, /*release_fences=*/{},
                  [](auto) {});
  EXPECT_EQ(last_error, "Present() called with no more present calls allowed.");
  EXPECT_FALSE(session_destroyed);
}

TEST_F(ScenicSessionTest, Present2MoreThanAllowed_ShouldGiveErrorAndDestroySession) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  // Max out the maximum allotted presents in flight.
  for (int i = 0; i < scheduling::FrameScheduler::kMaxPresentsInFlight; i++) {
    session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  }

  // Exceed limit.
  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  EXPECT_EQ(last_error,
            "Present2() called with no more present calls allowed. Terminating session.");
  EXPECT_TRUE(session_destroyed);
}

TEST_F(ScenicSessionTest, TriggeringPresentCallback_ShouldIncrementPresentsAllowed) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  // Max out the maximum allotted presents in flight.
  for (int i = 0; i < scheduling::FrameScheduler::kMaxPresentsInFlight; i++) {
    session.Present(0, {}, {}, [](auto) {});
  }
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);

  // Presents in flight should be incremented by size of callback.
  ASSERT_EQ(scheduler_->present1_callbacks_.size(),
            static_cast<uint64_t>(scheduling::FrameScheduler::kMaxPresentsInFlight));
  scheduler_->present1_callbacks_.front()({});

  // Should be able to present one more time.
  session.Present(0, {}, {}, [](auto) {});
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);

  // Next one should exceed the limit.
  session.Present(0, {}, {}, [](auto) {});
  EXPECT_EQ(last_error, "Present() called with no more present calls allowed.");
  EXPECT_FALSE(session_destroyed);
}

TEST_F(ScenicSessionTest, TriggeringPresent2Callback_ShouldIncrementPresentsAllowed) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  bool session_destroyed = false;
  scenic_impl::Session session(
      /*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
      /*destroy_session_function*/ [&session_destroyed] { session_destroyed = true; });
  InitializeSession(session);
  std::string last_error;
  session.set_error_callback([&last_error](std::string error) { last_error = error; });

  // Max out the maximum allotted presents in flight.
  for (int i = 0; i < scheduling::FrameScheduler::kMaxPresentsInFlight; i++) {
    session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  }
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);

  // Presents in flight should be incremented by size of callback.
  ASSERT_TRUE(scheduler_->on_frame_presented_callback_);
  fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info = {};
  frame_presented_info.presentation_infos.push_back({});
  scheduler_->on_frame_presented_callback_(std::move(frame_presented_info));

  // Should be able to present one more time.
  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  EXPECT_TRUE(last_error.empty());
  EXPECT_FALSE(session_destroyed);

  // Next one should exceed the limit.
  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});
  EXPECT_EQ(last_error,
            "Present2() called with no more present calls allowed. Terminating session.");
  EXPECT_TRUE(session_destroyed);
}

TEST_F(ScenicSessionTest, Present2Update_ShouldHaveReasonablePresentReceivedTime) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(), /*listener=*/nullptr,
                               /*destroy_session_function*/ [] {});
  InitializeSession(session);

  auto present_time = Now();
  session.Present2(utils::CreatePresent2Args(1, {}, {}, 0), [](auto) {});

  ASSERT_TRUE(scheduler_->last_present2_info_);
  auto present_received_info = scheduler_->last_present2_info_->TakePresentReceivedInfo();
  ASSERT_TRUE(present_received_info.has_present_received_time());
  EXPECT_GE(present_received_info.present_received_time(), present_time.get());
  EXPECT_LT(present_received_info.present_received_time(), (present_time + zx::msec(1)).get());
}

// Tests creating a session, and calling Present with two acquire fences. The call should not be
// propagated further until all fences have been signalled.
TEST_F(ScenicSessionTest, AcquireFences_WithPresent1) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               /*listener=*/nullptr, /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Create acquire fences.
  std::vector<zx::event> acquire_fences = scenic_impl::gfx::test::CreateEventArray(2);
  zx::event acquire_fence1 = scenic_impl::gfx::test::CopyEvent(acquire_fences.at(0));
  zx::event acquire_fence2 = scenic_impl::gfx::test::CopyEvent(acquire_fences.at(1));

  // Call Present with the acquire fences.
  session.Present(0u, std::move(acquire_fences), {}, [](auto) {});
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence1.signal(0u, ZX_EVENT_SIGNALED);
  // Nothing should have happened.
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 1);
}

// Tests creating a session, and calling Present with two acquire fences. The call should not be
// propagated further until all fences have been signalled.
TEST_F(ScenicSessionTest, AcquireFences_WithPresent2) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               /*listener=*/nullptr, /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Create acquire fences.
  std::vector<zx::event> acquire_fences = scenic_impl::gfx::test::CreateEventArray(2);
  zx::event acquire_fence1 = scenic_impl::gfx::test::CopyEvent(acquire_fences.at(0));
  zx::event acquire_fence2 = scenic_impl::gfx::test::CopyEvent(acquire_fences.at(1));

  // Call Present with the acquire fences.
  session.Present2(utils::CreatePresent2Args(0, std::move(acquire_fences), {}, 0), [](auto) {});
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence1.signal(0u, ZX_EVENT_SIGNALED);
  // Nothing should have happened.
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 1);
}

// Tests creating a session, and calling Present twice with different sets of acquire fences.
TEST_F(ScenicSessionTest, AcquireFences_WithMultiplePresent1) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               /*listener=*/nullptr, /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Create acquire fences.
  std::vector<zx::event> acquire_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence1 = scenic_impl::gfx::test::CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence2 = scenic_impl::gfx::test::CopyEvent(acquire_fences2.at(0));

  // Present twice with an acquire fence each.
  session.Present(0u, std::move(acquire_fences1), {}, [](auto) {});
  session.Present(0u, std::move(acquire_fences2), {}, [](auto) {});

  // Call with no fences.
  session.Present(0u, {}, {}, [](auto) {});

  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // Only the first call should have been made.
  EXPECT_EQ(scheduler_->schedule_called_count_, 1);

  acquire_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // Both the remaining calls should have been made.
  EXPECT_EQ(scheduler_->schedule_called_count_, 3);
}

// Tests creating a session, and calling Present twice with different sets of acquire fences.
TEST_F(ScenicSessionTest, AcquireFences_WithMultiplePresent2) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               /*listener=*/nullptr, /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Create acquire fences.
  std::vector<zx::event> acquire_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence1 = scenic_impl::gfx::test::CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence2 = scenic_impl::gfx::test::CopyEvent(acquire_fences2.at(0));

  // Present twice with an acquire fence each.
  session.Present2(utils::CreatePresent2Args(0, std::move(acquire_fences1), {}, 0), [](auto) {});
  session.Present2(utils::CreatePresent2Args(0, std::move(acquire_fences2), {}, 0), [](auto) {});

  // Call with no fences.
  session.Present2(utils::CreatePresent2Args(0, {}, {}, 0), [](auto) {});

  RunLoopUntilIdle();
  EXPECT_EQ(scheduler_->schedule_called_count_, 0);

  acquire_fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // Only the first call should have been made.
  EXPECT_EQ(scheduler_->schedule_called_count_, 1);

  acquire_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // Both the remaining calls should have been made.
  EXPECT_EQ(scheduler_->schedule_called_count_, 3);
}

// This tests checks that commands enqueued for separate Present aren't dispatched until (at least)
// the previous Present call has been made.
TEST_F(ScenicSessionTest, CommandForDifferentPresents_MustBeEnqueuedSeparately) {
  fuchsia::ui::scenic::SessionPtr session_ptr;
  scenic_impl::Session session(/*id=*/1, session_ptr.NewRequest(),
                               /*listener=*/nullptr, /*destroy_session_function*/ [] {});
  InitializeSession(session);

  // Enqueue a command.
  fuchsia::ui::scenic::Command cmd1;
  cmd1.set_gfx(::fuchsia::ui::gfx::Command());
  std::vector<fuchsia::ui::scenic::Command> cmds1;
  cmds1.push_back(std::move(cmd1));
  session.Enqueue(std::move(cmds1));

  // Create acquire fences.
  std::vector<zx::event> acquire_fences1 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence1 = scenic_impl::gfx::test::CopyEvent(acquire_fences1.at(0));
  std::vector<zx::event> acquire_fences2 = scenic_impl::gfx::test::CreateEventArray(1);
  zx::event acquire_fence2 = scenic_impl::gfx::test::CopyEvent(acquire_fences2.at(0));

  session.Present2(utils::CreatePresent2Args(0, std::move(acquire_fences1), {}, 0), [](auto) {});

  // Enqueue a command for the second batch.
  fuchsia::ui::scenic::Command cmd2;
  cmd2.set_gfx(::fuchsia::ui::gfx::Command());
  std::vector<fuchsia::ui::scenic::Command> cmds2;
  cmds2.push_back(std::move(cmd2));
  session.Enqueue(std::move(cmds2));

  session.Present2(utils::CreatePresent2Args(0, std::move(acquire_fences2), {}, 0), [](auto) {});

  // The first command could have been safely dispatched.
  RunLoopUntilIdle();
  EXPECT_LE(dispatcher_->num_commands_dispatched_, 1);

  acquire_fence1.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // The first command must have been dispatched before the call Present2 finished, and the second
  // could have been safely dispatched afterwards.
  EXPECT_GE(dispatcher_->num_commands_dispatched_, 1);
  EXPECT_LE(dispatcher_->num_commands_dispatched_, 2);

  acquire_fence2.signal(0u, ZX_EVENT_SIGNALED);
  RunLoopUntilIdle();
  // After present, the dispatch must have happened.
  EXPECT_EQ(dispatcher_->num_commands_dispatched_, 2);
}

}  // namespace test
}  // namespace scenic_impl
