// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/flatland_test_view.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include "fuchsia/ui/app/cpp/fidl.h"

namespace ui_testing {

using fuchsia::ui::composition::ContentId;
using fuchsia::ui::composition::TransformId;

void FlatlandTestView::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  flatland_ = mock_handles_->svc().Connect<fuchsia::ui::composition::Flatland>();
  flatland_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::Flatland: "
                   << zx_status_get_string(status);
  });
  flatland_->SetDebugName("FlatlandTestView");

  // Set up parent watcher to retrieve layout info.
  parent_watcher_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::ParentViewportWatcher: "
                   << zx_status_get_string(status);
  });
  // Create a11y view's ViewRef.
  auto view_identity = scenic::NewViewIdentityOnCreation();
  view_ref_ = fidl::Clone(view_identity.view_ref);

  flatland_->CreateView2(std::move(*args.mutable_view_creation_token()), std::move(view_identity),
                         /* view_bound_protocols = */ {}, parent_watcher_.NewRequest());

  flatland_->CreateTransform(TransformId({.value = kRootTransformId}));
  flatland_->SetRootTransform(TransformId({.value = kRootTransformId}));

  flatland_->CreateTransform(TransformId({.value = kRectangleHolderTransform}));
  flatland_->AddChild(TransformId({.value = kRootTransformId}),
                      TransformId({.value = kRectangleHolderTransform}));

  parent_watcher_->GetLayout([this](fuchsia::ui::composition::LayoutInfo layout_info) {
    layout_info_ = std::move(layout_info);

    DrawContent();
    ResizeChildViewport();
  });
}

void FlatlandTestView::NestChildView() {
  FX_CHECK(!child_view_is_nested);

  child_view_is_nested = true;

  auto child_view_provider = mock_handles_->svc().Connect<fuchsia::ui::app::ViewProvider>();

  auto [child_view_token, child_viewport_token] = scenic::ViewCreationTokenPair::New();

  fuchsia::ui::app::CreateView2Args args;
  args.set_view_creation_token(std::move(child_view_token));
  child_view_provider->CreateView2(std::move(args));

  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size({std::max(1u, width() / 4), std::max(1u, height() / 4)});
  {
    fuchsia::ui::composition::ChildViewWatcherPtr unused_watcher;
    flatland_->CreateViewport(ContentId{.value = kChildViewportContentId},
                              std::move(child_viewport_token), fidl::Clone(viewport_properties),
                              unused_watcher.NewRequest());
  }

  flatland_->CreateTransform(TransformId{.value = kChildViewportTransformId});
  flatland_->SetContent(TransformId{.value = kChildViewportTransformId},
                        ContentId{.value = kChildViewportContentId});
  flatland_->AddChild(TransformId{.value = kRootTransformId},
                      TransformId{.value = kChildViewportTransformId});
  flatland_->SetTranslation(
      TransformId{.value = kChildViewportTransformId},
      {static_cast<int32_t>(width() * 3 / 8), static_cast<int32_t>(height() * 3 / 8)});

  PresentChanges();
}

void FlatlandTestView::ResizeChildViewport() {
  if (!child_view_is_nested) {
    return;
  }

  fuchsia::ui::composition::ViewportProperties viewport_properties;
  viewport_properties.set_logical_size({std::max(1u, width() / 4), std::max(1u, height() / 4)});
  flatland_->SetViewportProperties(ContentId{.value = kChildViewportContentId},
                                   fidl::Clone(viewport_properties));

  flatland_->SetTranslation(
      TransformId{.value = kChildViewportTransformId},
      {static_cast<int32_t>(width() * 3 / 8), static_cast<int32_t>(height() * 3 / 8)});
  PresentChanges();
}

uint32_t FlatlandTestView::width() {
  FX_CHECK(layout_info_);
  return layout_info_->logical_size().width;
}

uint32_t FlatlandTestView::height() {
  FX_CHECK(layout_info_);
  return layout_info_->logical_size().height;
}

void FlatlandTestView::DrawRectangle(int32_t x, int32_t y, int32_t z, uint32_t width,
                                     uint32_t height, uint8_t red, uint8_t green, uint8_t blue,
                                     uint8_t alpha) {
  const ContentId kFilledRectId = {next_resource_id_++};
  const TransformId kTransformId = {next_resource_id_++};

  float red_f = static_cast<float>(red) / 255.f;
  float green_f = static_cast<float>(green) / 255.f;
  float blue_f = static_cast<float>(blue) / 255.f;
  float alpha_f = static_cast<float>(alpha) / 255.f;

  flatland_->CreateFilledRect(kFilledRectId);
  flatland_->SetSolidFill(kFilledRectId, {red_f, green_f, blue_f, alpha_f}, {width, height});

  // Associate the rect with a transform.
  flatland_->CreateTransform(kTransformId);
  flatland_->SetContent(kTransformId, kFilledRectId);
  flatland_->SetTranslation(kTransformId, {x, y});

  // Attach the transform to the view.
  flatland_->AddChild(fuchsia::ui::composition::TransformId{kRectangleHolderTransform},
                      kTransformId);
}

void FlatlandTestView::PresentChanges() {
  flatland_->Present(fuchsia::ui::composition::PresentArgs{});
}

}  // namespace ui_testing
