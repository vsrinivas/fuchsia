// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_CONFIGURATION_COLOR_TRANSFORM_MANAGER_H_
#define SRC_UI_A11Y_LIB_CONFIGURATION_COLOR_TRANSFORM_MANAGER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <math.h>

#include <array>

#include <src/lib/fxl/macros.h>

namespace a11y {

class ColorTransformManager : public fuchsia::accessibility::ColorTransform {
 public:
  explicit ColorTransformManager(sys::ComponentContext* startup_context);

  ~ColorTransformManager() = default;

  // Registers a color transform handler to receive updates about color correction and inversion
  // settings changes. Only one color transform handler at a time is supported.
  void RegisterColorTransformHandler(
      fidl::InterfaceHandle<fuchsia::accessibility::ColorTransformHandler> handle);

  // Called to actually change the color transform settings in the system.
  void ChangeColorTransform(bool color_inversion_enabled,
                            fuchsia::accessibility::ColorCorrectionMode color_correction_mode);

 private:
  fidl::BindingSet<fuchsia::accessibility::ColorTransform> bindings_;

  // Note that for now, this class supports exactly one color transform handler.
  fuchsia::accessibility::ColorTransformHandlerPtr color_transform_handler_ptr_;
};
} // namespace a11y

#endif  // SRC_UI_A11Y_LIB_CONFIGURATION_COLOR_TRANSFORM_MANAGER_H_
