// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/scene_provider/scene_provider.h"

#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/observation/test/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/testing/scene_provider/scene_provider_config_lib.h"

namespace ui_testing {

void SceneProvider::AttachClientView(
    fuchsia::ui::test::scene::ProviderAttachClientViewRequest request,
    AttachClientViewCallback callback) {
  FX_LOGS(INFO) << "Attach client view";

  auto scene_provider_config = scene_provider_config_lib::Config::TakeFromStartupHandle();

  fuchsia::ui::views::ViewRef client_view_ref;

  if (scene_provider_config.use_scene_manager()) {
    fuchsia::session::scene::ManagerSyncPtr scene_manager;
    context_->svc()->Connect(scene_manager.NewRequest());

    fuchsia::session::scene::Manager_SetRootView_Result set_root_view_result;
    scene_manager->SetRootView(std::move(*request.mutable_view_provider()), &set_root_view_result);
    if (set_root_view_result.is_response()) {
      client_view_ref = std::move(set_root_view_result.response().view_ref);
    }
  } else {
    fuchsia::ui::policy::PresenterPtr root_presenter;
    context_->svc()->Connect(root_presenter.NewRequest());

    auto client_view_tokens = scenic::ViewTokenPair::New();
    auto [client_control_ref, view_ref] = scenic::ViewRefPair::New();
    client_view_ref = fidl::Clone(view_ref);

    root_presenter->PresentOrReplaceView2(std::move(client_view_tokens.view_holder_token),
                                          fidl::Clone(client_view_ref),
                                          /* presentation */ nullptr);

    auto client_view_provider = request.mutable_view_provider()->Bind();
    client_view_provider->CreateViewWithViewRef(std::move(client_view_tokens.view_token.value),
                                                std::move(client_control_ref), std::move(view_ref));
  }

  callback(fsl::GetKoid(client_view_ref.reference.get()));
}

void SceneProvider::RegisterGeometryObserver(
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> geometry_observer,
    RegisterGeometryObserverCallback callback) {
  // Register the client's geometry observer.
  fuchsia::ui::observation::test::RegistrySyncPtr observer_registry;
  context_->svc()->Connect<fuchsia::ui::observation::test::Registry>(
      observer_registry.NewRequest());
  observer_registry->RegisterGlobalGeometryProvider(std::move(geometry_observer));

  callback();
}

fidl::InterfaceRequestHandler<fuchsia::ui::test::scene::Provider> SceneProvider::GetHandler() {
  return scene_provider_bindings_.GetHandler(this);
}

}  // namespace ui_testing
