// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/inspect/cpp/component.h>

#include <memory>

#include <sdk/lib/vfs/cpp/vmo_file.h>

using inspect::NodeHealth;

namespace sys {

std::weak_ptr<ComponentInspector> ComponentInspector::singleton_;

ComponentInspector::ComponentInspector() : inspector_("root") {}

std::shared_ptr<ComponentInspector> ComponentInspector::Initialize(
    sys::ComponentContext* startup_context) {
  ZX_ASSERT(!singleton_.lock());

  auto inspector = std::shared_ptr<ComponentInspector>(new ComponentInspector());

  zx::vmo read_only_vmo;
  auto vmo = inspector->inspector()->GetVmo();
  if (vmo.is_ok()) {
    ZX_ASSERT(vmo.take_value()->duplicate(ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                                          &read_only_vmo) == ZX_OK);
    auto vmo_file = std::make_unique<vfs::VmoFile>(std::move(read_only_vmo), 0, 4096);
    ZX_ASSERT(startup_context->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
                  "root.inspect", std::move(vmo_file)) == ZX_OK);
  }

  singleton_ = inspector;

  return inspector;
}

NodeHealth& ComponentInspector::Health() {
  if (!component_health_) {
    component_health_ = std::make_unique<NodeHealth>(&inspector()->GetRoot());
  }
  return *component_health_;
}

}  // namespace sys
