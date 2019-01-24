// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/async/cpp/task.h>
#include <lib/component/cpp/testing/test_with_environment.h>
#include <lib/fsl/vmo/vector.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/base_view/cpp/embedded_view_utils.h>
#include <lib/ui/gfx/cpp/math.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>

#include "lib/component/cpp/startup_context.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/scenic/cpp/resources.h"
#include "lib/ui/scenic/cpp/session.h"

#include <map>
#include <string>
#include <vector>

namespace {

struct ViewContext {
  scenic::SessionPtrAndListenerRequest session_and_listener_request;
  zx::eventpair view_token;
};

// clang-format off
const std::map<std::string, std::string> kServices = {
    {"fuchsia.tracelink.Registry", "fuchsia-pkg://fuchsia.com/trace_manager#meta/trace_manager.cmx"},
    {"fuchsia.ui.policy.Presenter2", "fuchsia-pkg://fuchsia.com/root_presenter#meta/root_presenter.cmx"},
    {"fuchsia.ui.scenic.Scenic", "fuchsia-pkg://fuchsia.com/scenic#meta/scenic.cmx"},
    {"fuchsia.vulkan.loader.Loader", "fuchsia-pkg://fuchsia.com/vulkan_loader#meta/vulkan_loader.cmx"},
};
// clang-format on

class EmbedderView : public fuchsia::ui::scenic::SessionListener {
 public:
  EmbedderView(ViewContext context, scenic::EmbeddedViewInfo info,
               const std::string& debug_name = "EmbedderView")
      : binding_(this, std::move(context.session_and_listener_request.second)),
        session_(std::move(context.session_and_listener_request.first)),
        view_(&session_, std::move(context.view_token), debug_name),
        embedded_view_info_(std::move(info)),
        view_holder_(&session_,
                     std::move(embedded_view_info_.view_holder_token),
                     debug_name + " ViewHolder"),
        top_node_(&session_) {
    binding_.set_error_handler([](zx_status_t status) { FAIL() << status; });
    view_.AddChild(top_node_);
    top_node_.Attach(view_holder_);
    // Call |Session::Present| in order to flush events having to do with
    // creation of |view_| and |top_node_|.
    session_.Present(0, [](auto) {});
  }

  void SetViewStateChangedCallback(
      std::function<void(fuchsia::ui::gfx::ViewState)> callback) {
    view_state_changed_callback_ = std::move(callback);
  }

 private:
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    for (const auto& event : events) {
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
          event.gfx().Which() ==
              fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
        const auto& evt = event.gfx().view_properties_changed();
        view_holder_.SetViewProperties(std::move(evt.properties));
        session_.Present(0, [](auto) {});
      } else if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
                 event.gfx().Which() ==
                     fuchsia::ui::gfx::Event::Tag::kViewStateChanged) {
        const auto& evt = event.gfx().view_state_changed();
        if (evt.view_holder_id == view_holder_.id()) {
          // Clients of |EmbedderView| *must* set a view state changed
          // callback.  Failure to do so is a usage error.
          FXL_DCHECK(view_state_changed_callback_);
          view_state_changed_callback_(evt.state);
        }
      }
    }
  }

  void OnScenicError(std::string error) override { FAIL() << error; }

  fidl::Binding<fuchsia::ui::scenic::SessionListener> binding_;
  scenic::Session session_;
  scenic::View view_;
  scenic::EmbeddedViewInfo embedded_view_info_;
  scenic::ViewHolder view_holder_;
  scenic::EntityNode top_node_;
  std::function<void(fuchsia::ui::gfx::ViewState)> view_state_changed_callback_;
};

// Test fixture that sets up an environment suitable for Scenic pixel tests
// and provides related utilities. The environment includes Scenic and
// RootPresenter, and their dependencies.
class ViewEmbedderTest : public component::testing::TestWithEnvironment {
 protected:
  ViewEmbedderTest() {
    std::unique_ptr<component::testing::EnvironmentServices> services =
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
  // |RootPresenter|. See also examples/ui/hello_base_view
  ViewContext CreatePresentationContext() {
    zx::eventpair view_holder_token, view_token;
    FXL_CHECK(zx::eventpair::create(0u, &view_holder_token, &view_token) ==
              ZX_OK);

    ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get()),
        .view_token = std::move(view_token),
    };

    fuchsia::ui::policy::Presenter2Ptr presenter;
    environment_->ConnectToService(presenter.NewRequest());
    presenter->PresentView(std::move(view_holder_token), nullptr);

    return view_context;
  }

  fuchsia::ui::scenic::ScenicPtr scenic_;
  std::unique_ptr<component::testing::EnclosingEnvironment> environment_;
};

TEST_F(ViewEmbedderTest, BouncingBall) {
  auto info = scenic::LaunchComponentAndCreateView(
      environment_->launcher_ptr(),
      "fuchsia-pkg://fuchsia.com/bouncing_ball#meta/bouncing_ball.cmx", {});

  EmbedderView embedder_view(CreatePresentationContext(), std::move(info));

  // Run the loop until we observe the view state changing, or hit a 10 second
  // timeout.
  bool view_state_changed_observed = false;
  embedder_view.SetViewStateChangedCallback(
      [this, &view_state_changed_observed](auto view_state) {
        view_state_changed_observed = true;
        async::PostTask(dispatcher(), QuitLoopClosure());
      });
  RunLoopWithTimeout(zx::sec(10));

  EXPECT_TRUE(view_state_changed_observed);
}

}  // namespace
