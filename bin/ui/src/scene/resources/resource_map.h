// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/scene/resources/resource.h"
#include "apps/mozart/src/scene/util/error_reporter.h"

#include <unordered_map>

namespace mozart {
namespace scene {

class ResourceMap {
 public:
  explicit ResourceMap(
      ErrorReporter* error_reporter = ErrorReporter::Default());
  ~ResourceMap();

  void Clear();

  // Attempt to add the resource; return true if successful.  Return false if
  // the ID is already present in the map, which is left unchanged.
  bool AddResource(ResourceId id, ResourcePtr resource);

  // Attempt to remove the specified resource.  Return true if successful, and
  // false if the ID was not present in the map.
  bool RemoveResource(ResourceId id);

  size_t size() const { return resources_.size(); }

  // Attempt to find the resource within the map.  If it is found, verify that
  // it has the correct type, and return it.  Return nullptr if it is not found,
  // or if type validation fails.
  template <class ResourceT>
  ftl::RefPtr<ResourceT> FindResource(ResourceId id) {
    auto it = resources_.find(id);

    if (it == resources_.end()) {
      error_reporter_->ERROR() << "No resource exists with ID " << id;
      return ftl::RefPtr<ResourceT>();
    };

    auto resource_ptr = it->second->GetOpsDelegate(ResourceT::kTypeInfo);

    if (resource_ptr == nullptr) {
      error_reporter_->ERROR()
          << "Type mismatch for resource ID " << id << ": actual type is "
          << it->second->type_info().name << ", expected a sub-type of "
          << ResourceT::kTypeInfo.name;
      return ftl::RefPtr<ResourceT>();
    }

    return ftl::RefPtr<ResourceT>(static_cast<ResourceT*>(resource_ptr));
  }

 private:
  std::unordered_map<ResourceId, ResourcePtr> resources_;
  ErrorReporter* const error_reporter_;
};

}  // namespace scene
}  // namespace mozart
