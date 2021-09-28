// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "observer_registry.h"

namespace view_tree {

void Registry::RegisterGlobalGeometryObserver(
    fidl::InterfaceRequest<fuchsia::ui::observation::Geometry> request,
    Registry::RegisterGlobalGeometryObserverCallback callback) {
  // TODO(fxbug.dev/85238): Complete this method.
}

}  // namespace view_tree
