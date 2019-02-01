// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/resource_map.h"
#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

namespace sketchy_service {

bool ResourceMap::AddResource(ResourceId id, ResourcePtr resource) {
  FXL_DCHECK(resource);
  auto result = resources_.insert(std::make_pair(id, std::move(resource)));
  if (!result.second) {
    FXL_LOG(ERROR)
        << "::fuchsia::ui::sketchy::service::ResourceMap::AddResource(): "
        << "resource with ID " << id << " already exists.";
    return false;
  }
  return true;
}

bool ResourceMap::RemoveResource(ResourceId id) {
  size_t erased_count = resources_.erase(id);
  if (erased_count == 0) {
    FXL_LOG(ERROR)
        << "::fuchsia::ui::sketchy::service::ResourceMap::RemoveResource(): "
        << "no resource with ID " << id;
    return false;
  }
  return true;
}

void ResourceMap::Clear() { resources_.clear(); }

template <class ResourceT>
fxl::RefPtr<ResourceT> ResourceMap::FindResource(ResourceId id) {
  auto it = resources_.find(id);
  if (it == resources_.end()) {
    FXL_LOG(ERROR) << "No resource exists with ID " << id;
    return fxl::RefPtr<ResourceT>();
  }

  auto resource_ptr = it->second->GetDelegate(ResourceT::kTypeInfo);
  if (!resource_ptr) {
    FXL_LOG(ERROR) << "Type mismatch for resource ID " << id
                   << ": actual type is " << it->second->type_info().name
                   << ", expected a sub-type of " << ResourceT::kTypeInfo.name;
    return fxl::RefPtr<ResourceT>();
  }

  return fxl::RefPtr<ResourceT>(static_cast<ResourceT*>(resource_ptr));
}

#define FIND_RESOURCE_FOR(type) \
  template fxl::RefPtr<type> ResourceMap::FindResource<type>(ResourceId id)

FIND_RESOURCE_FOR(ImportNode);
FIND_RESOURCE_FOR(Stroke);
FIND_RESOURCE_FOR(StrokeGroup);

}  // namespace sketchy_service
