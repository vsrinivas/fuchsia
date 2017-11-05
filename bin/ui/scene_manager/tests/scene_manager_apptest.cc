// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/fxl/synchronization/waitable_event.h"

#include "garnet/bin/ui/scene_manager/resources/nodes/entity_node.h"
#include "garnet/bin/ui/scene_manager/tests/mocks.h"
#include "garnet/bin/ui/scene_manager/tests/scene_manager_test.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"
#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/tests/test_with_message_loop.h"

namespace scene_manager {
namespace test {

TEST_F(SceneManagerTest, CreateAndDestroySession) {
  scenic::SessionPtr session;
  EXPECT_EQ(0U, engine()->GetSessionCount());
  manager_->CreateSession(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);
  session = nullptr;
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 0);
}

TEST_F(SceneManagerTest, ScheduleUpdateOutOfOrder) {
  // Create a session.
  scenic::SessionPtr session;
  EXPECT_EQ(0U, engine()->GetSessionCount());
  manager_->CreateSession(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);

  // Present on the session with presentation_time = 1.
  scenic::Session::PresentCallback callback = [](auto) {};
  session->Present(1, CreateEventArray(1), CreateEventArray(1), callback);

  // Briefly pump the message loop. Expect that the session is not destroyed.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);

  // Present with an older presentation time.
  session->Present(0, CreateEventArray(1), CreateEventArray(1), callback);

  // Expect the session is destroyed.
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 0);
}

TEST_F(SceneManagerTest, ScheduleUpdateInOrder) {
  // Create a session.
  scenic::SessionPtr session;
  EXPECT_EQ(0U, engine()->GetSessionCount());
  manager_->CreateSession(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);

  // Present on the session with presentation_time = 1.
  scenic::Session::PresentCallback callback = [](auto) {};
  session->Present(1, CreateEventArray(1), CreateEventArray(1), callback);

  // Briefly pump the message loop. Expect that the session is not destroyed.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);

  // Present with the same presentation time.
  session->Present(1, CreateEventArray(1), CreateEventArray(1), callback);

  // Briefly pump the message loop. Expect that the session is not destroyed.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);
}

bool IsFenceSignalled(const zx::event& fence) {
  zx_signals_t signals = 0u;
  zx_status_t status = fence.wait_one(kFenceSignalled, 0, &signals);
  FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
  return signals & kFenceSignalled;
}

TEST_F(SceneManagerTest, ReleaseFences) {
  // Tests creating a session, and calling Present with two release fences.
  // The release fences should be signalled after a subsequent Present.
  EXPECT_EQ(0u, engine()->GetSessionCount());

  scenic::SessionPtr session;
  manager_->CreateSession(session.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);
  EXPECT_EQ(1u, engine()->GetSessionCount());
  auto handler = static_cast<SessionHandlerForTest*>(engine()->FindSession(1));

  {
    ::fidl::Array<scenic::OpPtr> ops;
    ops.push_back(scenic_lib::NewCreateCircleOp(1, 50.f));
    ops.push_back(scenic_lib::NewCreateCircleOp(2, 25.f));
    session->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_UNTIL(handler->enqueue_count() == 1);
  EXPECT_EQ(1u, handler->enqueue_count());

  // Create release fences
  ::fidl::Array<zx::event> release_fences = CreateEventArray(2);
  zx::event release_fence1 = CopyEvent(release_fences[0]);
  zx::event release_fence2 = CopyEvent(release_fences[1]);

  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));

  // Call Present with release fences.
  session->Present(0u, ::fidl::Array<zx::event>::New(0),
                   std::move(release_fences),
                   [](scenic::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 1);
  EXPECT_EQ(1u, handler->present_count());

  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present again with no release fences.
  session->Present(0u, ::fidl::Array<zx::event>::New(0),
                   ::fidl::Array<zx::event>::New(0),
                   [](scenic::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 2);
  EXPECT_EQ(2u, handler->present_count());

  RUN_MESSAGE_LOOP_UNTIL(IsFenceSignalled(release_fence1));
  EXPECT_TRUE(IsFenceSignalled(release_fence2));
}

TEST_F(SceneManagerTest, AcquireAndReleaseFences) {
  // Tests creating a session, and calling Present with an acquire and a release
  // fence. The release fences should be signalled only after a subsequent
  // Present, and not until the acquire fence has been signalled.
  EXPECT_EQ(0u, engine()->GetSessionCount());

  scenic::SessionPtr session;
  manager_->CreateSession(session.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(engine()->GetSessionCount() == 1);
  EXPECT_EQ(1u, engine()->GetSessionCount());
  auto handler = static_cast<SessionHandlerForTest*>(engine()->FindSession(1));

  {
    ::fidl::Array<scenic::OpPtr> ops;
    ops.push_back(scenic_lib::NewCreateCircleOp(1, 50.f));
    ops.push_back(scenic_lib::NewCreateCircleOp(2, 25.f));
    session->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_UNTIL(handler->enqueue_count() == 1);
  EXPECT_EQ(1u, handler->enqueue_count());

  // Create acquire and release fences
  zx::event acquire_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &acquire_fence));
  zx::event release_fence;
  ASSERT_EQ(ZX_OK, zx::event::create(0, &release_fence));

  ::fidl::Array<zx::event> acquire_fences;
  acquire_fences.push_back(CopyEvent(acquire_fence));

  ::fidl::Array<zx::event> release_fences;
  release_fences.push_back(CopyEvent(release_fence));

  // Call Present with both the acquire and release fences.
  session->Present(0u, std::move(acquire_fences), std::move(release_fences),
                   [](scenic::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 1);
  EXPECT_EQ(1u, handler->present_count());

  EXPECT_FALSE(IsFenceSignalled(release_fence));

  // Call Present again with no fences.
  session->Present(0u, ::fidl::Array<zx::event>::New(0),
                   ::fidl::Array<zx::event>::New(0),
                   [](scenic::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 2);

  EXPECT_FALSE(IsFenceSignalled(release_fence));

  // Now signal the acquire fence.
  acquire_fence.signal(0u, kFenceSignalled);

  // Now expect that the first frame was presented, and its release fence was
  // signalled.
  RUN_MESSAGE_LOOP_UNTIL(IsFenceSignalled(release_fence));
}

}  // namespace test
}  // namespace scene_manager
