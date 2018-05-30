// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/renderer_params.h"

namespace root_presenter {

RendererParams RendererParams::FromCommandLine(
    const fxl::CommandLine& command_line) {
  RendererParams params;

  std::pair<std::string, fuchsia::ui::gfx::ShadowTechnique> shadow_args[] = {
      {"unshadowed", fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED},
      {"screen_space_shadows", fuchsia::ui::gfx::ShadowTechnique::SCREEN_SPACE},
      {"shadow_map", fuchsia::ui::gfx::ShadowTechnique::SHADOW_MAP},
      {"moment_shadow_map",
       fuchsia::ui::gfx::ShadowTechnique::MOMENT_SHADOW_MAP}};
  for (auto& arg : shadow_args) {
    if (command_line.HasOption(arg.first)) {
      FXL_CHECK(!params.shadow_technique.has_value())
          << "Cannot specify multiple shadow args.";
      params.shadow_technique.set_value(arg.second);
    }
  }
  if (command_line.HasOption("clipping_enabled")) {
    params.clipping_enabled.set_value(true);
  } else if (command_line.HasOption("clipping_disabled")) {
    FXL_CHECK(!params.clipping_enabled.has_value())
        << "Cannot use both -clipping_enabled and -clipping_disabled.";
    params.clipping_enabled.set_value(false);
  }
  if (command_line.HasOption("render_continuously")) {
    params.render_frequency.set_value(
        fuchsia::ui::gfx::RenderFrequency::CONTINUOUSLY);
  } else if (command_line.HasOption("render_when_requested")) {
    FXL_CHECK(!params.render_frequency.has_value())
        << "Cannot use both -render_continuously and -render_when_requested.";
    params.render_frequency.set_value(
        fuchsia::ui::gfx::RenderFrequency::WHEN_REQUESTED);
  }
  return params;
}

}  // namespace root_presenter
