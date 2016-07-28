// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"

namespace escher {

struct TextureDescriptor {
  struct Hash;
  enum class Format { kRGBA, kDepth, kInvalid };

  SizeI size;
  Format format = Format::kInvalid;
  bool mipmapped = false;
};

inline bool operator==(const TextureDescriptor& lhs,
                       const TextureDescriptor& rhs) {
  return lhs.size == rhs.size &&
         lhs.format == rhs.format &&
         lhs.mipmapped == rhs.mipmapped;
}

struct TextureDescriptor::Hash {
  typedef TextureDescriptor argument_type;
  typedef size_t result_type;

  inline size_t operator()(const TextureDescriptor& descriptor) const {
    return descriptor.size.GetHashCode() +
           static_cast<int>(descriptor.format) * 37 +
           (descriptor.mipmapped ? 1 : 0);
  }
};

}  // namespace escher
