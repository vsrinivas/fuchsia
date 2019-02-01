// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/resources/waitable_resource.h"

namespace escher {

const ResourceTypeInfo WaitableResource::kTypeInfo(
    "WaitableResource", ResourceType::kResource,
    ResourceType::kWaitableResource);

WaitableResource::WaitableResource(ResourceManager* owner) : Resource(owner) {}

}  // namespace escher
