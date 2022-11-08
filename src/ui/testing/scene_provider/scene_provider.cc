// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/scene_provider/scene_provider.h"

#include <fuchsia/session/scene/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/testing/scene_provider/scene_provider_config_lib.h"

namespace ui_testing {

void FakeViewController::Dismiss() { dismiss_(); }

SceneProvider::SceneProvider(sys::ComponentContext* context) : context_(context) {
  auto scene_provider_config = scene_provider_config_lib::Config::TakeFromStartupHandle();
  use_flatland_ = scene_provider_config.use_flatland();
  use_scene_manager_ = scene_provider_config.use_scene_manager();

  FX_CHECK(!use_flatland_ || use_scene_manager_) << "flatland x root presenter not supported";

  if (use_scene_manager_) {
    context_->svc()->Connect(scene_manager_.NewRequest());
  } else {
    context_->svc()->Connect(root_presenter_.NewRequest());
  }
}

void SceneProvider::AttachClientView(
    fuchsia::ui::test::scene::ControllerAttachClientViewRequest request,
    AttachClientViewCallback callback) {
  FX_LOGS(INFO) << "Attach client view";

  fuchsia::ui::views::ViewRef client_view_ref;

  if (use_scene_manager_) {
    fuchsia::session::scene::Manager_SetRootView_Result set_root_view_result;
    scene_manager_->SetRootView(std::move(*request.mutable_view_provider()), &set_root_view_result);
    FX_CHECK(set_root_view_result.is_response())
        << "Failed to attach client view due to internal error in scene manager";
    client_view_ref = std::move(set_root_view_result.response().view_ref);
  } else {
    auto client_view_tokens = scenic::ViewTokenPair::New();
    auto [client_control_ref, view_ref] = scenic::ViewRefPair::New();
    client_view_ref = fidl::Clone(view_ref);

    root_presenter_->PresentOrReplaceView2(std::move(client_view_tokens.view_holder_token),
                                           fidl::Clone(client_view_ref),
                                           /* presentation */ nullptr);

    auto client_view_provider = request.mutable_view_provider()->Bind();
    client_view_provider->CreateViewWithViewRef(std::move(client_view_tokens.view_token.value),
                                                std::move(client_control_ref), std::move(view_ref));
  }

  callback(fsl::GetKoid(client_view_ref.reference.get()));
}

void SceneProvider::RegisterViewTreeWatcher(
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::ViewTreeWatcher> view_tree_watcher,
    RegisterViewTreeWatcherCallback callback) {
  // Register the client's view tree watcher.
  fuchsia::ui::observation::test::RegistrySyncPtr observer_registry;
  context_->svc()->Connect<fuchsia::ui::observation::test::Registry>(
      observer_registry.NewRequest());
  observer_registry->RegisterGlobalViewTreeWatcher(std::move(view_tree_watcher));

  callback();
}

// TODO(fxbug.dev/112819): Refactor to accommodate Flatland + Geometry
// Observation.
void SceneProvider::PresentView(
    fuchsia::element::ViewSpec view_spec,
    fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller,
    fidl::InterfaceRequest<fuchsia::element::ViewController> view_controller,
    PresentViewCallback callback) {
  if (annotation_controller) {
    annotation_controller_.Bind(std::move(annotation_controller));
  }

  if (view_controller) {
    fake_view_controller_.emplace(std::move(view_controller), [this] { this->DismissView(); });
  }

  // TODO(fxbug.dev/106094): Register client's scoped view tree watcher, if
  // requested.

  // On GFX, |view_spec| will have the `view_ref` and `view_holder_token` fields
  // set. On flatland, it will have the `viewport_creation_token` field set.
  // Any other combination thereof is invalid.
  if (view_spec.has_view_ref() && view_spec.has_view_holder_token()) {
    FX_CHECK(!use_flatland_)
        << "Client attempted to present a view using GFX tokens when flatland is enabled";
    if (use_scene_manager_) {
      fuchsia::session::scene::Manager_PresentRootViewLegacy_Result set_root_view_result;
      scene_manager_->PresentRootViewLegacy(std::move(*view_spec.mutable_view_holder_token()),
                                            std::move(*view_spec.mutable_view_ref()),
                                            &set_root_view_result);
      FX_CHECK(!set_root_view_result.is_err())
          << "Failed to present view due to internal error in scene manager";
    } else {
      root_presenter_->PresentOrReplaceView2(std::move(*view_spec.mutable_view_holder_token()),
                                             std::move(*view_spec.mutable_view_ref()),
                                             /* presentation */ nullptr);
    }
  } else if (view_spec.has_viewport_creation_token()) {
    FX_CHECK(use_flatland_)
        << "Client attempted to present a view using a flatland token when GFX is enabled";

    fuchsia::session::scene::Manager_PresentRootView_Result set_root_view_result;
    scene_manager_->PresentRootView(std::move(*view_spec.mutable_viewport_creation_token()),
                                    &set_root_view_result);
  } else {
    FX_LOGS(FATAL) << "Invalid view spec";
  }

  fuchsia::element::GraphicalPresenter_PresentView_Result result;
  result.set_response({});
  callback(std::move(result));
}

fidl::InterfaceRequestHandler<fuchsia::ui::test::scene::Controller>
SceneProvider::GetSceneControllerHandler() {
  return scene_controller_bindings_.GetHandler(this);
}

fidl::InterfaceRequestHandler<fuchsia::element::GraphicalPresenter>
SceneProvider::GetGraphicalPresenterHandler() {
  return graphical_presenter_bindings_.GetHandler(this);
}

void SceneProvider::DismissView() {
  FX_CHECK(!use_flatland_)
      << "Dismissing views on flatland is not yet supported (fxbug.dev/114431)";

  // Give the scene provider a new ViewHolderToken to drop the existing view.
  auto client_view_tokens = scenic::ViewTokenPair::New();
  auto [client_control_ref, client_view_ref] = scenic::ViewRefPair::New();

  fuchsia::session::scene::Manager_PresentRootViewLegacy_Result set_root_view_result;

  scene_manager_->PresentRootViewLegacy(std::move(client_view_tokens.view_holder_token),
                                        fidl::Clone(client_view_ref), &set_root_view_result);
  FX_CHECK(!set_root_view_result.is_err())
      << "Got a PresentRootViewLegacyError when trying to attach an empty view";
}

}  // namespace ui_testing
