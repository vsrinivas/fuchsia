// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/base/type_info.h"

namespace escher {

// All subclasses of Resource are represented here.
enum class ResourceType {
  // Abstract base classes.
  kResource = 1,
  kWaitableResource = 1 << 1,

  // Concrete subclasses.
  kImage = 1 << 2,
  kTexture = 1 << 3,
  kFramebuffer = 1 << 4,
  kBuffer = 1 << 5,
  kMesh = 1 << 6,

  // Resources defined in escher::impl namespace.
  kImplModelDisplayList = 1 << 30,
  kImplDescriptorSetAllocation = 1 << 31,
};

typedef TypeInfo<ResourceType> ResourceTypeInfo;

}  // namespace escher
