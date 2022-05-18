// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/flatland_accessibility_view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <zircon/status.h>

#include <utility>

namespace a11y {
namespace {

// IDs for the flatland resources.
constexpr uint64_t kRootTransformId = 1;
constexpr uint64_t kProxyViewportTransformId = 2;
constexpr uint64_t kProxyViewportContentId = 3;

bool InvokeViewPropertiesChangedCallback(
    const fuchsia::ui::composition::LayoutInfo& layout_info,
    const FlatlandAccessibilityView::ViewPropertiesChangedCallback& callback) {
  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(fidl::Clone(layout_info.logical_size()));
  return callback(std::move(viewport_properties));
}

void InvokeViewPropertiesChangedCallbacks(
    const fuchsia::ui::composition::LayoutInfo& layout_info,
    std::vector<FlatlandAccessibilityView::ViewPropertiesChangedCallback>* callbacks) {
  auto it = callbacks->begin();
  while (it != callbacks->end()) {
    if (InvokeViewPropertiesChangedCallback(layout_info, *it)) {
      it++;
    } else {
      it = callbacks->erase(it);
    }
  }
}

void InvokeSceneReadyCallbacks(
    std::vector<FlatlandAccessibilityView::SceneReadyCallback>* callbacks) {
  auto it = callbacks->begin();
  while (it != callbacks->end()) {
    if ((*it)()) {
      it++;
    } else {
      it = callbacks->erase(it);
    }
  }
}

}  // namespace

FlatlandAccessibilityView::FlatlandAccessibilityView(fuchsia::ui::composition::FlatlandPtr flatland)
    : flatland_(std::move(flatland)) {
  flatland_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::Flatland: "
                   << zx_status_get_string(status);
  });
  flatland_->SetDebugName("a11y-view");
}

void FlatlandAccessibilityView::CreateView(
    fuchsia::ui::views::ViewCreationToken a11y_view_token,
    fuchsia::ui::views::ViewportCreationToken proxy_viewport_token) {
  FX_LOGS(INFO) << "A11y received `CreateView` request";

  // Set up view-bound protocols for flatland instance.
  fuchsia::ui::composition::ViewBoundProtocols view_bound_protocols;
  view_bound_protocols.set_view_focuser(focuser_.NewRequest());

  // Create a11y view's ViewRef.
  auto view_identity = scenic::NewViewIdentityOnCreation();

  // Set up parent watcher to retrieve layout info.
  parent_watcher_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::ParentViewportWatcher: "
                   << zx_status_get_string(status);
  });

  // Create a11y view, and set it as the content for the root transform.
  flatland_->CreateView2(std::move(a11y_view_token), std::move(view_identity),
                         std::move(view_bound_protocols), parent_watcher_.NewRequest());

  flatland_->CreateTransform(fuchsia::ui::composition::TransformId({.value = kRootTransformId}));
  flatland_->SetRootTransform(fuchsia::ui::composition::TransformId({.value = kRootTransformId}));

  // Get layout info for the a11y view, which is required to build the proxy
  // viewport.
  parent_watcher_->GetLayout([this, proxy_viewport_token = std::move(proxy_viewport_token)](
                                 fuchsia::ui::composition::LayoutInfo layout_info) mutable {
    FX_LOGS(INFO) << "A11y view received layout info";
    layout_info_ = std::move(layout_info);

    InvokeViewPropertiesChangedCallbacks(*layout_info_, &view_properties_changed_callbacks_);

    // Create proxy viewport.
    fuchsia::ui::composition::ViewportProperties viewport_properties;
    viewport_properties.set_logical_size(fidl::Clone(layout_info_->logical_size()));
    fuchsia::ui::composition::ChildViewWatcherPtr child_view_watcher;
    flatland_->CreateViewport(fuchsia::ui::composition::ContentId{.value = kProxyViewportContentId},
                              std::move(proxy_viewport_token), std::move(viewport_properties),
                              child_view_watcher.NewRequest());

    // Create a proxy viewport transform, and attach as a child of the root
    // transform.
    flatland_->CreateTransform(
        fuchsia::ui::composition::TransformId{.value = kProxyViewportTransformId});
    flatland_->AddChild(fuchsia::ui::composition::TransformId{.value = kRootTransformId},
                        fuchsia::ui::composition::TransformId{.value = kProxyViewportTransformId});
    flatland_->SetContent(fuchsia::ui::composition::TransformId{.value = kProxyViewportTransformId},
                          fuchsia::ui::composition::ContentId{.value = kProxyViewportContentId});

    // Present changes.
    flatland_->Present(fuchsia::ui::composition::PresentArgs{});

    // We can consider the scene initialized once the a11y view is attached.
    is_initialized_ = true;
    InvokeSceneReadyCallbacks(&scene_ready_callbacks_);
  });
}

std::optional<fuchsia::ui::views::ViewRef> FlatlandAccessibilityView::view_ref() {
  if (!view_ref_) {
    return std::nullopt;
  }
  fuchsia::ui::views::ViewRef copy;
  fidl::Clone(*view_ref_, &copy);
  return std::move(copy);
}

void FlatlandAccessibilityView::add_view_properties_changed_callback(
    ViewPropertiesChangedCallback callback) {
  view_properties_changed_callbacks_.push_back(std::move(callback));
  if (layout_info_) {
    InvokeViewPropertiesChangedCallback(*layout_info_, view_properties_changed_callbacks_.back());
  }
}

void FlatlandAccessibilityView::add_scene_ready_callback(SceneReadyCallback callback) {
  scene_ready_callbacks_.push_back(std::move(callback));
  if (is_initialized_) {
    scene_ready_callbacks_.back()();
  }
}

void FlatlandAccessibilityView::RequestFocus(fuchsia::ui::views::ViewRef view_ref,
                                             RequestFocusCallback callback) {
  FX_DCHECK(focuser_);
  focuser_->RequestFocus(std::move(view_ref), std::move(callback));
}

fidl::InterfaceRequestHandler<fuchsia::accessibility::scene::Provider>
FlatlandAccessibilityView::GetHandler() {
  return view_bindings_.GetHandler(this);
}

}  // namespace a11y
