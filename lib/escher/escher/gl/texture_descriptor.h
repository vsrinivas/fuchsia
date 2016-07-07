// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"
#include "escher/gl/unique_texture.h"

namespace escher {

typedef UniqueTexture (*TextureFactory)(const SizeI& size);

struct TextureDescriptor {
  struct Hash;

  SizeI size;
  TextureFactory factory = nullptr;
};

inline bool operator==(const TextureDescriptor& lhs,
                       const TextureDescriptor& rhs) {
  return lhs.size == rhs.size && lhs.factory == rhs.factory;
}

struct TextureDescriptor::Hash {
  typedef TextureDescriptor argument_type;
  typedef size_t result_type;

  inline size_t operator()(const TextureDescriptor& descriptor) const {
    return descriptor.size.GetHashCode() +
        reinterpret_cast<intptr_t>(descriptor.factory) * 37;
  }
};

}  // namespace escher
