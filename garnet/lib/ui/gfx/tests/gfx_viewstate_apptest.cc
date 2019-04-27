// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/embedded_view_utils.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/fxl/logging.h>
#include <zircon/status.h>

#include "garnet/testing/views/embedder_view.h"

namespace {

// clang-format off
const std::map<std::string, std::string> kServices = {
    {"fuchsia.tracelink.Registry", "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx"},
    {"fuchsia.ui.policy.Presenter", "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
    {"fuchsia.ui.scenic.Scenic", "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"},
    {"fuchsia.vulkan.loader.Loader", "fuchsia-pkg://fuchsia.com/vulkan_loader#meta/vulkan_loader.cmx"},
    {"fuchsia.sysmem.Allocator", "fuchsia-pkg://fuchsia.com/sysmem_connector#meta/sysmem_connector.cmx"},
};
// clang-format on

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ViewEmbedderTest : public sys::testing::TestWithEnvironment {
 protected:
  ViewEmbedderTest() {
    std::unique_ptr<sys::testing::EnvironmentServices> services =
        CreateServices();

    for (const auto& [service_name, url] : kServices) {
      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = url;
      services->AddServiceWithLaunchInfo(std::move(launch_info), service_name);
    }

    constexpr char kEnvironment[] = "ViewEmbedderTest";
    environment_ =
        CreateNewEnclosingEnvironment(kEnvironment, std::move(services));

    environment_->ConnectToService(scenic_.NewRequest());
    scenic_.set_error_handler([this](zx_status_t status) {
      FAIL() << "Lost connection to Scenic: " << status;
    });
  }

  // Create a |ViewContext| that allows us to present a view via
  // |RootPresenter|. See also examples/ui/simplest_embedder
  scenic::ViewContext CreatePresentationContext() {
    auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
        .view_token2 = std::move(view_token),
    };

    fuchsia::ui::policy::PresenterPtr presenter;
    environment_->ConnectToService(presenter.NewRequest());
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return view_context;
  }

  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
};

TEST_F(ViewEmbedderTest, BouncingBall) {
  auto info = scenic::LaunchComponentAndCreateView(
      environment_->launcher_ptr(),
      "fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cmx", {});

  scenic::EmbedderView embedder_view(CreatePresentationContext());

  bool view_state_changed_observed = false;
  embedder_view.EmbedView(std::move(info),
                          [&view_state_changed_observed](auto) {
                            view_state_changed_observed = true;
                          });

  EXPECT_TRUE(RunLoopWithTimeoutOrUntil(
      [&view_state_changed_observed] { return view_state_changed_observed; },
      zx::sec(10)));
}

}  // namespace
