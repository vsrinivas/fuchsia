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
};

// Loads a static page via the component framework and verifies its semantic tree.
TEST_F(WebSemanticsTest, StaticSemantics) {
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

  // Get the viewref koid.
  const zx_koid_t view_ref_koid = fsl::GetKoid(web_runner.view_ref.reference.get());

  // Present the view.
  scenic::EmbedderView embedder_view({
      .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
      .view_token = CreatePresentationViewToken(),
  });

  // Embed the view.
  bool is_rendering = false;
  embedder_view.EmbedView(std::move(web_runner),
                          [&is_rendering](fuchsia::ui::gfx::ViewState view_state) {
                            is_rendering = view_state.is_rendering;
                          });
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&is_rendering] { return is_rendering; }, kTimeout));
  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] {
        auto node = view_manager()->GetSemanticNode(view_ref_koid, 0u);
        return node != nullptr && node->has_attributes() && node->attributes().has_label();
      },
      kTimeout))
      << "No root node found.";

  // Verify semantic tree for static.html:
  /*
  0 Label:Say something. Anything.
    ID: 5 Label:no label
        ID: 6 Label:Test 1 2 3...
            ID: 10 Label:Test 1 2 3...
  */

  auto root = view_manager()->GetSemanticNode(view_ref_koid, 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid, "Say something. Anything.");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid, "Test 1 2 3...");
  ASSERT_TRUE(node);
}

}  // namespace
}  // namespace accessibility_test
