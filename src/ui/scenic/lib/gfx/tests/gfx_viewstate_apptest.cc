// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/ui/base_view/base_view.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/scenic/lib/gfx/tests/pixel_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/testing/views/embedder_view.h"

namespace {

const int64_t kTestTimeout = 60;

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ViewEmbedderTest : public gfx::PixelTest {
 public:
  ViewEmbedderTest() : gfx::PixelTest("ViewEmbedderTest") {}
};

TEST_F(ViewEmbedderTest, BouncingBall) {
  auto info = scenic::LaunchComponentAndCreateView(
      environment_->launcher_ptr(),
      "fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cmx", {});

  scenic::EmbedderView embedder_view(CreatePresentationContext());

  bool view_state_changed_observed = false;
  embedder_view.EmbedView(std::move(info), [&view_state_changed_observed](auto) {
    view_state_changed_observed = true;
  });

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&view_state_changed_observed] { return view_state_changed_observed; },
      zx::sec(kTestTimeout)));
}

TEST_F(ViewEmbedderTest, ProtectedVkcube) {
  // vkcube_on_scenic does not produce protected content if platform does not allow. Check if
  // protected memory is available beforehand to skip these cases.
  {
    if (!scenic_impl::gfx::test::VkSessionTest::CreateVulkanDeviceQueues(
            /*use_protected_memory=*/true)) {
      GTEST_SKIP();
    }
  }

  auto info = scenic::LaunchComponentAndCreateView(
      environment_->launcher_ptr(),
      "fuchsia-pkg://fuchsia.com/vkcube-on-scenic#meta/vkcube_on_scenic.cmx",
      {"--protected_output"});

  scenic::EmbedderView embedder_view(CreatePresentationContext());

  bool view_state_changed_observed = false;
  embedder_view.EmbedView(std::move(info), [&view_state_changed_observed](auto) {
    view_state_changed_observed = true;
  });

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&view_state_changed_observed] { return view_state_changed_observed; },
      zx::sec(kTestTimeout)));
}

// Initialize two sessions and their associated views, and ensure that killing the embedded
// session triggers a ViewDisconnected event to the holding one.
TEST_F(ViewEmbedderTest, DeadBindingShouldKillSession) {
  // Initialize session 1.
  auto test_session = SetUpTestSession();
  scenic::Session* const session = &test_session->session;
  const auto [display_width, display_height] = test_session->display_dimensions;
  scenic::Scene* const scene = &test_session->scene;
  test_session->SetUpCamera().SetProjection(0);

  // Initialize session 2.
  auto unique_session2 = std::make_unique<scenic::Session>(scenic());
  auto session2 = unique_session2.get();
  session2->set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << "Session2 terminated.";
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

  session->set_event_handler(
      [&view_connected_observed](std::vector<fuchsia::ui::scenic::Event> events) {
        for (const auto& event : events) {
          if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
              event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewConnected) {
            view_connected_observed = true;
            return;
          }
        }
        ASSERT_FALSE(true);
      });

  Present(session);
  Present(session2);

  EXPECT_TRUE(
      RunLoopWithTimeoutOrUntil([&view_connected_observed]() { return view_connected_observed; }));

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

}  // namespace
