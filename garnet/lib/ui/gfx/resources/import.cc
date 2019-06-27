// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/import.h"

#include "garnet/lib/ui/gfx/engine/resource_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"

namespace scenic_impl {
namespace gfx {
namespace {
ResourcePtr CreateDelegate(Session* session, ResourceId id, ::fuchsia::ui::gfx::ImportSpec spec) {
  switch (spec) {
    case ::fuchsia::ui::gfx::ImportSpec::NODE:
      return fxl::MakeRefCounted<EntityNode>(session, id);
  }
  return nullptr;
}
}  // namespace

constexpr ResourceTypeInfo Import::kTypeInfo = {ResourceType::kImport, "Import"};

Import::Import(Session* session, ResourceId id, fuchsia::ui::gfx::ImportSpec spec,
               const fxl::WeakPtr<ResourceLinker>& resource_linker_weak)
    : Resource(session, id, Import::kTypeInfo),
      import_spec_(spec),
      delegate_(CreateDelegate(session, id, spec)),
      resource_linker_weak_(resource_linker_weak) {
  FXL_DCHECK(delegate_);
  FXL_DCHECK(!delegate_->type_info().IsKindOf(Import::kTypeInfo));
}

Import::~Import() {
  if (imported_resource_ != nullptr) {
    imported_resource_->RemoveImport(this);
  }
  if (resource_linker_weak_) {
    resource_linker_weak_->OnImportDestroyed(this);
  }
}

Resource* Import::GetDelegate(const ResourceTypeInfo& type_info) {
  if (Import::kTypeInfo == type_info) {
    return this;
  }
  return delegate_->GetDelegate(type_info);
}

void Import::BindImportedResource(Resource* resource) { imported_resource_ = resource; }

void Import::UnbindImportedResource() {
  imported_resource_ = nullptr;

  // Send a ImportUnboundEvent to the SessionListener.
  auto event = ::fuchsia::ui::gfx::Event();
  event.set_import_unbound(::fuchsia::ui::gfx::ImportUnboundEvent());
  event.import_unbound().resource_id = id();
  session()->EnqueueEvent(std::move(event));
}

}  // namespace gfx
}  // namespace scenic_impl
