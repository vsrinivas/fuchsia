// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/component/cpp/testing/realm_builder.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/tests/gfx_integration_tests/pixel_test.h"
#include "src/ui/scenic/tests/utils/scenic_realm_builder.h"

namespace integration_tests {

using RealmRoot = component_testing::RealmRoot;

class ViewEmbedderTest : public PixelTest {
 private:
  RealmRoot SetupRealm() {
    return ScenicRealmBuilder()
        .AddRealmProtocol(fuchsia::ui::scenic::Scenic::Name_)
        .AddRealmProtocol(fuchsia::ui::annotation::Registry::Name_)
        .Build();
  }
};

// Initialize two sessions and their associated views, and ensure that killing the embedded
// session triggers a ViewDisconnected event to the holding one.
TEST_F(ViewEmbedderTest, DeadBindingShouldKillSession) {
  // Initialize session 1.
  auto test_session = std::make_unique<RootSession>(scenic(), GetDisplayDimensions());
  test_session->session.set_error_handler([](auto) { FAIL() << "Session terminated."; });

  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;
  test_session->SetUpCamera().SetProjection(0);

  // Initialize session 2.
  auto unique_session2 = std::make_unique<scenic::Session>(scenic());
  auto session2 = unique_session2.get();
  session2->set_error_handler([this](zx_status_t status) {
    FX_LOGS(INFO) << "Session2 terminated.";
    QuitLoop();
  });

  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_token2, view_holder_token2] = scenic::ViewTokenPair::New();

  scenic::View view(session, std::move(view_token), "ClipView");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ClipViewHolder");

  // View 2 is embedded by view 1.
  scenic::View view2(session2, std::move(view_token2), "ClipView2");
  scenic::ViewHolder view_holder2(session, std::move(view_holder_token2), "ClipViewHolder2");

  scene->AddChild(view_holder);

  // Transform and embed view holder 2 in first view.
  scenic::EntityNode transform_node(session);
  transform_node.SetTranslation(display_width / 2, 0, 0);
  view.AddChild(transform_node);
  transform_node.AddChild(view_holder2);

  // Ensure that view2 connects to view1.
  bool view_connected_observed = false;
  bool view2_connected_observed = false;

  session->set_event_handler([&](std::vector<fuchsia::ui::scenic::Event> events) {
    for (const auto& event : events) {
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
          event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewConnected) {
        if (view_holder.id() == event.gfx().view_connected().view_holder_id) {
          view_connected_observed = true;
        } else if (view_holder2.id() == event.gfx().view_connected().view_holder_id) {
          view2_connected_observed = true;
        }
        return;
      }
    }
  });

  Present(session);
  Present(session2);

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&]() { return view_connected_observed && view2_connected_observed; }));

  // Crash Session2 by submitting an invalid release resource command.
  session2->AllocResourceId();
  session2->ReleaseResource(session2->next_resource_id() + 1);

  bool view_disconnected_observed = false;

  session->set_event_handler(
      [&view_disconnected_observed](std::vector<fuchsia::ui::scenic::Event> events) {
        for (const auto& event : events) {
          if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
              event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewDisconnected) {
            view_disconnected_observed = true;
            return;
          }
        }
        ASSERT_FALSE(true);
      });

  // Observe results.
  Present(session2);
  Present(session);

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&view_disconnected_observed]() { return view_disconnected_observed; }));
}

