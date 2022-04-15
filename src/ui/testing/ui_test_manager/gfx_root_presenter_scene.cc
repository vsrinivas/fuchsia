// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/gfx_root_presenter_scene.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

void GfxRootPresenterScene::Initialize(component_testing::RealmRoot* realm) {
  realm_ = realm;

  scenic_ = realm_->Connect<fuchsia::ui::scenic::Scenic>();

  // Set up ui test manager's View, to harvest the client view's state.
  auto session_pair = scenic::CreateScenicSessionPtrAndListenerRequest(scenic_.get());
  session_ = std::make_unique<scenic::Session>(std::move(session_pair.first),
                                               std::move(session_pair.second));

  // Create view tokens.
  auto test_view_tokens = scenic::ViewTokenPair::New();
  auto client_view_tokens = scenic::ViewTokenPair::New();

  // Instruct root_presenter to present ui test manager's View.
  auto root_presenter = realm_->Connect<fuchsia::ui::policy::Presenter>();

  root_presenter->PresentOrReplaceView(std::move(test_view_tokens.view_holder_token),
                                       /* presentation */ nullptr);

  // Set up test's View, to harvest the client view's view_state.is_rendering signal.
  session_->SetDebugName("ui-test-manager-scene");
  session_->set_event_handler([this](const std::vector<fuchsia::ui::scenic::Event>& events) {
    for (const auto& event : events) {
      if (!event.is_gfx())
        continue;  // skip non-gfx events

      if (event.gfx().is_view_properties_changed()) {
        FX_LOGS(INFO) << "View properties changed";
        const auto properties = event.gfx().view_properties_changed().properties;
        FX_CHECK(client_view_holder_) << "Expect that view holder is already set up.";
        client_view_holder_->SetViewProperties(properties);
        session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
      } else if (event.gfx().is_view_state_changed()) {
        FX_LOGS(INFO) << "Client view is rendering content";
        client_view_is_rendering_ = true;
      } else if (event.gfx().is_view_connected()) {
        FX_LOGS(INFO) << "Client view connected";
        client_view_connected_ = true;
      } else if (event.gfx().is_view_disconnected()) {
        FX_LOGS(INFO) << "Client view disconnected";
        client_view_connected_ = false;
      } else if (event.gfx().is_view_attached_to_scene()) {
        FX_LOGS(INFO) << "Test view attached to scene";
        test_view_attached_ = true;
      } else if (event.gfx().is_view_detached_from_scene()) {
        FX_LOGS(INFO) << "Test view detached from scene";
        test_view_attached_ = false;
      }
    }
  });

  client_view_holder_ = std::make_unique<scenic::ViewHolder>(
      session_.get(), std::move(client_view_tokens.view_holder_token), "client view holder");
  ui_test_manager_view_ = std::make_unique<scenic::View>(
      session_.get(), std::move(test_view_tokens.view_token), "test view");
  ui_test_manager_view_->AddChild(*client_view_holder_);

  // Request to present ; this will trigger dispatch of view properties.
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});

  // Attach client view.
  auto view_provider = realm_->Connect<fuchsia::ui::app::ViewProvider>();
  auto [client_control_ref, client_view_ref] = scenic::ViewRefPair::New();
  client_view_ref_ = fidl::Clone(client_view_ref);
  view_provider->CreateViewWithViewRef(std::move(client_view_tokens.view_token.value),
                                       std::move(client_control_ref), std::move(client_view_ref));
}

bool GfxRootPresenterScene::ClientViewIsAttached() {
  return test_view_attached_ && client_view_connected_;
}

bool GfxRootPresenterScene::ClientViewIsRendering() { return client_view_is_rendering_; }

std::optional<zx_koid_t> GfxRootPresenterScene::ClientViewRefKoid() {
  if (!client_view_ref_)
    return std::nullopt;

  return fsl::GetKoid(client_view_ref_->reference.get());
}

}  // namespace ui_testing
