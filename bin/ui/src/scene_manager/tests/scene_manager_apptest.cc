// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/ftl/synchronization/waitable_event.h"

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene_manager/tests/mocks.h"
#include "apps/mozart/src/scene_manager/tests/scene_manager_test.h"
#include "apps/mozart/src/scene_manager/tests/util.h"

namespace scene_manager {
namespace test {

TEST_F(SceneManagerTest, CreateAndDestroySession) {
  mozart2::SessionPtr session;
  EXPECT_EQ(0U, manager_impl_->session_context()->GetSessionCount());
  manager_->CreateSession(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         1);
  session = nullptr;
  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         0);
}

TEST_F(SceneManagerTest, MultipleSessionConnections1) {
  // Tests creating a session, making a second connection to the same session,
  // and verifying that one connection continues to work after closing the other
  // one.  We do this for two pairs of sessions in parallel, to test that it
  // works both when the original connection is closed first, and also when the
  // second connection is closed first.
  EXPECT_EQ(0U, manager_impl_->session_context()->GetSessionCount());

  mozart2::SessionPtr sess1a;
  mozart2::SessionPtr sess2a;
  manager_->CreateSession(sess1a.NewRequest(), nullptr);
  manager_->CreateSession(sess2a.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         2);
  auto handler1 = static_cast<SessionHandlerForTest*>(
      manager_impl_->session_context()->FindSession(1));
  auto handler2 = static_cast<SessionHandlerForTest*>(
      manager_impl_->session_context()->FindSession(2));

  mozart2::SessionPtr sess1b;
  sess1a->Connect(sess1b.NewRequest(), nullptr);
  mozart2::SessionPtr sess2b;
  sess2a->Connect(sess2b.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(handler1->connect_count() == 1);
  RUN_MESSAGE_LOOP_UNTIL(handler2->connect_count() == 1);
  EXPECT_EQ(0U, handler1->enqueue_count());
  EXPECT_EQ(0U, handler2->enqueue_count());

  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateCircleOp(1, 50.f));
    ops.push_back(mozart::NewCreateCircleOp(2, 25.f));
    sess1a->Enqueue(std::move(ops));
  }
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateCircleOp(1, 50.f));
    ops.push_back(mozart::NewCreateCircleOp(2, 25.f));
    sess2a->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_UNTIL(handler1->enqueue_count() == 1);
  RUN_MESSAGE_LOOP_UNTIL(handler2->enqueue_count() == 1);

  // Disconnect one connection, and send Present() on the other.
  sess1a = nullptr;
  sess2b = nullptr;
  sess1b->Present(0u, ::fidl::Array<mx::event>::New(0),
                  ::fidl::Array<mx::event>::New(0),
                  [](mozart2::PresentationInfoPtr info) {});
  sess2a->Present(0u, ::fidl::Array<mx::event>::New(0),
                  ::fidl::Array<mx::event>::New(0),
                  [](mozart2::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler1->present_count() == 1);
  RUN_MESSAGE_LOOP_UNTIL(handler2->present_count() == 1);

  sess1b = nullptr;
  sess2a = nullptr;
  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         0);
}

TEST_F(SceneManagerTest, MultipleSessionConnections2) {
  // Creates multiple connections to a single session, then tests that all
  // are closed when one of them presents an illegal op.
  EXPECT_EQ(0U, manager_impl_->session_context()->GetSessionCount());

  mozart2::SessionPtr sess1a;
  manager_->CreateSession(sess1a.NewRequest(), nullptr);
  mozart2::SessionPtr sess1b;
  sess1a->Connect(sess1b.NewRequest(), nullptr);
  mozart2::SessionPtr sess1c;
  sess1a->Connect(sess1c.NewRequest(), nullptr);
  mozart2::SessionPtr sess1d;
  sess1c->Connect(sess1d.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         1);
  auto handler = static_cast<SessionHandlerForTest*>(
      manager_impl_->session_context()->FindSession(1));

  // Enqueue ops via sess1a.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateCircleOp(1, 50.f));
    ops.push_back(mozart::NewCreateCircleOp(2, 25.f));
    sess1a->Enqueue(std::move(ops));
  }
  // Enqueue ops via sess1b.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateEntityNodeOp(3));
    sess1b->Enqueue(std::move(ops));
  }
  // Enqueue ops via sess1c.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateShapeNodeOp(4));
    ops.push_back(mozart::NewCreateShapeNodeOp(5));
    sess1c->Enqueue(std::move(ops));
  }

  // Once these are known to be enqueued, it is safe to refer to the session
  // ids that were created via the different connections.
  RUN_MESSAGE_LOOP_UNTIL(handler->enqueue_count() == 3);
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewAddChildOp(3, 4));
    ops.push_back(mozart::NewAddChildOp(3, 5));
    ops.push_back(mozart::NewSetShapeOp(4, 1));
    ops.push_back(mozart::NewSetShapeOp(5, 2));
    sess1d->Enqueue(std::move(ops));
    sess1d->Present(0u, ::fidl::Array<mx::event>::New(0),
                    ::fidl::Array<mx::event>::New(0),
                    [](mozart2::PresentationInfoPtr info) {});
  }
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 1);
  {
    auto resources = handler->session()->resources();
    auto entity = resources->FindResource<EntityNode>(3);
    EXPECT_EQ(2U, entity->children().size());
  }

  // Do something illegal and verify that the session is torn down.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    FTL_LOG(INFO)
        << "The subsequent 'resource already exists' error is expected";
    ops.push_back(mozart::NewCreateEntityNodeOp(3));  // already exists!
    sess1b->Enqueue(std::move(ops));
    sess1b->Present(0u, ::fidl::Array<mx::event>::New(0),
                    ::fidl::Array<mx::event>::New(0),
                    [](mozart2::PresentationInfoPtr info) {});
  }

  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         0);

  // TODO: Test SessionListener.  One good way to do this would be to attach a
  // listener when creating connection 1c, and verifying that the error message
  // triggered above is received (and therefore was sent properly as part of
  // Session tear-down).
}