// When annotation View and annotation ViewHolder are created within the same
// frame (i.e. the same SessionUpdate() call), we need to ensure that they are
// created in the correct order.
//
// ViewTree update of annotation ViewHolder should be created earlier before
// annotation View, since the update of latter one refers to the ViewHolder
// in ViewTree. Otherwise it will trigger a DCHECK() within ViewTree and lead
// to a bad tree state.
TEST_F(ViewEmbedderTest, AnnotationViewAndViewHolderInSingleFrame) {
  auto test_session = std::make_unique<RootSession>(scenic(), GetDisplayDimensions());
  test_session->session.set_error_handler([](auto) { FAIL() << "Session terminated."; });

  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;

  // Initialize second session
  auto unique_session_view = std::make_unique<scenic::Session>(scenic());
  auto unique_session_annotation = std::make_unique<scenic::Session>(scenic());

  auto session_view = unique_session_view.get();
  auto session_annotation = unique_session_annotation.get();

  session_view->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Session terminated.";
    FAIL();
    QuitLoop();
  });
  session_annotation->set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "Annotation Session terminated.";
    FAIL();
    QuitLoop();
  });

  test_session->SetUpCamera().SetProjection(0);
  scenic::EntityNode entity_node(session);
  entity_node.SetTranslation(0, 0, 0);
  test_session->scene.AddChild(entity_node);

  // Create two sets of view/view-holder token pairs.
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
  auto [view_control_ref, view_ref] = scenic::ViewRefPair::New();
  auto [view_token_annotation, view_holder_token_annotation] = scenic::ViewTokenPair::New();

  fuchsia::ui::views::ViewRef view_ref_create;
  view_ref.Clone(&view_ref_create);
  scenic::View view(session_view, std::move(view_token), std::move(view_control_ref),
                    std::move(view_ref_create), "View");
  scenic::View view_annotation(session_annotation, std::move(view_token_annotation),
                               "View Annotation");
  scenic::ViewHolder view_holder(session, std::move(view_holder_token), "ViewHolder");

  // Bounds of each view should be the size of a quarter of the display with
  // origin at 0,0 relative to its transform node.
  const std::array<float, 3> bmin = {0.f, 0.f, -2.f};
  const std::array<float, 3> bmax = {display_width, display_height / 2, 1.f};
  const std::array<float, 3> imin = {0, 0, 0};
  const std::array<float, 3> imax = {0, 0, 0};
  view_holder.SetViewProperties(bmin, bmax, imin, imax);
  view_holder.SetTranslation(0, display_height / 2, 0);

  // Pane extends across the entire right-side of the display, even though
  // its containing view is only in the top-right corner.
  auto pane_width = display_width;
  auto pane_height = display_height / 2;
  scenic::Rectangle pane_shape2(session_view, pane_width / 2, pane_height);
  scenic::Rectangle pane_shape_annotation(session_annotation, pane_width / 2, pane_height);

  // Create pane materials.
  scenic::Material pane_material_view(session_view);
  scenic::Material pane_material_annotation(session_annotation);
  pane_material_view.SetColor(0, 0, 255, 255);        // Blue
  pane_material_annotation.SetColor(0, 255, 0, 255);  // Green

  scenic::ShapeNode pane_node(session_view);
  pane_node.SetShape(pane_shape2);
  pane_node.SetMaterial(pane_material_view);
  pane_node.SetTranslation(pane_width / 4, pane_height / 2, 0);

  scenic::ShapeNode pane_node_annotation(session_annotation);
  pane_node_annotation.SetShape(pane_shape_annotation);
  pane_node_annotation.SetMaterial(pane_material_annotation);
  pane_node_annotation.SetTranslation(pane_width * 3 / 4, pane_height / 2, 0);

  // Add view holders to the transform.
  entity_node.AddChild(view_holder);
  view.AddChild(pane_node);
  view_annotation.AddChild(pane_node_annotation);

  Present(session);
  Present(session_view);

  RunLoopWithTimeout(zx::msec(100));

  // In this way we'll trigger the annotation ViewHolder creation and
  // annotation View creation in the same UpdateSessions() call and we
  // should ensure that there is no error nor any gfx crash.
  bool view_holder_annotation_created = false;
  fuchsia::ui::views::ViewRef view_ref_annotation;
  view_ref.Clone(&view_ref_annotation);
  annotation_registry()->CreateAnnotationViewHolder(
      std::move(view_ref_annotation), std::move(view_holder_token_annotation),
      [&view_holder_annotation_created]() { view_holder_annotation_created = true; });
  EXPECT_FALSE(view_holder_annotation_created);

  session_view->Present(zx::time(0), [this](auto) { QuitLoop(); });
  session_annotation->Present(zx::time(0), [this](auto) { QuitLoop(); });
  RunLoopWithTimeout(zx::msec(100));

  EXPECT_TRUE(view_holder_annotation_created);
}

}  // namespace integration_tests
