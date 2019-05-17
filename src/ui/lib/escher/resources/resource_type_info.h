// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_
#define SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_

#include "src/ui/lib/escher/base/type_info.h"

namespace escher {

// All subclasses of Resource are represented here.
enum class ResourceType {
  // Abstract base classes.
  kResource = 1,
  kWaitableResource = 1 << 1,

  // Concrete subclasses.
  kImage = 1 << 2,
  kImageView = 1 << 3,
  kSampler = 1 << 4,
  kTexture = 1 << 5,
  kFramebuffer = 1 << 6,
  kBuffer = 1 << 7,
  kMesh = 1 << 8,
  kRenderPass = 1 << 9,
  kPipelineLayout = 1 << 10,
  kShaderProgram = 1 << 11,
  kFrame = 1 << 12,

  // Resources defined in escher::impl namespace.
  kImplModelPipelineCache = 1 << 27,
  kImplModelDisplayList = 1 << 28,
  kImplDescriptorSetAllocation = 1 << 29,
  kImplFramebuffer = 1 << 30,
  kImplRenderPass = 1 << 31,
};

typedef TypeInfo<ResourceType> ResourceTypeInfo;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RESOURCES_RESOURCE_TYPE_INFO_H_
