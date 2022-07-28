// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_ENGINE_COLOR_CONVERTER_H_
#define SRC_UI_SCENIC_LIB_GFX_ENGINE_COLOR_CONVERTER_H_

#include <lib/sys/cpp/component_context.h>

#include "src/ui/scenic/lib/display/color_converter.h"
#include "src/ui/scenic/lib/gfx/engine/scene_graph.h"

namespace scenic_impl::gfx {

class ColorConverter : public display::ColorConverterImpl {
 public:
  ColorConverter(sys::ComponentContext* app_context, SceneGraphWeakPtr scene_graph);
  ~ColorConverter() override {}

  // |fuchsia::ui::display::color::Converter|
  void SetValues(fuchsia::ui::display::color::ConversionProperties properties,
                 SetValuesCallback callback) override;

  // |fuchsia::ui::display::color::Converter|
  void SetMinimumRgb(uint8_t minimum_rgb, SetMinimumRgbCallback callback) override;

 private:
  SceneGraphWeakPtr scene_graph_;
};

}  // namespace scenic_impl::gfx

#endif  // SRC_UI_SCENIC_LIB_GFX_ENGINE_COLOR_CONVERTER_H_
