// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "lib/fxl/command_line.h"

namespace root_presenter {

// Stores optional default render values for the presenter.
struct RendererParams {
 public:
  static RendererParams FromCommandLine(const fxl::CommandLine& command_line);

  template <class T>
  class OptionalValue {
   public:
    OptionalValue() = default;
    OptionalValue(T value) { set_value(value); };
    bool has_value() { return has_value_; }
    void set_value(T value) {
      value_ = value;
      has_value_ = true;
    }
    T value() { return value_; }

   private:
    bool has_value_ = false;
    T value_;
  };

  OptionalValue<fuchsia::ui::gfx::RenderFrequency> render_frequency;
  OptionalValue<fuchsia::ui::gfx::ShadowTechnique> shadow_technique;
  OptionalValue<bool> clipping_enabled;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_RENDERER_PARAMS_H_
