// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/embedded_view_utils.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <gtest/gtest.h>

#include "garnet/testing/views/embedder_view.h"
#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/scenic/lib/gfx/tests/pixel_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"

namespace {

const int64_t kTestTimeout = 60;

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ViewEmbedderTest : public gfx::PixelTest {
 public:
  ViewEmbedderTest() : gfx::PixelTest("ViewEmbedderTest") {}
};

VK_TEST_F(ViewEmbedderTest, BouncingBall) {
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

VK_TEST_F(ViewEmbedderTest, ProtectedVkcube) {
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
      "fuchsia-pkg://fuchsia.com/vkcube_on_scenic#meta/vkcube_on_scenic.cmx",
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

}  // namespace
