// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inspect_deprecated/component.h"

#include <memory>

#include <sdk/lib/vfs/cpp/vmo_file.h>

namespace inspect_deprecated {

std::weak_ptr<ComponentInspector> ComponentInspector::singleton_;

ComponentInspector::ComponentInspector() : inspector_(), root_tree_(inspector_.CreateTree()) {}

std::shared_ptr<ComponentInspector> ComponentInspector::Initialize(
    sys::ComponentContext* startup_context) {
  ZX_ASSERT(!singleton_.lock());

  auto inspector = std::shared_ptr<ComponentInspector>(new ComponentInspector());

  zx::vmo read_only_vmo = inspector->root_tree_.DuplicateVmo();
  auto vmo_file = std::make_unique<vfs::VmoFile>(std::move(read_only_vmo), 0, 4096);
  ZX_ASSERT(startup_context->outgoing()
                ->GetOrCreateDirectory("diagnostics")
                ->AddEntry("root.inspect", std::move(vmo_file)) == ZX_OK);

  singleton_ = inspector;

  return inspector;
}

NodeHealth& ComponentInspector::Health() {
  if (!component_health_) {
    component_health_ = std::make_unique<NodeHealth>(&root_tree()->GetRoot());
  }
  return *component_health_.get();
}

}  // namespace inspect_deprecated
