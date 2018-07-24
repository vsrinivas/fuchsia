// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/shadertoy/service/view_shadertoy.h"

namespace shadertoy {

ShadertoyStateForView::ShadertoyStateForView(
    App* app,
    ::fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner> view_owner_request,
    bool handle_input_events)
    : ShadertoyState(app) {
  FXL_CHECK(false) << "not implemented";
}

void ShadertoyStateForView::OnSetResolution() {
  FXL_CHECK(false) << "not implemented";
}

}  // namespace shadertoy
