// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/sys/inspect/cpp/component.h>
#include <lib/vfs/cpp/service.h>
#include <lib/vfs/cpp/vmo_file.h>

#include <memory>

using inspect::NodeHealth;

namespace sys {

ComponentInspector::ComponentInspector(sys::ComponentContext* startup_context) : inspector_() {
  auto connections = std::make_unique<
      fidl::BindingSet<fuchsia::inspect::Tree, std::unique_ptr<fuchsia::inspect::Tree>>>();
  auto state = inspect::internal::GetState(&inspector_);

  startup_context->outgoing()
      ->GetOrCreateDirectory("diagnostics")
      ->AddEntry(fuchsia::inspect::Tree::Name_,
                 std::make_unique<vfs::Service>(inspect::MakeTreeHandler(&inspector_)));
}

NodeHealth& ComponentInspector::Health() {
  if (!component_health_) {
    component_health_ = std::make_unique<NodeHealth>(&inspector()->GetRoot());
  }
  return *component_health_;
}

}  // namespace sys
