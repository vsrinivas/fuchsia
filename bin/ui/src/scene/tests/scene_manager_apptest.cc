// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/src/scene/resources/nodes/entity_node.h"
#include "apps/mozart/src/scene/tests/scene_manager_test.h"
#include "gtest/gtest.h"
#include "lib/ftl/synchronization/waitable_event.h"

namespace mozart {
namespace scene {
namespace test {

TEST_F(SceneManagerTest, CreateAndDestroySession) {
  mozart2::SessionPtr session;
  EXPECT_EQ(0U, manager_impl_->GetSessionCount());
  manager_->CreateSession(session.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 1);
  session = nullptr;
  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 0);
}

TEST_F(SceneManagerTest, MultipleSessionConnections1) {
  // Tests creating a session, making a second connection to the same session,
  // and verifying that one connection continues to work after closing the other
  // one.  We do this for two pairs of sessions in parallel, to test that it
  // works both when the original connection is closed first, and also when the
  // second connection is closed first.
  EXPECT_EQ(0U, manager_impl_->GetSessionCount());

  mozart2::SessionPtr sess1a;
  mozart2::SessionPtr sess2a;
  manager_->CreateSession(sess1a.NewRequest(), nullptr);
  manager_->CreateSession(sess2a.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 2);
  auto handler1 =
      static_cast<SessionHandlerForTest*>(manager_impl_->FindSession(1));
  auto handler2 =
      static_cast<SessionHandlerForTest*>(manager_impl_->FindSession(2));

  mozart2::SessionPtr sess1b;
  sess1a->Connect(sess1b.NewRequest(), nullptr);
  mozart2::SessionPtr sess2b;
  sess2a->Connect(sess2b.NewRequest(), nullptr);
  RUN_MESSAGE_LOOP_WHILE(handler1->connect_count() != 1);
  RUN_MESSAGE_LOOP_WHILE(handler2->connect_count() != 1);
  EXPECT_EQ(0U, handler1->enqueue_count());
  EXPECT_EQ(0U, handler2->enqueue_count());

  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewCreateCircleOp(1, 50.f));
    ops.push_back(NewCreateCircleOp(2, 25.f));
    sess1a->Enqueue(std::move(ops));
  }
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewCreateCircleOp(1, 50.f));
    ops.push_back(NewCreateCircleOp(2, 25.f));
    sess2a->Enqueue(std::move(ops));
  }
  RUN_MESSAGE_LOOP_WHILE(handler1->enqueue_count() != 1);
  RUN_MESSAGE_LOOP_WHILE(handler2->enqueue_count() != 1);

  // Disconnect one connection, and send Present() on the other.
  sess1a = nullptr;
  sess2b = nullptr;
  sess1b->Present(0u, ::fidl::Array<mx::event>::New(0),
                  ::fidl::Array<mx::event>::New(0),
                  [](mozart2::PresentationInfoPtr info) {});
  sess2a->Present(0u, ::fidl::Array<mx::event>::New(0),
                  ::fidl::Array<mx::event>::New(0),
                  [](mozart2::PresentationInfoPtr info) {});
  RUN_MESSAGE_LOOP_WHILE(handler1->present_count() != 1);
  RUN_MESSAGE_LOOP_WHILE(handler2->present_count() != 1);

  sess1b = nullptr;
  sess2a = nullptr;
  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 0);
}

TEST_F(SceneManagerTest, MultipleSessionConnections2) {
  // Creates multiple connections to a single session, then tests that all
  // are closed when one of them presents an illegal op.
  EXPECT_EQ(0U, manager_impl_->GetSessionCount());

  mozart2::SessionPtr sess1a;
  manager_->CreateSession(sess1a.NewRequest(), nullptr);
  mozart2::SessionPtr sess1b;
  sess1a->Connect(sess1b.NewRequest(), nullptr);
  mozart2::SessionPtr sess1c;
  sess1a->Connect(sess1c.NewRequest(), nullptr);
  mozart2::SessionPtr sess1d;
  sess1c->Connect(sess1d.NewRequest(), nullptr);

  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 1);
  auto handler =
      static_cast<SessionHandlerForTest*>(manager_impl_->FindSession(1));

  // Enqueue ops via sess1a.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewCreateCircleOp(1, 50.f));
    ops.push_back(NewCreateCircleOp(2, 25.f));
    sess1a->Enqueue(std::move(ops));
  }
  // Enqueue ops via sess1b.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewCreateEntityNodeOp(3));
    sess1b->Enqueue(std::move(ops));
  }
  // Enqueue ops via sess1c.
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewCreateShapeNodeOp(4));
    ops.push_back(NewCreateShapeNodeOp(5));
    sess1c->Enqueue(std::move(ops));
  }

  // Once these are known to be enqueued, it is safe to refer to the session
  // ids that were created via the different connections.
  RUN_MESSAGE_LOOP_WHILE(handler->enqueue_count() != 3);
  {
    ::fidl::Array<mozart2::OpPtr> ops;
    ops.push_back(NewAddChildOp(3, 4));
    ops.push_back(NewAddChildOp(3, 5));
    ops.push_back(NewSetShapeOp(4, 1));
    ops.push_back(NewSetShapeOp(5, 2));
    sess1d->Enqueue(std::move(ops));
    sess1d->Present(0u, ::fidl::Array<mx::event>::New(0),
                    ::fidl::Array<mx::event>::New(0),
                    [](mozart2::PresentationInfoPtr info) {});
  }
  RUN_MESSAGE_LOOP_WHILE(handler->present_count() != 1);
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
    ops.push_back(NewCreateEntityNodeOp(3));  // already exists!
    sess1b->Enqueue(std::move(ops));
    sess1b->Present(0u, ::fidl::Array<mx::event>::New(0),
                    ::fidl::Array<mx::event>::New(0),
                    [](mozart2::PresentationInfoPtr info) {});
  }

  RUN_MESSAGE_LOOP_WHILE(manager_impl_->GetSessionCount() != 0);

  // TODO: Test SessionListener.  One good way to do this would be to attach a
  // listener when creating connection 1c, and verifying that the error message
  // triggered above is received (and therefore was sent properly as part of
  // Session tear-down).
}

}  // namespace test
}  // namespace scene
}  // namespace mozart
