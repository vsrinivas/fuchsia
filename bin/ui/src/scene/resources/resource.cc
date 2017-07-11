// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/resource.h"

#include <algorithm>

#include "apps/mozart/src/scene/resources/import.h"
#include "apps/mozart/src/scene/session/session.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo Resource::kTypeInfo = {0, "Resource"};

Resource::Resource(Session* session, const ResourceTypeInfo& type_info)
    : session_(session), type_info_(type_info) {
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

}  // namespace scene
}  // namespace mozart
