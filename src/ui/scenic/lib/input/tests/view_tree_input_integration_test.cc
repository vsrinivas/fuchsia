// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/ui/scenic/lib/input/input_system.h"
#include "src/ui/scenic/lib/input/tests/util.h"

namespace lib_ui_input_tests {
namespace {

class ViewTreeInputIntegrationTest : public InputSystemTest {
 public:
  ViewTreeInputIntegrationTest() = default;

 protected:
  uint32_t test_display_width_px() const override { return 5; }
  uint32_t test_display_height_px() const override { return 5; }
};

// Scene layout:
//  root
//   |
//  view1
//   |
//  view2
TEST_F(ViewTreeInputIntegrationTest, IsInputSuppressed_ShouldReturnFalseByDefault) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();

  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder holder_1(session, std::move(vh1), "1");
  scene->AddChild(holder_1);
  RequestToPresent(session);

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "2");
  client_1.view()->AddChild(holder_2);
  RequestToPresent(client_1.session());

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
  RequestToPresent(client_2.session());

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));
}

// Scene layout:
//  root
//   |
//  view1
//   |
//  view2 - hit testing suppressed
TEST_F(ViewTreeInputIntegrationTest, IsInputSuppressed_ForSuppressedNode_ShouldReturnTrue) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();

  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder holder_1(session, std::move(vh1), "1");
  scene->AddChild(holder_1);
  RequestToPresent(session);

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "2");
  holder_2.SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kSuppress);
  client_1.view()->AddChild(holder_2);
  RequestToPresent(client_1.session());

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
  RequestToPresent(client_2.session());

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_TRUE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));
}

// Scene layout:
//  root
//   |
//  view1 - hit testing suppressed
//   |
//  view2
TEST_F(ViewTreeInputIntegrationTest,
       IsInputSuppressed_ForDescendantOfSuppressedNode_ShouldReturnTrue) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();

  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder holder_1(session, std::move(vh1), "1");
  holder_1.SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kSuppress);
  scene->AddChild(holder_1);
  RequestToPresent(session);

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "2");
  client_1.view()->AddChild(holder_2);
  RequestToPresent(client_1.session());

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
  RequestToPresent(client_2.session());

  EXPECT_TRUE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_TRUE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));
}

// Scene layout:
//  root
//   |
//  view1
//   |
//  view2  - hit testing suppressed, then not suppressed
TEST_F(ViewTreeInputIntegrationTest, IsInputSuppressed_AfterRemovingSuppression_ShouldReturnFalse) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();

  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder holder_1(session, std::move(vh1), "1");
  scene->AddChild(holder_1);
  RequestToPresent(session);

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "2");
  holder_2.SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kSuppress);
  client_1.view()->AddChild(holder_2);
  RequestToPresent(client_1.session());

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
  RequestToPresent(client_2.session());

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_TRUE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));

  // Remove hit testing suppression.
  holder_2.SetHitTestBehavior(fuchsia::ui::gfx::HitTestBehavior::kDefault);
  RequestToPresent(client_1.session());

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));
}

// Scene layout:
//  root
//   x <- disconnected
//  view1
//   |
//  view2
TEST_F(ViewTreeInputIntegrationTest, IsInputSuppressed_AfterDisconnectFromScene_ShouldReturnFalse) {
  auto [v1, vh1] = scenic::ViewTokenPair::New();
  auto [v2, vh2] = scenic::ViewTokenPair::New();
  auto [root_session, root_resources] = CreateScene();

  scenic::Session* const session = root_session.session();
  scenic::Scene* const scene = &root_resources.scene;

  scenic::ViewHolder holder_1(session, std::move(vh1), "1");
  scene->AddChild(holder_1);
  RequestToPresent(session);

  SessionWrapper client_1 = CreateClient("view_1", std::move(v1));
  scenic::ViewHolder holder_2(client_1.session(), std::move(vh2), "2");
  client_1.view()->AddChild(holder_2);
  RequestToPresent(client_1.session());

  SessionWrapper client_2 = CreateClient("view_2", std::move(v2));
  RequestToPresent(client_2.session());

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));

  // Now disconnect.
  scene->DetachChildren();
  RequestToPresent(session);

  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_1.ViewKoid()));
  EXPECT_FALSE(engine()->scene_graph()->view_tree().IsInputSuppressed(client_2.ViewKoid()));
}

}  // namespace
}  // namespace lib_ui_input_tests
