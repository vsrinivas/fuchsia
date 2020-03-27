// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/chromium/web_runner_tests/mock_get.h"
#include "src/chromium/web_runner_tests/test_server.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/lib/syslog/cpp/logger.h"
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

  auto web_runner = scenic::LaunchComponentAndCreateView(
      environment()->launcher_ptr(),
      fxl::StringPrintf("http://localhost:%d/static.html", server.port()), {});

  web_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

  // Present the view.
  scenic::EmbedderView embedder_view({
      .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
      .view_token = CreatePresentationViewToken(),
  });

  // Embed the view.
  embedder_view.EmbedView(std::move(web_runner), [this](fuchsia::ui::gfx::ViewState view_state) {
    EXPECT_TRUE(view_state.is_rendering);
    QuitLoop();
  });

  // Get the viewref koid.
  zx_koid_t view_ref_koid = WaitForKoid();
  EXPECT_NE(view_ref_koid, ZX_KOID_INVALID)
      << "No view ref could be intercepted. Possible Accessibility input wiring issue.";

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&] {
        auto tree = view_manager()->GetTreeByKoid(view_ref_koid);
        auto node = tree->GetNode(0);
        return node != nullptr;
      },
      kTimeout))
      << "No root node found.";

  auto tree = view_manager()->GetTreeByKoid(view_ref_koid);
  auto node = tree->GetNode(0);

  ASSERT_TRUE(node->has_attributes());
  ASSERT_TRUE(node->attributes().has_label()) << "Missing label: " << tree->ToString();
  EXPECT_EQ(node->attributes().label(), "Say something. Anything.");

  // TODO: more assertions
  //
  // Example semantic tree for static.html:
  /*
  ID: 0 Label:Say something. Anything.
    ID: 7 Label:no label
        ID: 8 Label:Test 1 2 3...
            ID: 17 Label:Test 1 2 3...
  */
}

}  // namespace
}  // namespace accessibility_test
