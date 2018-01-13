// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_RESOURCE_TYPE_INFO_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_RESOURCE_TYPE_INFO_H_

#include <cstdint>

namespace scene_manager {

// All subclasses of Resource are represented here.
enum ResourceType {
  // Support for importing/exporting resources.
  kImport = 1 << 0,

  // Low-level resources.
  kMemory = 1 << 1,
  kHostMemory = 1 << 2,
  kGpuMemory = 1 << 3,
  kImageBase = 1 << 4,
  kImage = 1 << 5,
  kHostImage = 1 << 6,
  kGpuImage = 1 << 7,
  kImagePipe = 1 << 8,
  kBuffer = 1 << 9,

  // Shapes.
  kShape = 1 << 10,
  kRectangle = 1 << 11,
  kRoundedRectangle = 1 << 12,
  kCircle = 1 << 13,
  kMesh = 1 << 14,

  // Materials.
  kMaterial = 1 << 15,

  // Nodes.
  kNode = 1 << 16,
  kClipNode = 1 << 17,
  kEntityNode = 1 << 18,
  kShapeNode = 1 << 19,

  // Compositor, layers.
  kCompositor = 1 << 20,
  kDisplayCompositor = 1 << 21,
  kLayer = 1 << 22,
  kLayerStack = 1 << 23,

  // Scene, camera, lighting.
  kScene = 1 << 24,
  kCamera = 1 << 25,
  kLight = 1 << 26,
  kAmbientLight = 1 << 27,
  kDirectionalLight = 1 << 28,
  kRenderer = 1 << 29,

  // Animation
  kVariable = 1 << 30,
};

// Bitwise combination of ResourceTypes.  A subclass hierarchy can be
// represented: for each class, a bit is set for that class and all of its
// parent classes.
using ResourceTypeFlags = uint64_t;

// Static metadata about a Resource subclass.
struct ResourceTypeInfo {
  const ResourceTypeFlags flags;
  const char* name;

  // Return true if this type is or inherits from |base_type|, and false
  // otherwise.
  bool IsKindOf(const ResourceTypeInfo& base_type) const {
    return base_type.flags == (flags & base_type.flags);
  }

  bool operator==(const ResourceTypeInfo& type) const {
    return flags == type.flags;
  }
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_RESOURCE_TYPE_INFO_H_
