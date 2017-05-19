// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace mozart {
namespace composer {

// All subclasses of Resource are represented here.
enum ResourceType {
  kMemory = 1,
  kHostMemory = 1 << 1,
  kGpuMemory = 1 << 2,

  kImage = 1 << 3,
  kBuffer = 1 << 4,

  kLink = 1 << 5,

  kShape = 1 << 6,
  kRectangle = 1 << 7,
  kCircle = 1 << 8,
  kMesh = 1 << 9,

  kMaterial = 1 << 10,

  kNode = 1 << 11,
  kClipNode = 1 << 12,
  kEntityNode = 1 << 13,
  kLinkNode = 1 << 14,
  kShapeNode = 1 << 15,
  kTagNode = 1 << 16,
};

// Bitwise combination of ResourceTypes.  A subclass hierarchy can be
// represented: for each class, a bit is set for that class and all of its
// parent classes.
typedef uint64_t ResourceTypeFlags;

// Static metadata about a Resource subclass.
struct ResourceTypeInfo {
  ResourceTypeFlags flags;
  const char* name;

  // Return true if this type is or inherits from |base_type|, and false
  // otherwise.
  bool IsKindOf(const ResourceTypeInfo& base_type) const {
    return base_type.flags == (flags & base_type.flags);
  }
};

}  // namespace composer
}  // namespace mozart
