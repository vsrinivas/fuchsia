// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/ui_test_manager/ui_test_manager.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

namespace {

constexpr auto kDefaultScale = 0.f;

std::optional<fuchsia::ui::observation::geometry::ViewDescriptor> ViewDescriptorFromSnapshot(
    const fuchsia::ui::observation::geometry::ViewTreeSnapshot& snapshot, zx_koid_t view_ref_koid) {
  if (!snapshot.has_views()) {
    return std::nullopt;
  }

  auto it = std::find_if(snapshot.views().begin(), snapshot.views().end(),
                         [view_ref_koid](const auto& view) {
                           return view.has_view_ref_koid() && view.view_ref_koid() == view_ref_koid;
                         });
  if (it == snapshot.views().end()) {
    return std::nullopt;
  }

  return fidl::Clone(*it);
}

}  // namespace

UITestManager::UITestManager(UITestRealm::Config config)
    : realm_(config), focus_chain_listener_binding_(this) {
  // Save the scene owner as a workaround for fxbug.dev/103985. We can't use
  // scene provider with web-semantics-test reliably yet, so for now, we force
  // UITestManager to use the raw scene manager / root presenter APIs for that
  // test. In order to choose the correct API, UITestManager needs to know which
  // scene owner the test realm is configured to use.
  //
  // TODO(fxbug.dev/103985): Remove once web-semantics-test runs reliably with
  // scene provider.
  scene_owner_ = config.scene_owner;
}

component_testing::Realm UITestManager::AddSubrealm() { return realm_.AddSubrealm(); }

void UITestManager::BuildRealm() { realm_.Build(); }

std::unique_ptr<sys::ServiceDirectory> UITestManager::CloneExposedServicesDirectory() {
  return realm_.CloneExposedServicesDirectory();
}

void UITestManager::InitializeScene(bool use_scene_provider) {
  FX_CHECK(!geometry_provider_) << "InitializeScene() called twice";

  // Register focus chain listener.
  auto focus_chain_listener_registry =
      realm_.realm_root()->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry->Register(focus_chain_listener_binding_.NewBinding());

  // TODO(fxbug.dev/103985): Remove the use_scene_provider == false code path
  // once we stabilize web-semantics-test.
  if (use_scene_provider) {
    // Use scene provider helper component to attach client view to the scene.
    fuchsia::ui::test::scene::ProviderAttachClientViewRequest request;
    request.set_view_provider(realm_.realm_root()->Connect<fuchsia::ui::app::ViewProvider>());
    scene_provider_ = realm_.realm_root()->Connect<fuchsia::ui::test::scene::Provider>();
    scene_provider_->RegisterGeometryObserver(geometry_provider_.NewRequest(), []() {});
    scene_provider_->AttachClientView(std::move(request), [this](zx_koid_t client_view_ref_koid) {
      client_view_ref_koid_ = client_view_ref_koid;
    });
  } else {
    // Register geometry observer. We should do this before attaching the client
    // view, so that we see all the view tree snapshots.
    realm_.realm_root()->Connect<fuchsia::ui::observation::test::Registry>(
        observer_registry_.NewRequest());
    observer_registry_->RegisterGlobalGeometryProvider(geometry_provider_.NewRequest());

    if (scene_owner_ == UITestRealm::SceneOwnerType::ROOT_PRESENTER) {
      root_presenter_ = realm_.realm_root()->Connect<fuchsia::ui::policy::Presenter>();

      auto client_view_tokens = scenic::ViewTokenPair::New();
      auto [client_control_ref, client_view_ref] = scenic::ViewRefPair::New();
      client_view_ref_koid_ = fsl::GetKoid(client_view_ref.reference.get());

      root_presenter_->PresentOrReplaceView2(std::move(client_view_tokens.view_holder_token),
                                             fidl::Clone(client_view_ref),
                                             /* presentation */ nullptr);

      auto client_view_provider = realm_.realm_root()->Connect<fuchsia::ui::app::ViewProvider>();
      client_view_provider->CreateViewWithViewRef(std::move(client_view_tokens.view_token.value),
                                                  std::move(client_control_ref),
                                                  std::move(client_view_ref));
    } else if (scene_owner_ == UITestRealm::SceneOwnerType::SCENE_MANAGER) {
      scene_manager_ = realm_.realm_root()->Connect<fuchsia::session::scene::Manager>();
      auto view_provider = realm_.realm_root()->Connect<fuchsia::ui::app::ViewProvider>();
      scene_manager_->SetRootView(
          std::move(view_provider),
          [this](fuchsia::session::scene::Manager_SetRootView_Result result) {
            FX_CHECK(result.is_response());
            client_view_ref_koid_ = fsl::GetKoid(result.response().view_ref.reference.get());
          });
    } else {
      FX_LOGS(FATAL) << "Unsupported scene owner";
    }
  }

  WatchViewTree();
}

