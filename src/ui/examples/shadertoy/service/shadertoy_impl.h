// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_
#define SRC_UI_EXAMPLES_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_

#include <fuchsia/examples/shadertoy/cpp/fidl.h>

#include "src/ui/examples/shadertoy/service/shadertoy_state.h"

namespace shadertoy {

// Thin wrapper that delegates Shadertoy API calls to a (subclass of)
// ShadertoyState.
class ShadertoyImpl : public fuchsia::examples::shadertoy::Shadertoy {
 public:
  explicit ShadertoyImpl(fxl::RefPtr<ShadertoyState> state);
  ~ShadertoyImpl() override;

  ShadertoyState* state() const { return state_.get(); }

 private:
  // |Shadertoy|
  void SetPaused(bool paused) override;

  // |Shadertoy|
  void SetShaderCode(
      std::string glsl,
      fuchsia::examples::shadertoy::Shadertoy::SetShaderCodeCallback callback) override;

  // |Shadertoy|
  void SetResolution(uint32_t width, uint32_t height) override;

  // |Shadertoy|
  void SetMouse(fuchsia::ui::gfx::vec4 i_mouse) override;

  // |Shadertoy|
  void SetImage(uint32_t channel,
                fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request) override;

  fxl::RefPtr<ShadertoyState> state_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ShadertoyImpl);
};

}  // namespace shadertoy

#endif  // SRC_UI_EXAMPLES_SHADERTOY_SERVICE_SHADERTOY_IMPL_H_
