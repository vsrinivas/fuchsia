// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/examples/shadertoy/shadertoy_state_for_view.h"

ShadertoyStateForView::ShadertoyStateForView(
    ShadertoyApp* app,
    ::fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    bool handle_input_events)
    : ShadertoyState(app) {
  FTL_CHECK(false) << "not implemented";
}

void ShadertoyStateForView::OnSetResolution() {
  FTL_CHECK(false) << "not implemented";
}

escher::Framebuffer* ShadertoyStateForView::GetOutputFramebuffer() {
  FTL_CHECK(false) << "not implemented";
  return nullptr;
}
