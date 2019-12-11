// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/id.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <memory>

namespace root_presenter {
// Color Transform Handler is responsible for translating color transform requests into Scenic
// commands to change the display's color transform. It is currently stateless and simply passes
// through requested changes.
class ColorTransformHandler : public fuchsia::accessibility::ColorTransformHandler {
 public:
  explicit ColorTransformHandler(sys::ComponentContext& component_context,
                                 scenic::ResourceId compositor_id, scenic::Session* session);

  ~ColorTransformHandler() = default;

  // SetColorTransformConfiguration is called (typically by Accessibility Manager) to request a
  // change in color transform.
  // |fuchsia::accessibility::ColorTransformHandler|
  void SetColorTransformConfiguration(
      fuchsia::accessibility::ColorTransformConfiguration configuration,
      SetColorTransformConfigurationCallback callback) override;

 private:
  // Creates the scenic command to apply the requested change.
  void InitColorConversionCmd(
      fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK* display_color_conversion_cmd,
      const std::array<float, 9> color_transform_matrix);

  scenic::Session* session_;  // No ownership.
  const scenic::ResourceId compositor_id_;
  fuchsia::accessibility::Settings settings_;
  fidl::Binding<fuchsia::accessibility::ColorTransformHandler> color_transform_handler_bindings_;
  fuchsia::accessibility::ColorTransformPtr color_transform_manager_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_COLOR_TRANSFORM_HANDLER_H_
