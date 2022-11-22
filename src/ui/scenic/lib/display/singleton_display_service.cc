// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/singleton_display_service.h"

namespace scenic_impl {
namespace display {

SingletonDisplayService::SingletonDisplayService(std::shared_ptr<display::Display> display)
    : display_(std::move(display)) {}

void SingletonDisplayService::GetMetrics(
    fuchsia::ui::display::singleton::Info::GetMetricsCallback callback) {
  const glm::vec2 dpr = display_->device_pixel_ratio();
  if (dpr.x != dpr.y) {
    FX_LOGS(WARNING) << "SingletonDisplayService::GetMetrics(): x/y display pixel ratio mismatch ("
                     << dpr.x << " vs. " << dpr.y << ")";
  }

  auto metrics = ::fuchsia::ui::display::singleton::Metrics();
  metrics.set_extent_in_px(
      ::fuchsia::math::SizeU{.width = display_->width_in_px(), .height = display_->height_in_px()});
  metrics.set_extent_in_mm(
      ::fuchsia::math::SizeU{.width = display_->width_in_mm(), .height = display_->height_in_mm()});
  metrics.set_recommended_device_pixel_ratio(::fuchsia::math::VecF{.x = dpr.x, .y = dpr.y});

  callback(std::move(metrics));
}

void SingletonDisplayService::AddPublicService(sys::OutgoingDirectory* outgoing_directory) {
  FX_DCHECK(outgoing_directory);
  outgoing_directory->AddPublicService(bindings_.GetHandler(this));
}

}  // namespace display
}  // namespace scenic_impl
