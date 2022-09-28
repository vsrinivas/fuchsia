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

#include <algorithm>
#include <utility>

#include "fuchsia/math/cpp/fidl.h"
#include "fuchsia/ui/composition/cpp/fidl.h"
#include "fuchsia/ui/views/cpp/fidl.h"
#include "lib/fidl/cpp/clone.h"

namespace a11y {
namespace {

using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::PresentArgs;
using fuchsia::ui::composition::TransformId;
using fuchsia::ui::composition::ViewportProperties;

// IDs for the flatland resources.
//
// The final scene topology is:
// a11y view:
//    root transform (11)
//    -->magnifier transform (12)
//       -->highlight view holder transform (13) {content: highlight viewport}
//
// highlight view:
//    highlight root transform (21)
//    -->proxy transform (22) {content: proxy viewport}
//    -->highlight transform [not always attached to the graph!]
//       -->rectangle 1
//       -->rectangle 2
//       -->rectangle 3
//       -->rectangle 4

constexpr uint64_t kA11yRootTransformId = 11;
constexpr uint64_t kMagnifierTransformId = 12;
constexpr uint64_t kHighlightViewportTransformId = 13;
constexpr uint64_t kHighlightViewportContentId = 14;
constexpr uint64_t kHighlightRootTransformId = 21;
constexpr uint64_t kProxyViewportTransformId = 22;
constexpr uint64_t kProxyViewportContentId = 23;

// Setup that does not require LayoutInfo.
fuchsia::ui::views::ViewRef InitialA11yViewSetup(
    fuchsia::ui::composition::Flatland* flatland_a11y,
    fuchsia::ui::views::ViewCreationToken a11y_view_token, fuchsia::ui::views::FocuserPtr& focuser,
    fuchsia::ui::composition::ParentViewportWatcherPtr& parent_watcher) {
  FX_DCHECK(flatland_a11y);

  auto view_identity = scenic::NewViewIdentityOnCreation();
  // Save its ViewRef to return.
  auto view_ref = fidl::Clone(view_identity.view_ref);

  // Set up view-bound protocols for flatland instance.
  fuchsia::ui::composition::ViewBoundProtocols view_bound_protocols;
  view_bound_protocols.set_view_focuser(focuser.NewRequest());

  // Create a11y view, and set it as the content for the root transform.
  flatland_a11y->CreateView2(std::move(a11y_view_token), std::move(view_identity),
                             std::move(view_bound_protocols), parent_watcher.NewRequest());

  flatland_a11y->CreateTransform(TransformId({.value = kA11yRootTransformId}));
  flatland_a11y->SetRootTransform(TransformId({.value = kA11yRootTransformId}));

  // Create magnifier transform, and attach as a child of the root transform.
  // Attach highlight viewport transform as a child of magnifier transform.
  flatland_a11y->CreateTransform(TransformId{.value = kMagnifierTransformId});
  flatland_a11y->AddChild(TransformId{.value = kA11yRootTransformId},
                          TransformId{.value = kMagnifierTransformId});

  return view_ref;
}

void FinishA11yViewSetup(fuchsia::ui::composition::Flatland* flatland_a11y,
                         const fuchsia::math::SizeU& logical_size,
                         fuchsia::ui::views::ViewportCreationToken highlight_viewport_token) {
  FX_DCHECK(flatland_a11y);

  // Create the highlight viewport.
  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(logical_size);
  {
    fuchsia::ui::composition::ChildViewWatcherPtr child_view_watcher;
    flatland_a11y->CreateViewport(
        ContentId{.value = kHighlightViewportContentId}, std::move(highlight_viewport_token),
        fidl::Clone(viewport_properties), child_view_watcher.NewRequest());
  }

  // Set up the highlight viewport transform.
  flatland_a11y->CreateTransform(TransformId{.value = kHighlightViewportTransformId});
  flatland_a11y->SetContent(TransformId{.value = kHighlightViewportTransformId},
                            ContentId{.value = kHighlightViewportContentId});
  flatland_a11y->AddChild(TransformId{.value = kMagnifierTransformId},
                          TransformId{.value = kHighlightViewportTransformId});
}

void HighlightViewSetup(
    fuchsia::ui::composition::Flatland* flatland_highlight,
    const fuchsia::math::SizeU& logical_size,
    fuchsia::ui::views::ViewCreationToken highlight_view_token,
    fuchsia::ui::views::ViewportCreationToken proxy_viewport_token,
    fuchsia::ui::composition::ParentViewportWatcherPtr& highlight_view_watcher) {
  FX_DCHECK(flatland_highlight);

  // Create the highlight view.
  auto view_identity = scenic::NewViewIdentityOnCreation();
  fuchsia::ui::composition::ViewBoundProtocols view_bound_protocols;
  flatland_highlight->CreateView2(std::move(highlight_view_token), std::move(view_identity),
                                  std::move(view_bound_protocols),
                                  highlight_view_watcher.NewRequest());

  // Set up the root transform.
  flatland_highlight->CreateTransform(TransformId({.value = kHighlightRootTransformId}));
  flatland_highlight->SetRootTransform(TransformId({.value = kHighlightRootTransformId}));

  // Create the proxy viewport.
  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(logical_size);

  {
    fuchsia::ui::composition::ChildViewWatcherPtr child_view_watcher;
    flatland_highlight->CreateViewport(
        ContentId{.value = kProxyViewportContentId}, std::move(proxy_viewport_token),
        std::move(viewport_properties), child_view_watcher.NewRequest());
  }

  // Set up the proxy viewport transform.
  flatland_highlight->CreateTransform(TransformId{.value = kProxyViewportTransformId});
  flatland_highlight->SetContent(TransformId{.value = kProxyViewportTransformId},
                                 ContentId{.value = kProxyViewportContentId});
  flatland_highlight->AddChild(TransformId{.value = kHighlightRootTransformId},
                               TransformId{.value = kProxyViewportTransformId});
}

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

FlatlandAccessibilityView::FlatlandAccessibilityView(
    fuchsia::ui::composition::FlatlandPtr flatland1,
    fuchsia::ui::composition::FlatlandPtr flatland2)
    : flatland_a11y_(std::move(flatland1), /* debug name = */ "a11y_view"),
      flatland_highlight_(std::move(flatland2), /* debug name = */ "highlight_view") {}

void FlatlandAccessibilityView::CreateView(
    fuchsia::ui::views::ViewCreationToken a11y_view_token,
    fuchsia::ui::views::ViewportCreationToken proxy_viewport_token) {
  FX_LOGS(INFO) << "A11y received `CreateView` request";

  // We can't create the proxy viewport until we receive layout info from
  // scenic, so we'll store the proxy viewport creation token to use later.
  proxy_viewport_token_ = std::move(proxy_viewport_token);

  a11y_view_ref_ = InitialA11yViewSetup(flatland_a11y_.flatland(), std::move(a11y_view_token),
                                        focuser_, parent_watcher_);

  // Present changes.
  flatland_a11y_.Present();

  // Watch for next layout info change.
  parent_watcher_->GetLayout([this](LayoutInfo layout_info) {
    FX_DCHECK(proxy_viewport_token_.has_value());

    layout_info_ = std::move(layout_info);
    const auto logical_size = layout_info_->logical_size();
    FX_LOGS(INFO) << "A11y view received layout info; view has width = " << logical_size.width
                  << ", height = " << logical_size.height;

    // Create highlight viewport.
    auto [highlight_view_token, highlight_viewport_token] = scenic::ViewCreationTokenPair::New();

    FinishA11yViewSetup(flatland_a11y_.flatland(), logical_size,
                        std::move(highlight_viewport_token));
    fuchsia::ui::composition::ParentViewportWatcherPtr unused_watcher{};
    HighlightViewSetup(flatland_highlight_.flatland(), logical_size,
                       std::move(highlight_view_token), std::move(proxy_viewport_token_.value()),
                       unused_watcher);
    proxy_viewport_token_.reset();

    // Make sure the highlight view is ready before presenting the a11y view.
    // Probably not necessary, but it might help avoid a flicker at startup.
    flatland_highlight_.Present(PresentArgs{}, [this](auto) {
      flatland_a11y_.Present(PresentArgs{}, [this](auto) {
        is_initialized_ = true;
        InvokeSceneReadyCallbacks(&scene_ready_callbacks_);
      });
    });

    // Report changes in view properties to observers.
    InvokeViewPropertiesChangedCallbacks(*layout_info_, &view_properties_changed_callbacks_);

    // Watch for further resizes of the parent viewport.
    WatchForResizes();
  });
}

void FlatlandAccessibilityView::WatchForResizes() {
  // Watch for next layout info change.
  parent_watcher_->GetLayout([this](LayoutInfo layout_info) {
    layout_info_ = std::move(layout_info);

    const auto logical_size = layout_info_->logical_size();
    FX_LOGS(INFO) << "A11y view received layout info; view has width = " << logical_size.width
                  << ", height = " << logical_size.height;

    ResizeViewports(logical_size);

    // Report changes in view properties to observers.
    InvokeViewPropertiesChangedCallbacks(*layout_info_, &view_properties_changed_callbacks_);

    WatchForResizes();
  });
}

void FlatlandAccessibilityView::ResizeViewports(fuchsia::math::SizeU logical_size) {
  FX_DCHECK(layout_info_.has_value());

  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size(logical_size);

  flatland_a11y_.flatland()->SetViewportProperties(ContentId{.value = kHighlightViewportContentId},
                                                   fidl::Clone(viewport_properties));
  flatland_highlight_.flatland()->SetViewportProperties(ContentId{.value = kProxyViewportContentId},
                                                        std::move(viewport_properties));

  flatland_a11y_.Present();
  flatland_highlight_.Present();
}

std::optional<fuchsia::ui::views::ViewRef> FlatlandAccessibilityView::view_ref() {
  if (!a11y_view_ref_) {
    return std::nullopt;
  }
  fuchsia::ui::views::ViewRef copy;
  fidl::Clone(*a11y_view_ref_, &copy);
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
