// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/base/type_info.h"

namespace escher {

// All subclasses of Resource are represented here.
enum class ResourceType {
  kResource = 1,
  kImage = 1 << 1,
  kTexture = 1 << 2,
  kFramebuffer = 1 << 3,
};

typedef TypeInfo<ResourceType> ResourceTypeInfo;

}  // namespace escher
