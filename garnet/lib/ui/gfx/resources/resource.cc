// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/resource.h"

#include <algorithm>

#include "garnet/lib/ui/gfx/engine/resource_linker.h"
#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/import.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Resource::kTypeInfo = {0, "Resource"};

Resource::Resource(Session* session, ResourceId id, const ResourceTypeInfo& type_info)
    : session_(session), id_(id), global_id_(session_->id(), id_), type_info_(type_info) {
  FXL_DCHECK(session);
  FXL_DCHECK(type_info.IsKindOf(Resource::kTypeInfo));
  session_->IncrementResourceCount();
}

Resource::~Resource() {
  for (auto& import : imports_) {
    import->UnbindImportedResource();
  }
  FXL_DCHECK((exported_ && resource_linker_weak_) || (!exported_ && !resource_linker_weak_));
  if (resource_linker_weak_ && exported_) {
    resource_linker_weak_->OnExportedResourceDestroyed(this);
  }
  session_->DecrementResourceCount();
}

ErrorReporter* Resource::error_reporter() const { return session_->error_reporter(); }

bool Resource::SetLabel(const std::string& label) {
  label_ = label.substr(0, ::fuchsia::ui::gfx::kLabelMaxLength);
  return true;
}

bool Resource::SetEventMask(uint32_t event_mask) {
  event_mask_ = event_mask;
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
  FXL_DCHECK(it != imports_.end()) << "Import must not already be unbound from this resource.";
  imports_.erase(it);
}

bool Resource::Detach() {
  error_reporter()->ERROR() << "Resources of type: " << type_name() << " do not support Detach().";
  return false;
}

Resource* Resource::GetDelegate(const ResourceTypeInfo& type_info) {
  return type_info_.IsKindOf(type_info) ? this : nullptr;
}

void Resource::SetExported(bool exported,
                           const fxl::WeakPtr<ResourceLinker>& resource_linker_weak) {
  FXL_DCHECK((exported && resource_linker_weak) || (!exported && !resource_linker_weak));
  exported_ = exported;
  resource_linker_weak_ = resource_linker_weak;
}

}  // namespace gfx
}  // namespace scenic_impl
