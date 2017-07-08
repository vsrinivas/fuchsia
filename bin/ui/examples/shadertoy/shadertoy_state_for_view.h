// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/shadertoy_state.h"

// Subclass of ShadertoyState that displays content in a View, which responds
// directly to touch input.  This is the easiest, but least flexible way to
// use the Shadertoy API.
class ShadertoyStateForView : public ShadertoyState {
 public:
  ShadertoyStateForView(
      ShadertoyApp* app,
      ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      bool handle_input_events);

 private:
  void OnSetResolution() override;
  escher::Framebuffer* GetOutputFramebuffer() override;
};
