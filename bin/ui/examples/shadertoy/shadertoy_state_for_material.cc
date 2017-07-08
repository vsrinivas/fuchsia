// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/shadertoy_state_for_material.h"

ShadertoyStateForMaterial::ShadertoyStateForMaterial(ShadertoyApp* app,
                                                     mx::eventpair export_token)
    : ShadertoyState(app) {
  FTL_CHECK(false) << "not implemented";
}

void ShadertoyStateForMaterial::OnSetResolution() {
  FTL_CHECK(false) << "not implemented";
}

escher::Framebuffer* ShadertoyStateForMaterial::GetOutputFramebuffer() {
  FTL_CHECK(false) << "not implemented";
  return nullptr;
}
