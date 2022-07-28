// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_COLOR_CONVERTER_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_COLOR_CONVERTER_H_

#include <fuchsia/ui/display/color/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <sdk/lib/syslog/cpp/macros.h>

namespace scenic_impl {
namespace display {

// Backend for the ColorConverter FIDL interface. This class is just an abstract
// class, since we have multiple implementations for GFX and Flatland.
class ColorConverterImpl : public fuchsia::ui::display::color::Converter {
 protected:
  ColorConverterImpl(sys::ComponentContext* app_context) {
    FX_DCHECK(app_context);
    app_context->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  fidl::BindingSet<fuchsia::ui::display::color::Converter> bindings_;
};

}  // namespace display
}  // namespace scenic_impl

#endif  //  SRC_UI_SCENIC_LIB_DISPLAY_COLOR_CONVERTER_H_
