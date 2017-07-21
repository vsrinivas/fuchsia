// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene_manager/resources/resource.h"

#include <algorithm>

#include "apps/mozart/src/scene_manager/engine/session.h"
#include "apps/mozart/src/scene_manager/resources/import.h"

namespace scene_manager {

const ResourceTypeInfo Resource::kTypeInfo = {0, "Resource"};

Resource::Resource(Session* session,
                   mozart::ResourceId id,
                   const ResourceTypeInfo& type_info)
    : session_(session), id_(id), type_info_(type_info) {
  FTL_DCHECK(session);
  FTL_DCHECK(type_info.IsKindOf(Resource::kTypeInfo));
  session_->IncrementResourceCount();
}

Resource::~Resource() {
  for (auto& import : imports_) {
    import->UnbindImportedResource();
  }
  session_->DecrementResourceCount();
}

ErrorReporter* Resource::error_reporter() const {
  return session_->error_reporter();
}

bool Resource::SetLabel(const std::string& label) {
  label_ = label.substr(0, mozart2::kLabelMaxLength);
  return true;
}

void Resource::AddImport(Import* import) {
  // Make sure the types of the resource and the import are compatible.
  if (type_info_.IsKindOf(import->type_info())) {
    error_reporter()->WARN() << "Type mismatch on import resolution.";
    return;
  }

  // Perform the binding.
  imports_.push_back(import);
  import->BindImportedResource(this);
}

void Resource::RemoveImport(Import* import) {
  auto it = std::find(imports_.begin(), imports_.end(), import);
  FTL_DCHECK(it != imports_.end())
      << "Import must not already be unbound from this resource.";
  imports_.erase(it);
}

Resource* Resource::GetDelegate(const ResourceTypeInfo& type_info) {
  return type_info_.IsKindOf(type_info) ? this : nullptr;
}

}  // namespace scene_manager
