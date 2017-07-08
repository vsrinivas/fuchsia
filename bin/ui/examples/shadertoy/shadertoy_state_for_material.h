// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/examples/shadertoy/shadertoy_state.h"

// Subclass of ShadertoyState that exports a Material that uses an ImagePipe as
// its texture.  The goal is to be slightly easier for clients to use than
// ShadertoyStateForImagePipe.
class ShadertoyStateForMaterial : public ShadertoyState {
 public:
  ShadertoyStateForMaterial(ShadertoyApp* app, mx::eventpair export_token);

 private:
  void OnSetResolution() override;
  escher::Framebuffer* GetOutputFramebuffer() override;
};
