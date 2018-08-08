// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_VIEW_SHADERTOY_H_
#define GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_VIEW_SHADERTOY_H_

#include "garnet/examples/ui/shadertoy/service/shadertoy_state.h"

namespace shadertoy {

// Subclass of ShadertoyState that displays content in a View, which responds
// directly to touch input.  This is the easiest, but least flexible way to
// use the Shadertoy API.
class ShadertoyStateForView : public ShadertoyState {
 public:
  ShadertoyStateForView(
      App* app,
      ::fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request,
      bool handle_input_events);

 private:
  void OnSetResolution() override;
};

}  // namespace shadertoy

#endif  // GARNET_EXAMPLES_UI_SHADERTOY_SERVICE_VIEW_SHADERTOY_H_
