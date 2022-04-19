// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/gfx_test_view.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

// |fuchsia::ui::app::ViewProvider|
void GfxTestView::CreateViewWithViewRef(zx::eventpair token,
                                        fuchsia::ui::views::ViewRefControl view_ref_control,
                                        fuchsia::ui::views::ViewRef view_ref) {
  // Set up ui test manager's View, to harvest the client view's state.
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get());
  session_ = std::make_unique<scenic::Session>(std::move(session_pair.first),
                                               std::move(session_pair.second));

  session_->SetDebugName("gfx-test-view-session");
  session_->set_event_handler([this](const std::vector<fuchsia::ui::scenic::Event>& events) {
    for (const auto& event : events) {
      if (!event.is_gfx())
        continue;  // skip non-gfx events

      if (event.gfx().is_view_properties_changed()) {
        FX_LOGS(INFO) << "View properties changed";
        test_view_properties_ = event.gfx().view_properties_changed().properties;
        if (child_view_holder_) {
          child_view_holder_->SetViewProperties(*test_view_properties_);
          session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
        }
      } else if (event.gfx().is_view_attached_to_scene()) {
        FX_LOGS(INFO) << "Test view attached to scene";
        test_view_attached_ = true;
      } else if (event.gfx().is_view_detached_from_scene()) {
        FX_LOGS(INFO) << "Test view detached from scene";
        test_view_attached_ = false;
      } else if (event.gfx().is_view_state_changed()) {
        FX_LOGS(INFO) << "Client view is rendering content";
        child_view_is_rendering_ = true;
      } else if (event.gfx().is_view_connected()) {
        FX_LOGS(INFO) << "Client view connected";
        child_view_connected_ = true;
      } else if (event.gfx().is_view_disconnected()) {
        FX_LOGS(INFO) << "Client view disconnected";
        child_view_connected_ = false;
      } else if (event.gfx().is_metrics()) {
        const auto& metrics = event.gfx().metrics().metrics;
        scale_factor_ = std::max(metrics.scale_x, metrics.scale_y);
        FX_LOGS(INFO) << "Test view scale factor updated to: " << scale_factor_;
      }
    }
  });

  // Create test view.
  test_view_ = std::make_unique<scenic::View>(session_.get(), scenic::ToViewToken(std::move(token)),
                                              std::move(view_ref_control), std::move(view_ref),
                                              "test manager view");

  // Request to present ; this will trigger dispatch of view properties.
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
}

// |fuchsia.ui.app.ViewProvider|
void GfxTestView::CreateView(zx::eventpair view_handle,
                             fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                             fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) {
  FX_LOGS(ERROR) << "CreateView() is not implemented.";
}

// |fuchsia.ui.app.ViewProvider|
void GfxTestView::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  FX_LOGS(ERROR) << "CreateView2() is not implemented.";
}

fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> GfxTestView::NewViewProviderBinding() {
  return view_provider_binding_.NewBinding();
}

void GfxTestView::AttachChildView(fuchsia::ui::app::ViewProviderPtr child_view_provider) {
  // Create child view tokens.
  auto child_view_tokens = scenic::ViewTokenPair::New();

  // Create client view holder.
  child_view_holder_ = std::make_unique<scenic::ViewHolder>(
      session_.get(), std::move(child_view_tokens.view_holder_token), "client view holder");
  test_view_->AddChild(*child_view_holder_);

  // We may not have the test view's properties yet.
  if (test_view_properties_) {
    child_view_holder_->SetViewProperties(*test_view_properties_);
  }

  // Listen for view metrics events on the child view holder.
  child_view_holder_->SetEventMask(fuchsia::ui::gfx::kMetricsEventMask);

  // Request to present ; this will trigger dispatch of view properties.
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});

  auto [child_control_ref, child_view_ref] = scenic::ViewRefPair::New();
  child_view_ref_ = fidl::Clone(child_view_ref);
  child_view_provider->CreateViewWithViewRef(std::move(child_view_tokens.view_token.value),
                                             std::move(child_control_ref),
                                             std::move(child_view_ref));
}

}  // namespace ui_testing
