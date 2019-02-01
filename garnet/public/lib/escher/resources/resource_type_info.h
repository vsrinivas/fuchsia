// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_
#define LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_

#include "lib/escher/base/type_info.h"

namespace escher {

// All subclasses of Resource are represented here.
enum class ResourceType {
  // Abstract base classes.
  kResource = 1,
  kWaitableResource = 1 << 1,

  // Concrete subclasses.
  kImage = 1 << 2,
  kImageView = 1 << 3,
  kTexture = 1 << 4,
  kFramebuffer = 1 << 5,
  kBuffer = 1 << 6,
  kMesh = 1 << 7,
  kRenderPass = 1 << 8,
  kPipelineLayout = 1 << 9,
  kShaderProgram = 1 << 10,
  kFrame = 1 << 11,

  // Resources defined in escher::impl namespace.
  kImplModelPipelineCache = 1 << 27,
  kImplModelDisplayList = 1 << 28,
  kImplDescriptorSetAllocation = 1 << 29,
  kImplFramebuffer = 1 << 30,
  kImplRenderPass = 1 << 31,
};

typedef TypeInfo<ResourceType> ResourceTypeInfo;

}  // namespace escher

#endif  // LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_