void UITestManager::WatchViewTree() {
  FX_CHECK(geometry_provider_)
      << "Geometry observer must be registered before calling WatchViewTree()";

  geometry_provider_->Watch([this](auto response) {
    if (!response.has_error()) {
      std::vector<fuchsia::ui::observation::geometry::ViewTreeSnapshot>* updates =
          response.mutable_updates();
      if (updates && !updates->empty()) {
        last_view_tree_snapshot_ = std::move(updates->back());
      }

      WatchViewTree();
      return;
    }

    const auto& error = response.error();

    if (error.has_channel_overflow() && error.channel_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider channel overflowed";
    } else if (error.has_buffer_overflow() && error.buffer_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider buffer overflowed";
    } else if (error.has_views_overflow() && error.views_overflow()) {
      FX_LOGS(FATAL) << "Geometry provider attempted to report too many views";
    }
  });
}

void UITestManager::OnFocusChange(fuchsia::ui::focus::FocusChain focus_chain,
                                  OnFocusChangeCallback callback) {
  last_focus_chain_ = std::move(focus_chain);
  callback();
}

bool UITestManager::ViewIsRendering(zx_koid_t view_ref_koid) {
  if (!last_view_tree_snapshot_) {
    return false;
  }

  return FindViewFromSnapshotByKoid(view_ref_koid) != std::nullopt;
}

std::optional<fuchsia::ui::observation::geometry::ViewDescriptor>
UITestManager::FindViewFromSnapshotByKoid(zx_koid_t view_ref_koid) {
  return ViewDescriptorFromSnapshot(*last_view_tree_snapshot_, view_ref_koid);
}

bool UITestManager::ClientViewIsRendering() {
  if (!client_view_ref_koid_) {
    return false;
  }

  return ViewIsRendering(*client_view_ref_koid_);
}

bool UITestManager::ClientViewIsFocused() {
  if (!last_focus_chain_ || !client_view_ref_koid_) {
    return false;
  }

  if (!last_focus_chain_->has_focus_chain()) {
    return false;
  }

  if (last_focus_chain_->focus_chain().empty()) {
    return false;
  }

  return fsl::GetKoid(last_focus_chain_->focus_chain().back().reference.get()) ==
         client_view_ref_koid_;
}

float UITestManager::ClientViewScaleFactor() {
  if (!last_view_tree_snapshot_ || !client_view_ref_koid_) {
    return kDefaultScale;
  }

  const auto client_view_descriptor = FindViewFromSnapshotByKoid(*client_view_ref_koid_);

  if (!client_view_descriptor || !client_view_descriptor->has_layout()) {
    return kDefaultScale;
  }

  const auto& pixel_scale = client_view_descriptor->layout().pixel_scale;

  return std::max(pixel_scale[0], pixel_scale[1]);
}

std::optional<zx_koid_t> UITestManager::ClientViewRefKoid() { return client_view_ref_koid_; }

}  // namespace ui_testing
