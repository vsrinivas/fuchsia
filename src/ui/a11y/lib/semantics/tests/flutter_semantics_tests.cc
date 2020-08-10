// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
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

  scenic::EmbeddedViewInfo flutter_runner =
      scenic::LaunchComponentAndCreateView(environment()->launcher_ptr(), kClientUrl);
  flutter_runner.controller.events().OnTerminated = [](auto...) { FAIL(); };

  // Get the viewref koid.
  const zx_koid_t view_ref_koid = fsl::GetKoid(flutter_runner.view_ref.reference.get());

  // Present the view.
  scenic::EmbedderView embedder_view({
      .session_and_listener_request = scenic::CreateScenicSessionPtrAndListenerRequest(scenic()),
      .view_token = CreatePresentationViewToken(),
  });

  // Embed the view.
  bool is_rendering = false;
  embedder_view.EmbedView(std::move(flutter_runner),
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

  // Verify semantic tree for a11y-demo:
  // ID: 0 Label:
  //   ID: 1 Label:Blue tapped 0 times
  //   ID: 2 Label:Yellow tapped 0 times
  //   ID: 3 Label:Blue
  //   ID: 4 Label:Yellow

  auto root = view_manager()->GetSemanticNode(view_ref_koid, 0u);
  auto node = FindNodeWithLabel(root, view_ref_koid, "Blue tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid, "Yellow tapped 0 times");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid, "Blue");
  ASSERT_TRUE(node);

  node = FindNodeWithLabel(root, view_ref_koid, "Yellow");
  ASSERT_TRUE(node);
}

}  // namespace
}  // namespace accessibility_test
