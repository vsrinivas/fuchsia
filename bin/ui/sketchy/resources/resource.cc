// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/resource.h"

namespace sketchy_service {

const ResourceTypeInfo Resource::kTypeInfo("Resource",
                                           ResourceType::kResource);

Resource* Resource::GetDelegate(const ResourceTypeInfo& expected_type) {
  return type_info().IsKindOf(expected_type) ? this : nullptr;
}

}  // namespace sketchy_service
