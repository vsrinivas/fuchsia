// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/ui/base_view/embedded_view_utils.h"
#include "src/ui/a11y/lib/semantics/tests/semantics_integration_test_fixture.h"
#include "src/ui/testing/views/embedder_view.h"

namespace accessibility_test {
namespace {

constexpr char kClientUrl[] = "fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cmx";
constexpr zx::duration kTimeout = zx::sec(15);

class FlutterSemanticsTests : public SemanticsIntegrationTest {
 public:
  FlutterSemanticsTests() : SemanticsIntegrationTest("flutter_semantics_test") {}
};

// Loads ally-demo flutter app and verifies its semantic tree.
TEST_F(FlutterSemanticsTests, StaticSemantics) {
  view_manager()->SetSemanticsEnabled(true);

  auto flutter_runner =
      scenic::LaunchComponentAndCreateView(environment()->launcher_ptr(), kClientUrl);
  flutter_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

  // Present the view.
  scenic::EmbedderView embedder_view({
      .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
      .view_token = CreatePresentationViewToken(),
  });

  // Embed the view.
  embedder_view.EmbedView(std::move(flutter_runner),
                          [this](fuchsia::ui::gfx::ViewState view_state) {
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

  EXPECT_TRUE(node->has_attributes());
  EXPECT_TRUE(node->attributes().has_label()) << "Missing label: " << tree->ToString();

  // TODO: more assertions
  //
  // Semantic tree a11y-demo:
  // ID: 0 Label:
  //   ID: 1 Label:Blue tapped 0 times
  //   ID: 2 Label:Yellow tapped 0 times
  //   ID: 3 Label:Blue
  //   ID: 4 Label:Yellow
}

}  // namespace
}  // namespace accessibility_test
