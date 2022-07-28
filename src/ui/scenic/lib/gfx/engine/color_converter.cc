// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/color_converter.h"

namespace scenic_impl::gfx {

ColorConverter::ColorConverter(sys::ComponentContext* app_context, SceneGraphWeakPtr scene_graph)
    : ColorConverterImpl(app_context), scene_graph_(scene_graph) {}

void ColorConverter::SetValues(fuchsia::ui::display::color::ConversionProperties properties,
                               SetValuesCallback callback) {
  auto coefficients = properties.has_coefficients()
                          ? properties.coefficients()
                          : std::array<float, 9>{1, 0, 0, 0, 1, 0, 0, 0, 1};
  auto preoffsets =
      properties.has_preoffsets() ? properties.preoffsets() : std::array<float, 3>{0, 0, 0};
  auto postoffsets =
      properties.has_postoffsets() ? properties.postoffsets() : std::array<float, 3>{0, 0, 0};

  auto print_parameters = [&coefficients, &preoffsets, &postoffsets]() {
    const std::string& coefficients_str = utils::GetArrayString("Coefficients", coefficients);
    const std::string& preoffsets_str = utils::GetArrayString("Preoffsets", preoffsets);
    const std::string& postoffsets_str = utils::GetArrayString("Postoffsets", postoffsets);
    FX_LOGS(ERROR) << "Invalid Color Conversion Parameter Values: \n"
                   << coefficients_str << preoffsets_str << postoffsets_str;
  };

  for (auto val : coefficients) {
    if (isinf(val) || isnan(val)) {
      print_parameters();
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  for (auto val : preoffsets) {
    if (isinf(val) || isnan(val)) {
      print_parameters();
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  for (auto val : postoffsets) {
    if (isinf(val) || isnan(val)) {
      print_parameters();
      callback(ZX_ERR_INVALID_ARGS);
      return;
    }
  }

  bool success = true;
  for (auto compositor : scene_graph_->compositors()) {
    if (auto swapchain = compositor->swapchain()) {
      ColorTransform transform;
      transform.preoffsets = preoffsets;
      transform.matrix = coefficients;
      transform.postoffsets = postoffsets;
      success &= swapchain->SetDisplayColorConversion(transform);
      FX_DCHECK(success);
    }
  }

  zx_status_t result = success ? ZX_OK : ZX_ERR_INTERNAL;
  callback(result);
}

void ColorConverter::SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCallback callback) {
  bool success = true;
  for (auto compositor : scene_graph_->compositors()) {
    if (auto swapchain = compositor->swapchain()) {
      success &= swapchain->SetMinimumRgb(minimum_rgb);
      FX_DCHECK(success);
    }
  }

  callback(success);
}

}  // namespace scenic_impl::gfx
