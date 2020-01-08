// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/inspect/cpp/component.h>
#include <lib/vfs/cpp/vmo_file.h>

#include <memory>

using inspect::NodeHealth;

namespace sys {

ComponentInspector::ComponentInspector(sys::ComponentContext* startup_context) : inspector_() {
  zx::vmo read_only_vmo = inspector_.DuplicateVmo();
  if (read_only_vmo.get() != ZX_HANDLE_INVALID) {
    auto vmo_file = std::make_unique<vfs::VmoFile>(std::move(read_only_vmo), 0, 4096);
    ZX_ASSERT(startup_context->outgoing()
                  ->GetOrCreateDirectory("diagnostics")
                  ->AddEntry("root.inspect", std::move(vmo_file)) == ZX_OK);
  }
}

NodeHealth& ComponentInspector::Health() {
  if (!component_health_) {
    component_health_ = std::make_unique<NodeHealth>(&inspector()->GetRoot());
  }
  return *component_health_;
}

}  // namespace sys
