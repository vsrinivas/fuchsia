// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/inspect/component.h>
#include <sdk/lib/vfs/cpp/vmo_file.h>

#include <memory>

namespace inspect {

std::weak_ptr<ComponentInspector> ComponentInspector::singleton_;

ComponentInspector::ComponentInspector()
    : inspector_(), root_tree_(inspector_.CreateTree("root")) {}

std::shared_ptr<ComponentInspector> ComponentInspector::Initialize(
    sys::ComponentContext* startup_context) {
  ZX_ASSERT(!singleton_.lock());

  auto inspector =
      std::shared_ptr<ComponentInspector>(new ComponentInspector());

  zx::vmo read_only_vmo;
  ZX_ASSERT(inspector->root_tree()->GetVmo().duplicate(
                ZX_RIGHTS_BASIC | ZX_RIGHT_READ | ZX_RIGHT_MAP,
                &read_only_vmo) == ZX_OK);
  auto vmo_file =
      std::make_unique<vfs::VmoFile>(std::move(read_only_vmo), 0, 4096);
  ZX_ASSERT(
      startup_context->outgoing()->GetOrCreateDirectory("objects")->AddEntry(
          "root.inspect", std::move(vmo_file)) == ZX_OK);

  singleton_ = inspector;

  return inspector;
}

}  // namespace inspect
