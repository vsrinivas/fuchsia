// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "observer_registry.h"

namespace view_tree {

void Registry::RegisterGlobalGeometryProvider(
    fidl::InterfaceRequest<fuchsia::ui::observation::geometry::Provider> request,
    Registry::RegisterGlobalGeometryProviderCallback callback) {
  // TODO(fxbug.dev/85238): Complete this method.
}

}  // namespace view_tree
