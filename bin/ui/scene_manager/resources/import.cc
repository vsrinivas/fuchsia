// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/resources/import.h"

#include "garnet/bin/ui/scene_manager/engine/session.h"
#include "garnet/bin/ui/scene_manager/resources/nodes/entity_node.h"

namespace scene_manager {
namespace {
ResourcePtr CreateDelegate(Session* session,
                           scenic::ResourceId id,
                           scenic::ImportSpec spec) {
  switch (spec) {
    case scenic::ImportSpec::NODE:
      return ftl::MakeRefCounted<EntityNode>(session, id);
  }
  return nullptr;
}
}  // namespace

constexpr ResourceTypeInfo Import::kTypeInfo = {ResourceType::kImport,
                                                "Import"};

Import::Import(Session* session,
               scenic::ResourceId id,
               scenic::ImportSpec spec,
               ResourceLinker* resource_linker)
    : Resource(session, id, Import::kTypeInfo),
      import_spec_(spec),
      delegate_(CreateDelegate(session, id, spec)),
      resource_linker_(resource_linker) {
  FTL_DCHECK(delegate_);
  FTL_DCHECK(!delegate_->type_info().IsKindOf(Import::kTypeInfo));
  FTL_DCHECK(resource_linker_);
}

Import::~Import() {
  if (imported_resource_ != nullptr) {
    imported_resource_->RemoveImport(this);
  }
  resource_linker_->OnImportDestroyed(this);
}

Resource* Import::GetDelegate(const ResourceTypeInfo& type_info) {
  if (Import::kTypeInfo == type_info) {
    return this;
  }
  return delegate_->GetDelegate(type_info);
}

void Import::BindImportedResource(Resource* resource) {
  imported_resource_ = resource;
}

void Import::UnbindImportedResource() {
  imported_resource_ = nullptr;

  // Send a ImportUnboundEvent to the SessionListener.
  auto event = scenic::Event::New();
  event->set_import_unbound(scenic::ImportUnboundEvent::New());
  event->get_import_unbound()->resource_id = id();
  session()->EnqueueEvent(std::move(event));
}

}  // namespace scene_manager
