// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <optional>

#include "src/lib/fxl/command_line.h"

namespace root_presenter {

// Stores optional default render values for the presenter.
struct RendererParams {
 public:
  static RendererParams FromCommandLine(const fxl::CommandLine& command_line);

  std::optional<fuchsia::ui::gfx::RenderFrequency> render_frequency;
  std::optional<fuchsia::ui::gfx::ShadowTechnique> shadow_technique;
  std::optional<bool> clipping_enabled;
  std::optional<bool> debug_enabled;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_
