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
  FX_CHECK(config.display_rotation % 90 == 0);
  display_rotation_ = config.display_rotation;
}

component_testing::Realm UITestManager::AddSubrealm() { return realm_.AddSubrealm(); }

void UITestManager::BuildRealm() {
  realm_.Build();

  // Get the display information using the |fuchsia.ui.display.singleton.Info|.
  fuchsia::ui::display::singleton::Metrics info;
  fuchsia::ui::display::singleton::InfoSyncPtr display_info =
      realm_.realm_root()->ConnectSync<fuchsia::ui::display::singleton::Info>();
  auto status = display_info->GetMetrics(&info);

  FX_DCHECK(status == ZX_OK);

  display_width_ = info.extent_in_px().width;
  display_height_ = info.extent_in_px().height;

  // Connect screenshotter.
  screenshotter_ = realm_.realm_root()->ConnectSync<fuchsia::ui::composition::Screenshot>();

  // Register focus chain listener.
  auto focus_chain_listener_registry =
      realm_.realm_root()->Connect<fuchsia::ui::focus::FocusChainListenerRegistry>();
  focus_chain_listener_registry->Register(focus_chain_listener_binding_.NewBinding());

  // Register geometry observer.
  if (realm_.config().scene_owner) {
    scene_controller_ = realm_.realm_root()->Connect<fuchsia::ui::test::scene::Controller>();
    scene_controller_->RegisterViewTreeWatcher(view_tree_watcher_.NewRequest(), []() {});
  } else {
    realm_.realm_root()->Connect<fuchsia::ui::observation::test::Registry>(
        observer_registry_.NewRequest());
    observer_registry_->RegisterGlobalViewTreeWatcher(view_tree_watcher_.NewRequest());
  }

  view_tree_watcher_.set_error_handler(
      [](auto) { FX_LOGS(ERROR) << "Received error on view tree watcher channel"; });

  // Watch view geometry.
  Watch();
}

std::unique_ptr<sys::ServiceDirectory> UITestManager::CloneExposedServicesDirectory() {
  return realm_.CloneExposedServicesDirectory();
}

void UITestManager::InitializeScene() {
  // Use scene provider helper component to attach client view to the scene.
  fuchsia::ui::test::scene::ControllerAttachClientViewRequest request;
  request.set_view_provider(realm_.realm_root()->Connect<fuchsia::ui::app::ViewProvider>());
  scene_controller_->AttachClientView(std::move(request), [this](zx_koid_t client_view_ref_koid) {
    client_view_ref_koid_ = client_view_ref_koid;
  });
}

std::pair<uint64_t, uint64_t> UITestManager::GetDisplayDimensions() const {
  return std::make_pair(display_width_, display_height_);
}

Screenshot UITestManager::TakeScreenshot() const {
  fuchsia::ui::composition::ScreenshotTakeRequest request;
  request.set_format(fuchsia::ui::composition::ScreenshotFormat::BGRA_RAW);

  fuchsia::ui::composition::ScreenshotTakeResponse response;
  auto status = screenshotter_->Take(std::move(request), &response);

  FX_DCHECK(status == ZX_OK);

  return Screenshot(response.vmo(), display_width_, display_height_, display_rotation_);
}

void UITestManager::Watch() {
  FX_CHECK(view_tree_watcher_) << "View Tree watcher must be registered before calling Watch()";

  view_tree_watcher_->Watch([this](auto response) {
    if (!response.has_error()) {
      std::vector<fuchsia::ui::observation::geometry::ViewTreeSnapshot>* updates =
          response.mutable_updates();
      if (updates && !updates->empty()) {
        last_view_tree_snapshot_ = std::move(updates->back());
      }

      Watch();
      return;
    }

    const auto& error = response.error();

    if (error | fuchsia::ui::observation::geometry::Error::CHANNEL_OVERFLOW) {
      FX_LOGS(DEBUG) << "View Tree watcher channel overflowed";
    } else if (error | fuchsia::ui::observation::geometry::Error::BUFFER_OVERFLOW) {
      FX_LOGS(DEBUG) << "View Tree watcher buffer overflowed";
    } else if (error | fuchsia::ui::observation::geometry::Error::VIEWS_OVERFLOW) {
      // This one indicates some possible data loss, so we log with a high severity.
      FX_LOGS(WARNING) << "View Tree watcher attempted to report too many views";
    }
    Watch();
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
  return client_view_ref_koid_ && ViewIsFocused(*client_view_ref_koid_);
}

bool UITestManager::ViewIsFocused(zx_koid_t view_ref_koid) {
  if (!last_focus_chain_) {
    return false;
  }

  if (!last_focus_chain_->has_focus_chain()) {
    return false;
  }

  if (last_focus_chain_->focus_chain().empty()) {
    return false;
  }

  return fsl::GetKoid(last_focus_chain_->focus_chain().back().reference.get()) == view_ref_koid;
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
