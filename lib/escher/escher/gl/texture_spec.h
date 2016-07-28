// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"

namespace escher {

struct TextureSpec {
  struct Hash;
  enum class Format { kRGBA, kDepth, kInvalid };

  SizeI size;
  Format format = Format::kInvalid;
  bool mipmapped = false;
};

inline bool operator==(const TextureSpec& lhs,
                       const TextureSpec& rhs) {
  return lhs.size == rhs.size &&
         lhs.format == rhs.format &&
         lhs.mipmapped == rhs.mipmapped;
}

struct TextureSpec::Hash {
  typedef TextureSpec argument_type;
  typedef size_t result_type;

  inline size_t operator()(const TextureSpec& spec) const {
    return spec.size.GetHashCode() +
           static_cast<int>(spec.format) * 37 +
           (spec.mipmapped ? 1 : 0);
  }
};

}  // namespace escher
