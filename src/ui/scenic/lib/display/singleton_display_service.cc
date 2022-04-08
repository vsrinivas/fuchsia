// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/singleton_display_service.h"

#include "src/ui/bin/root_presenter/displays/display_configuration.h"
#include "src/ui/bin/root_presenter/displays/display_model.h"

namespace scenic_impl {
namespace display {

SingletonDisplayService::SingletonDisplayService(std::shared_ptr<display::Display> display)
    : display_(std::move(display)) {}

void SingletonDisplayService::GetMetrics(
    fuchsia::ui::display::singleton::Info::GetMetricsCallback callback) {
  root_presenter::DisplayModel display_model;
  root_presenter::display_configuration::InitializeModelForDisplay(
      display_->width_in_px(), display_->height_in_px(), &display_model);

  auto display_metrics = display_model.GetMetrics();
  const float x_dpr = display_metrics.x_scale_in_px_per_pp();
  const float y_dpr = display_metrics.y_scale_in_px_per_pp();
  if (x_dpr != y_dpr) {
    FX_LOGS(WARNING) << "SingletonDisplayService::GetMetrics(): x/y display pixel ratio mismatch ("
                     << x_dpr << " vs. " << y_dpr << ") using: " << x_dpr;
  }

  auto metrics = ::fuchsia::ui::display::singleton::Metrics();

  metrics.set_extent_in_px(
      ::fuchsia::math::SizeU{.width = display_->width_in_px(), .height = display_->height_in_px()});
  metrics.set_extent_in_mm(
      ::fuchsia::math::SizeU{.width = display_->width_in_mm(), .height = display_->height_in_mm()});
  metrics.set_recommended_device_pixel_ratio(::fuchsia::math::VecF{.x = x_dpr, .y = y_dpr});

  callback(std::move(metrics));
}

void SingletonDisplayService::AddPublicService(sys::OutgoingDirectory* outgoing_directory) {
  FX_DCHECK(outgoing_directory);
  outgoing_directory->AddPublicService(bindings_.GetHandler(this));
}

}  // namespace display
}  // namespace scenic_impl
