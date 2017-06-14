// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace mozart {
namespace scene {

// All subclasses of Resource are represented here.
enum ResourceType {
  kMemory = 1 << 0,
  kHostMemory = 1 << 1,
  kGpuMemory = 1 << 2,
  kImage = 1 << 3,
  kBuffer = 1 << 4,
  kScene = 1 << 5,
  kShape = 1 << 6,
  kRectangle = 1 << 7,
  kRoundedRectangle = 1 << 8,
  kCircle = 1 << 9,
  kMesh = 1 << 10,

  kMaterial = 1 << 11,

  kNode = 1 << 12,
  kClipNode = 1 << 13,
  kEntityNode = 1 << 14,
  kLinkNode = 1 << 15,
  kShapeNode = 1 << 16,
  kTagNode = 1 << 17,
  kProxy = 1 << 18,
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

}  // namespace scene
}  // namespace mozart
