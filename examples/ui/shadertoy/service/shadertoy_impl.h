// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/examples/ui/shadertoy/service/services/shadertoy.fidl.h"
#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

namespace shadertoy {

// Thin wrapper that delegates Shadertoy API calls to a (subclass of)
// ShadertoyState.
class ShadertoyImpl : public mozart::example::Shadertoy {
 public:
  explicit ShadertoyImpl(ftl::RefPtr<ShadertoyState> state);
  ~ShadertoyImpl() override;

 private:
  // |Shadertoy|
  void SetPaused(bool paused) override;

  // |Shadertoy|
  void SetShaderCode(const ::fidl::String& glsl,
                     const SetShaderCodeCallback& callback) override;

  // |Shadertoy|
  void SetResolution(uint32_t width, uint32_t height) override;

  // |Shadertoy|
  void SetMouse(scenic::vec4Ptr i_mouse) override;

  // |Shadertoy|
  void SetImage(uint32_t channel,
                ::fidl::InterfaceRequest<scenic::ImagePipe> request) override;

  ftl::RefPtr<ShadertoyState> state_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ShadertoyImpl);
};

}  // namespace shadertoy
