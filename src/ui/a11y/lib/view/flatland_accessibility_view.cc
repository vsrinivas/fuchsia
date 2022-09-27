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

using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::PresentArgs;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;

// IDs for the flatland resources.
constexpr uint64_t kA11yRootTransformId = 1;
constexpr uint64_t kProxyViewportTransformId = 2;
constexpr uint64_t kProxyViewportContentId = 3;
constexpr uint64_t kMagnifierTransformId = 4;

bool InvokeViewPropertiesChangedCallback(
    const LayoutInfo& layout_info,
    const FlatlandAccessibilityView::ViewPropertiesChangedCallback& callback) {
  ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(fidl::Clone(layout_info.logical_size()));
  return callback(viewport_properties);
}

void InvokeViewPropertiesChangedCallbacks(
    const LayoutInfo& layout_info,
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
    : flatland_a11y_(std::move(flatland), /* debug name = */ "a11y") {}

void FlatlandAccessibilityView::CreateView(
    fuchsia::ui::views::ViewCreationToken a11y_view_token,
    fuchsia::ui::views::ViewportCreationToken proxy_viewport_token) {
  FX_LOGS(INFO) << "A11y received `CreateView` request";

  // We can't create the proxy viewport until we receive layout info from
  // scenic, so we'll store the proxy viewport creation token to use later.
  proxy_viewport_token_ = std::move(proxy_viewport_token);

  // PERFORM SETUP THAT DOES NOT REQUIRE LAYOUT INFO.

  // Set up view-bound protocols for flatland instance.
  fuchsia::ui::composition::ViewBoundProtocols view_bound_protocols;
  view_bound_protocols.set_view_focuser(focuser_.NewRequest());

  // Create a11y view's ViewRef.
  auto view_identity = scenic::NewViewIdentityOnCreation();

  // Save a11y view's ViewRef.
  view_ref_ = fidl::Clone(view_identity.view_ref);

  // Create a11y view, and set it as the content for the root transform.
  flatland_a11y_.flatland()->CreateView2(std::move(a11y_view_token), std::move(view_identity),
                                         std::move(view_bound_protocols),
                                         parent_watcher_.NewRequest());

  flatland_a11y_.flatland()->CreateTransform(TransformId({.value = kA11yRootTransformId}));
  flatland_a11y_.flatland()->SetRootTransform(TransformId({.value = kA11yRootTransformId}));

  // Create magnifier transform, and attach as a child of the root transform.
  // Attach proxy viewport transform as a child of magnifier transform.
  flatland_a11y_.flatland()->CreateTransform(TransformId{.value = kMagnifierTransformId});
  flatland_a11y_.flatland()->AddChild(TransformId{.value = kA11yRootTransformId},
                                      TransformId{.value = kMagnifierTransformId});

  // Present changes.
  flatland_a11y_.Present();

  // Wait for layout info to create the proxy viewport.
  WatchLayoutInfo();
}

void FlatlandAccessibilityView::WatchLayoutInfo() {
  // Watch for next layout info change.
  parent_watcher_->GetLayout([this](LayoutInfo layout_info) {
    layout_info_ = std::move(layout_info);

    const auto& logical_size = layout_info_->logical_size();
    FX_LOGS(INFO) << "A11y view received layout info; view has width = " << logical_size.width
                  << ", height = " << logical_size.height;

    // If the proxy viewport already exists, update its properties.
    //
    // Otherwise, if the new layout info is the first we've received, create the proxy
    // viewport.
    if (!proxy_viewport_token_.has_value()) {
      ResizeProxyViewport();
    } else {
      CreateProxyViewport();
    }

    // Report changes in view properties to observers.
    InvokeViewPropertiesChangedCallbacks(*layout_info_, &view_properties_changed_callbacks_);

    // Immediately set another hanging get for layout info.
    WatchLayoutInfo();
  });
}

void FlatlandAccessibilityView::CreateProxyViewport() {
  FX_DCHECK(!is_initialized_);
  FX_DCHECK(proxy_viewport_token_.has_value());
  FX_DCHECK(layout_info_.has_value());

  // Create proxy viewport.
  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(fidl::Clone(layout_info_->logical_size()));
  fuchsia::ui::composition::ChildViewWatcherPtr child_view_watcher;
  flatland_a11y_.flatland()->CreateViewport(
      ContentId{.value = kProxyViewportContentId}, std::move(*proxy_viewport_token_),
      std::move(viewport_properties), child_view_watcher.NewRequest());
  proxy_viewport_token_.reset();

  // Create a proxy viewport transform, and attach as a child of the
  // magnification transform.
  flatland_a11y_.flatland()->CreateTransform(TransformId{.value = kProxyViewportTransformId});
  flatland_a11y_.flatland()->SetContent(TransformId{.value = kProxyViewportTransformId},
                                        ContentId{.value = kProxyViewportContentId});
  flatland_a11y_.flatland()->AddChild(TransformId{.value = kMagnifierTransformId},
                                      TransformId{.value = kProxyViewportTransformId});

  // Consider the scene "ready" once the proxy viewport is attached on the
  // server side.
  flatland_a11y_.Present(PresentArgs{}, [this](auto) {
    is_initialized_ = true;
    InvokeSceneReadyCallbacks(&scene_ready_callbacks_);
  });
}

void FlatlandAccessibilityView::ResizeProxyViewport() {
  FX_DCHECK(layout_info_.has_value());

  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(fidl::Clone(layout_info_->logical_size()));
  flatland_a11y_.flatland()->SetViewportProperties(ContentId{.value = kProxyViewportContentId},
                                                   std::move(viewport_properties));
  flatland_a11y_.Present();
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

void FlatlandAccessibilityView::SetMagnificationTransform(
    float scale, float x, float y, SetMagnificationTransformCallback callback) {
  flatland_a11y_.flatland()->SetScale(
      fuchsia::ui::composition::TransformId{.value = kMagnifierTransformId},
      fuchsia::math::VecF{.x = scale, .y = scale});

  // Translation arguments to this method are normalized, so we need to put them
  // into the coordinate space of the magnifier transform.
  auto translation_x = x * static_cast<float>(layout_info_->logical_size().width) / 2.f;
  auto translation_y = y * static_cast<float>(layout_info_->logical_size().height) / 2.f;
  flatland_a11y_.flatland()->SetTranslation(
      fuchsia::ui::composition::TransformId{.value = kMagnifierTransformId},
      fuchsia::math::Vec{.x = static_cast<int32_t>(translation_x),
                         .y = static_cast<int32_t>(translation_y)});

  flatland_a11y_.Present(fuchsia::ui::composition::PresentArgs{},
                         [callback = std::move(callback)](auto) { callback(); });
}

}  // namespace a11y
