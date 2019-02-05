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
  EmbedderView(ViewContext context,
               const std::string& debug_name = "EmbedderView")
      : binding_(this, std::move(context.session_and_listener_request.second)),
        session_(std::move(context.session_and_listener_request.first)),
        view_(&session_, std::move(context.view_token), debug_name),
        top_node_(&session_) {
    binding_.set_error_handler([](zx_status_t status) { FAIL() << status; });
    view_.AddChild(top_node_);
    // Call |Session::Present| in order to flush events having to do with
    // creation of |view_| and |top_node_|.
    session_.Present(0, [](auto) {});
  }

  // Sets the EmbeddedViewInfo and attaches the embedded View to the scene. Any
  // callbacks for the embedded View's ViewState are delivered to the supplied
  // callback.
  void EmbedView(scenic::EmbeddedViewInfo info,
                 std::function<void(fuchsia::ui::gfx::ViewState)> callback) {
    // Only one EmbeddedView is currently supported.
    FXL_CHECK(!embedded_view_);
    embedded_view_ = std::make_unique<EmbeddedView>(std::move(info), &session_,
                                                    std::move(callback));

    // Attach the embedded view to the scene.
    top_node_.Attach(embedded_view_->view_holder);

    // Call |Session::Present| to apply the embedded view to the scene graph.
    session_.Present(0, [](auto) {});
  }

 private:
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    for (const auto& event : events) {
      if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
          event.gfx().Which() ==
              fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged) {
        const auto& evt = event.gfx().view_properties_changed();
        // Naively apply the parent's ViewProperties to any EmbeddedViews.
        if (embedded_view_) {
          embedded_view_->view_holder.SetViewProperties(
              std::move(evt.properties));
          session_.Present(0, [](auto) {});
        }
      } else if (event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
                 event.gfx().Which() ==
                     fuchsia::ui::gfx::Event::Tag::kViewStateChanged) {
        const auto& evt = event.gfx().view_state_changed();
        if (embedded_view_ &&
            evt.view_holder_id == embedded_view_->view_holder.id()) {
          // Clients of |EmbedderView| *must* set a view state changed
          // callback.  Failure to do so is a usage error.
          FXL_CHECK(embedded_view_->view_state_changed_callback);
          embedded_view_->view_state_changed_callback(evt.state);
        }
      }
    }
  }

  void OnScenicError(std::string error) override { FAIL() << error; }

  struct EmbeddedView {
    EmbeddedView(
        scenic::EmbeddedViewInfo info, scenic::Session* session,
        std::function<void(fuchsia::ui::gfx::ViewState)> view_state_callback,
        const std::string& debug_name = "EmbedderView")
        : embedded_info(std::move(info)),
          view_holder(session, std::move(embedded_info.view_holder_token),
                      debug_name + " ViewHolder"),
          view_state_changed_callback(std::move(view_state_callback)) {}

    scenic::EmbeddedViewInfo embedded_info;
    scenic::ViewHolder view_holder;
    std::function<void(fuchsia::ui::gfx::ViewState)>
        view_state_changed_callback;
  };

  fidl::Binding<fuchsia::ui::scenic::SessionListener> binding_;
  scenic::Session session_;
  scenic::View view_;
  scenic::EntityNode top_node_;
  std::optional<fuchsia::ui::gfx::ViewProperties> embedded_view_properties_;
  std::unique_ptr<EmbeddedView> embedded_view_;
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

  EmbedderView embedder_view(CreatePresentationContext());

  bool view_state_changed_observed = false;
  auto view_state_callback = [this,
                              &view_state_changed_observed](auto view_state) {
    view_state_changed_observed = true;
    QuitLoop();
  };
  embedder_view.EmbedView(std::move(info), std::move(view_state_callback));

  // Run the loop until we observe the view state changing, or hit a 10 second
  // timeout.
  RunLoopWithTimeout(zx::sec(10));
  EXPECT_TRUE(view_state_changed_observed);
}

}  // namespace
