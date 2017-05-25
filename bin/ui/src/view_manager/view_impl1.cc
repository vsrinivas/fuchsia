// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/view_impl1.h"

namespace view_manager {

ViewImpl1::ViewImpl1(ViewRegistry1* registry) : ViewImpl(registry) {}

void ViewImpl1::OnSetState() {}

void ViewImpl1::CreateScene(fidl::InterfaceRequest<mozart::Scene> scene) {
  registry_->CreateScene(state_, std::move(scene));
}

}  // namespace view_manager
