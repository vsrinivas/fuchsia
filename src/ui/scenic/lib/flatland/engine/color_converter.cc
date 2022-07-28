// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/color_converter.h"

#include "src/ui/scenic/lib/utils/helpers.h"

namespace flatland {

ColorConverter::ColorConverter(sys::ComponentContext* app_context,
                               std::weak_ptr<DisplayCompositor> compositor)
    : ColorConverterImpl(app_context), compositor_(compositor) {}

void ColorConverter::SetValues(fuchsia::ui::display::color::ConversionProperties properties,
                               SetValuesCallback callback) {
  auto compositor = compositor_.lock();
  FX_DCHECK(compositor);

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

  compositor->SetColorConversionValues(coefficients, preoffsets, postoffsets);
  callback(ZX_OK);
}

void ColorConverter::SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCallback callback) {
  auto compositor = compositor_.lock();
  FX_DCHECK(compositor);
  callback(compositor->SetMinimumRgb(minimum_rgb));
}

}  // namespace flatland
