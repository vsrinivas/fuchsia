// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/chromium/web_runner_tests/mock_get.h"
#include "src/chromium/web_runner_tests/test_server.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

constexpr zx::duration kTimeout = zx::sec(60);

class WebSemanticsTest : public SemanticsIntegrationTest {
 public:
  WebSemanticsTest() : SemanticsIntegrationTest("web_semantics_test") {}

  // |SemanticsIntegrationTest|
  void CreateServices(std::unique_ptr<sys::testing::EnvironmentServices>& services) override {
    services->AddServiceWithLaunchInfo(
        {.url = "fuchsia-pkg://fuchsia.com/web_engine#meta/context_provider.cmx"},
        "fuchsia.web.ContextProvider");
  }

  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(SemanticsIntegrationTest::SetUp());

    web_runner_tests::TestServer server;
    FX_CHECK(server.FindAndBindPort());

    auto serve = server.ServeAsync([&server] {
      while (server.Accept()) {
        web_runner_tests::MockHttpGetResponse(&server, "static.html");
      }
    });

    view_manager()->SetSemanticsEnabled(true);

    scenic::EmbeddedViewInfo web_runner = scenic::LaunchComponentAndCreateView(
        environment()->launcher_ptr(),
        fxl::StringPrintf("http://localhost:%d/static.html", server.port()), {});

    web_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

    view_ref_koid_ = fsl::GetKoid(web_runner.view_ref.reference.get());

    // Present the view.
    embedder_view_.emplace(scenic::ViewContext{
        .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
        .view_token = CreatePresentationViewToken(),
    });

    // Embed the view.
    bool is_rendering = false;
    embedder_view_->EmbedView(std::move(web_runner),
                              [&is_rendering](fuchsia::ui::gfx::ViewState view_state) {
                                is_rendering = view_state.is_rendering;
                              });
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&is_rendering] { return is_rendering; }, kTimeout));

    EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
        [this] {
          auto node = view_manager()->GetSemanticNode(view_ref_koid_, 0u);
          return node != nullptr && node->has_attributes() && node->attributes().has_label();
        },
        kTimeout))
        << "No root node found.";

    /* The semantic tree for static.html:
     * ID: 0 Label:Say something. Anything.
     *     ID: 5 Label:no label
     *         ID: 7 Label:Test 1 2 3...
     *             ID: 13 Label:Test 1 2 3...
     *         ID: 11 Label:Click here
     *             ID: 14 Label:Click here
     *                 ID: 15 Label:Click here
     */
  }

  zx_koid_t view_ref_koid() const { return view_ref_koid_; }

 private:
  // Wrapped in optional since the view is not created until the middle of SetUp
  std::optional<scenic::EmbedderView> embedder_view_;

  zx_koid_t view_ref_koid_;
};

// Loads a static page via the component framework and verifies its semantic tree.
TEST_F(WebSemanticsTest, StaticSemantics) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Say something. Anything.");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Test 1 2 3... ");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid(), "Click here");
  ASSERT_TRUE(node);
}

TEST_F(WebSemanticsTest, HitTesting) {
  auto root = view_manager()->GetSemanticNode(view_ref_koid(), 0u);

  // When performing hit tests, aim for just inside the node's bounding box.  Note
  // that for nodes from Chrome, the min corner has a larger y value than the max.
  fuchsia::math::PointF offset = {1., -1.};

  // Hit test the plain text
  auto node = FindNodeWithLabel(root, view_ref_koid(), "Test 1 2 3... ");
  ASSERT_TRUE(node);
  auto hit_node = HitTest(view_ref_koid(), CalculateViewTargetPoint(view_ref_koid(), node, offset));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());

  // Hit test the button
  node = FindNodeWithLabel(root, view_ref_koid(), "Click here");
  ASSERT_TRUE(node);
  hit_node = HitTest(view_ref_koid(), CalculateViewTargetPoint(view_ref_koid(), node, offset));
  ASSERT_TRUE(hit_node.has_value());
  ASSERT_EQ(*hit_node, node->node_id());
}

}  // namespace
}  // namespace accessibility_test
