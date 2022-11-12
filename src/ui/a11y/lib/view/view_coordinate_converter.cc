// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_coordinate_converter.h"

#include <fuchsia/ui/observation/scope/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace a11y {

ViewCoordinateConverter::ViewCoordinateConverter(
    fuchsia::ui::observation::scope::RegistryPtr registry, zx_koid_t context_view_ref_koid)
    : context_view_ref_koid_(context_view_ref_koid) {
  registry.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::observation::scope::Registry: "
                   << zx_status_get_string(status);
  });

  registry->RegisterScopedViewTreeWatcher(context_view_ref_koid_, watcher_.NewRequest(), []() {});
  watcher_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::observation::geometry::ViewTreeWatcher: "
                   << zx_status_get_string(status);
  });

  Watch();
}

void ViewCoordinateConverter::ProcessResponse(
    fuchsia::ui::observation::geometry::WatchResponse response) {
  if (response.has_error() || !response.has_updates() || response.updates().empty()) {
    // For now, a11y does not care about the possible errors here and makes a best effort to receive
    // updated values.
    return;
  }

  // We only care about the most recent snapshot, so access the last value.
  const auto& view_tree_snapshot = response.updates().back();
  FX_DCHECK(view_tree_snapshot.has_views());
  for (const auto& view : view_tree_snapshot.views()) {
    ViewData& view_data = view_transforms_[view.view_ref_koid()];
    view_data.origin_in_context = {view.extent_in_context().origin.x,
                                   view.extent_in_context().origin.y};
    view_data.origin = {view.layout().extent.min.x, view.layout().extent.min.y};
    view_data.angle = view.extent_in_context().angle_degrees;
    const float view_width = std::abs(view.layout().extent.max.x - view.layout().extent.min.x);
    const float view_height = std::abs(view.layout().extent.max.y - view.layout().extent.min.y);

    // We have to do this computation instead of reading the scale that the geometry watcher
    // reports, because that scale is not relative to the context view.
    view_data.x_scale = view.extent_in_context().width / view_width;
    view_data.y_scale = view.extent_in_context().height / view_height;
  }

  for (auto& callback : callbacks_) {
    callback();
  }
}

std::optional<fuchsia::math::PointF> ViewCoordinateConverter::Convert(
    zx_koid_t view_ref_koid, fuchsia::math::PointF coordinate) const {
  const auto it = view_transforms_.find(view_ref_koid);
  if (it == view_transforms_.end()) {
    return std::nullopt;
  }

  if (view_ref_koid == context_view_ref_koid_) {
    return coordinate;
  }

  const auto& view_data = it->second;

  // First, compute an offset of the coordinate to its origin, and convert this offset into context
  // view scale.
  const float x_offset = view_data.x_scale * (coordinate.x - view_data.origin.x);
  const float y_offset = view_data.y_scale * (coordinate.y - view_data.origin.y);

  fuchsia::math::PointF context_point;
  if (view_data.angle == 0.0) {
    context_point.x = view_data.origin_in_context.x + x_offset;
    context_point.y = view_data.origin_in_context.y + y_offset;
    return context_point;
  } else if (view_data.angle == 90.0) {
    context_point.x = view_data.origin_in_context.x + y_offset;
    context_point.y = view_data.origin_in_context.y - x_offset;
    return context_point;
  } else if (view_data.angle == 180.0) {
    context_point.x = view_data.origin_in_context.x - x_offset;
    context_point.y = view_data.origin_in_context.y - y_offset;
    return context_point;
  } else if (view_data.angle == 270.0) {
    context_point.x = view_data.origin_in_context.x - y_offset;
    context_point.y = view_data.origin_in_context.y + x_offset;
    return context_point;
  }

  return std::nullopt;
}

void ViewCoordinateConverter::Watch() {
  watcher_->Watch([this](fuchsia::ui::observation::geometry::WatchResponse response) {
    ProcessResponse(std::move(response));
    Watch();
  });
}

void ViewCoordinateConverter::RegisterCallback(OnGeometryChangeCallback callback) {
  callbacks_.push_back(std::move(callback));
}

}  // namespace a11y
