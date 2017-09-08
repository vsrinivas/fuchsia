// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
  kImagePipe = 1 << 6,
  kBuffer = 1 << 7,

  // Shapes.
  kShape = 1 << 8,
  kRectangle = 1 << 9,
  kRoundedRectangle = 1 << 10,
  kCircle = 1 << 11,
  kMesh = 1 << 12,

  // Materials.
  kMaterial = 1 << 13,

  // Nodes.
  kNode = 1 << 15,
  kClipNode = 1 << 16,
  kEntityNode = 1 << 17,
  kShapeNode = 1 << 18,

  // Compositor, layers.
  kCompositor = 1 << 19,
  kDisplayCompositor = 1 << 20,
  kLayer = 1 << 21,
  kLayerStack = 1 << 22,

  // Scene, camera, lighting.
  kScene = 1 << 23,
  kCamera = 1 << 24,
  kDirectionalLight = 1 << 25,
  kRenderer = 1 << 26,
};

// Bitwise combination of ResourceTypes.  A subclass hierarchy can be
// represented: for each class, a bit is set for that class and all of its
// parent classes.
using ResourceTypeFlags = uint64_t;

// Static metadata about a Resource subclass.
struct ResourceTypeInfo {
  ResourceTypeFlags flags;
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
