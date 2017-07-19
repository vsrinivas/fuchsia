// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/import.h"

#include "apps/mozart/src/scene_manager/resources/nodes/entity_node.h"

namespace mozart {
namespace scene {
namespace {
// Resources that are created to be |Import| delegates are not
// directly owned by the |ResourceMap|. Instead, they are owned by the import
// resources themselves. So we give them an id of 0, which is reserved.
constexpr ResourceId kDelegateResourceId = 0u;

ResourcePtr CreateDelegate(Session* session, mozart2::ImportSpec spec) {
  switch (spec) {
    case mozart2::ImportSpec::NODE:
      return ftl::MakeRefCounted<EntityNode>(session, kDelegateResourceId);
  }
  return nullptr;
}
}  // namespace

constexpr ResourceTypeInfo Import::kTypeInfo = {ResourceType::kImport,
                                                "Import"};

Import::Import(Session* session,
               ResourceId id,
               mozart2::ImportSpec spec,
               mx::eventpair import_token)
    : Resource(session, id, Import::kTypeInfo),
      import_token_(std::move(import_token)),
      import_spec_(spec),
      delegate_(CreateDelegate(session, spec)) {
  FTL_DCHECK(delegate_);
  FTL_DCHECK(!delegate_->type_info().IsKindOf(Import::kTypeInfo));
}

Import::~Import() {
  if (imported_resource_ != nullptr) {
    imported_resource_->RemoveImport(this);
    imported_resource_ = nullptr;
  }
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
}

}  // namespace scene
}  // namespace mozart