bool IsFenceSignalled(const mx::event& fence) {
  mx_signals_t signals = 0u;
  mx_status_t status = fence.wait_one(kFenceSignalledOrClosed, 0, &signals);
  FTL_DCHECK(status == MX_OK || status == MX_ERR_TIMED_OUT);
  return signals & kFenceSignalledOrClosed;
}

TEST_F(SceneManagerTest, ReleaseFences) {
  // Tests creating a session, and calling Present with two release fences.
  // The release fences should be signalled after a subsequent Present.
  EXPECT_EQ(0u, manager_impl_->session_context()->GetSessionCount());

  mozart2::SessionPtr session_host;
  manager_->CreateSession(session_host.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         1);
  EXPECT_EQ(1u, manager_impl_->session_context()->GetSessionCount());
  auto handler = static_cast<SessionHandlerForTest*>(
      manager_impl_->session_context()->FindSession(1));

  mozart2::SessionPtr session;
  session_host->Connect(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(handler->connect_count() == 1);
  EXPECT_EQ(0u, handler->enqueue_count());

  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateCircleOp(1, 50.f));
    ops.push_back(mozart::NewCreateCircleOp(2, 25.f));
    session->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_UNTIL(handler->enqueue_count() == 1);
  EXPECT_EQ(1u, handler->enqueue_count());

  // Create release fences
  mx::event release_fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &release_fence1));
  mx::event release_fence2;
  ASSERT_EQ(MX_OK, mx::event::create(0, &release_fence2));

  ::fidl::Array<mx::event> release_fences;
  release_fences.push_back(CopyEvent(release_fence1));
  release_fences.push_back(CopyEvent(release_fence2));

  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));

  // Call Present with release fences.
  session->Present(0u, ::fidl::Array<mx::event>::New(0),
                   std::move(release_fences),
                   [](mozart2::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 1);
  EXPECT_EQ(1u, handler->present_count());

  EXPECT_FALSE(IsFenceSignalled(release_fence1));
  EXPECT_FALSE(IsFenceSignalled(release_fence2));
  // Call Present again with no release fences.
  session->Present(0u, ::fidl::Array<mx::event>::New(0),
                   ::fidl::Array<mx::event>::New(0),
                   [](mozart2::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 2);
  EXPECT_EQ(2u, handler->present_count());

  EXPECT_TRUE(IsFenceSignalled(release_fence1));
  EXPECT_TRUE(IsFenceSignalled(release_fence2));
}

TEST_F(SceneManagerTest, AcquireAndReleaseFences) {
  // Tests creating a session, and calling Present with an acquire and a release
  // fence. The release fences should be signalled only after a subsequent
  // Present, and not until the acquire fence has been signalled.
  EXPECT_EQ(0u, manager_impl_->session_context()->GetSessionCount());

  mozart2::SessionPtr session_host;
  manager_->CreateSession(session_host.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_UNTIL(manager_impl_->session_context()->GetSessionCount() ==
                         1);
  EXPECT_EQ(1u, manager_impl_->session_context()->GetSessionCount());
  auto handler = static_cast<SessionHandlerForTest*>(
      manager_impl_->session_context()->FindSession(1));

  mozart2::SessionPtr session;
  session_host->Connect(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_UNTIL(handler->connect_count() == 1);
  EXPECT_EQ(0u, handler->enqueue_count());

  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(mozart::NewCreateCircleOp(1, 50.f));
    ops.push_back(mozart::NewCreateCircleOp(2, 25.f));
    session->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_UNTIL(handler->enqueue_count() == 1);
  EXPECT_EQ(1u, handler->enqueue_count());

  // Create acquire and release fences
  mx::event acquire_fence;
  ASSERT_EQ(MX_OK, mx::event::create(0, &acquire_fence));
  mx::event release_fence;
  ASSERT_EQ(MX_OK, mx::event::create(0, &release_fence));

  ::fidl::Array<mx::event> acquire_fences;
  acquire_fences.push_back(CopyEvent(acquire_fence));

  ::fidl::Array<mx::event> release_fences;
  release_fences.push_back(CopyEvent(release_fence));

  // Call Present with both the acquire and release fences.
  session->Present(0u, std::move(acquire_fences), std::move(release_fences),
                   [](mozart2::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_UNTIL(handler->present_count() == 1);
  EXPECT_EQ(1u, handler->present_count());

  EXPECT_FALSE(IsFenceSignalled(release_fence));

  // Call Present again with no fences.
  session->Present(0u, ::fidl::Array<mx::event>::New(0),
                   ::fidl::Array<mx::event>::New(0),
                   [](mozart2::PresentationInfoPtr info) {});
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
