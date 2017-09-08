// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "garnet/bin/ui/sketchy/resources/types.h"

namespace sketchy_service {

class ResourceMap {
 public:
  // Attempt to add the resource; return true if successful.  Return false if
  // the ID is already present in the map, which is left unchange
  bool AddResource(ResourceId id, ResourcePtr resource);

  // Attempt to remove the specified resource.  Return true if successful, and
  // false if the ID was not present in the map.
  bool RemoveResource(ResourceId id);

  // Clear the resources that have been added.
  void Clear();

  // Attempt to find the resource within the map.  If it is found, verify that
  // it has the correct type, and return it.  Return nullptr if it is not found,
  // or if type validation fails.
  //
  // example:
  // ResourceType someResource = map.FindResource<ResourceType>();
  template <class ResourceT>
  ftl::RefPtr<ResourceT> FindResource(ResourceId id);

  size_t size() const { return resources_.size(); }

 private:
  // Maps ID within the session with client to the resource that is maintained.
  std::unordered_map<ResourceId, ResourcePtr> resources_;
};

}  // namespace sketchy_service
