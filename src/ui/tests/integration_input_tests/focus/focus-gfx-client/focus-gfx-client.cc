// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/zx/eventpair.h>
#include <zircon/status.h>

#include <iostream>
#include <memory>

#include <test/focus/cpp/fidl.h>

// A bare-bones component to observe focus events, crafted with the Scenic "GFX" API.

namespace focus_gfx_client {

class FocusGfxClient : fuchsia::ui::app::ViewProvider {
 public:
  FocusGfxClient() : view_provider_binding_(this) {
    context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();

    // Vend the ViewProvider protocol.
    context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
        [this](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
          view_provider_binding_.Bind(std::move(request));
        });

    // Connect to the test's ResponseListener.
    response_listener_ = context_->svc()->Connect<test::focus::ResponseListener>();
    response_listener_.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Response listener disconnected: " << zx_status_get_string(status);
    });

    // Connect to Scenic, set up a scenic session.
    auto scenic = context_->svc()->Connect<fuchsia::ui::scenic::Scenic>();
    scenic.set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Scenic disconnected, status: " << zx_status_get_string(status);
    });

    fuchsia::ui::scenic::SessionPtr client_endpoint;
    fuchsia::ui::scenic::SessionEndpoints endpoints;
    endpoints.set_session(client_endpoint.NewRequest());
    endpoints.set_view_ref_focused(vrf_.NewRequest());
    scenic->CreateSessionT(std::move(endpoints), [] { /* don't block, feed forward */ });
    session_ = std::make_unique<scenic::Session>(std::move(client_endpoint));
    session_->SetDebugName("focus-gfx-client");
    session_->set_error_handler([](zx_status_t status) {
      FX_LOGS(FATAL) << "Scenic session disconnected, status: " << zx_status_get_string(status);
    });

    // Publish changes to the scene graph.
    session_->Present2(/*when*/ zx_clock_get_monotonic(), /*span*/ 0, [](auto) {});

    // ViewProvider becomes available for clients on loop.Run(), after this constructor finishes.
  }

 private:
  // |fuchsia::ui::app::ViewProvider|
  void CreateViewWithViewRef(zx::eventpair token, fuchsia::ui::views::ViewRefControl control_ref,
                             fuchsia::ui::views::ViewRef view_ref) override {
    FX_LOGS(INFO) << "CreateViewWithViewRef called.";

    view_ = std::make_unique<scenic::View>(
        session_.get(), fuchsia::ui::views::ViewToken{.value = std::move(token)},
        std::move(control_ref), std::move(view_ref), "focus-gfx-client view");

    session_->Present2(zx_clock_get_monotonic(), 0, [](auto) { /* don't block, feed forward */ });

    // Now wait for a focus event, and report it back to the test.
    vrf_->Watch([this](fuchsia::ui::views::FocusState focus_state) {
      FX_LOGS(INFO) << "focus data: " << std::boolalpha << focus_state.focused();
      test::focus::Data data;
      data.set_time_received(zx_clock_get_monotonic()).set_focus_status(focus_state.focused());
      response_listener_->Respond(std::move(data));
    });
  }

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair, fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override {
    FX_NOTREACHED() << "unused";
  }

  // This component's global context.
  std::unique_ptr<sys::ComponentContext> context_;

  // Protocols used by this component.
  fuchsia::ui::views::ViewRefFocusedPtr vrf_;
  test::focus::ResponseListenerPtr response_listener_;

  // Protocols vended by this component.
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;

  // Scene state.
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::View> view_;
};

}  // namespace focus_gfx_client

int main(int argc, char** argv) {
  FX_LOGS(INFO) << "Starting component";
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  focus_gfx_client::FocusGfxClient client;
  return loop.Run();
}
