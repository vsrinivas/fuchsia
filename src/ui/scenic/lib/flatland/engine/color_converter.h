// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERTER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERTER_H_

#include <lib/sys/cpp/component_context.h>

#include "src/ui/scenic/lib/display/color_converter.h"
#include "src/ui/scenic/lib/flatland/engine/display_compositor.h"

namespace flatland {

class ColorConverter : public scenic_impl::display::ColorConverterImpl {
 public:
  ColorConverter(sys::ComponentContext* app_context, std::weak_ptr<DisplayCompositor> compositor);
  ~ColorConverter() override {}

  // |fuchsia::ui::display::color::Converter|
  void SetValues(fuchsia::ui::display::color::ConversionProperties properties,
                 SetValuesCallback callback) override;

  // |fuchsia::ui::display::color::Converter|
  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCallback callback) override;

 private:
  std::weak_ptr<DisplayCompositor> compositor_;
};

}  // namespace flatland

#endif  //  SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_COLOR_CONVERTER_H_
