// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"

#include <stdint.h>

namespace escher {

struct TextureSpec {
  struct Hash;

  // Textures may have more than one flag set.  Some combinations of flags may
  // not be allowed.
  enum Flag {
    kNone = 0x0,
    // Designates a texture that may only be used as a framebuffer attachment,
    // not used by a sampler in a shader.  OpenGL calls this a renderbuffer;
    // we follow the convention established by Metal (and probably Vulkan?),
    // which simplifies render-pass specification.
    kRenderbuffer = 0x1,
    kMipmapped = 0x2,
  };
  enum class Format { kRGBA, kDepth, kInvalid };

  bool HasFlag(Flag flag) const { return flags & flag; }
  void SetFlag(Flag flag) { flags |= flag; }

  SizeI size;
  uint32_t flags = Flag::kNone;
  Format format = Format::kInvalid;
};

inline bool operator==(const TextureSpec& lhs,
                       const TextureSpec& rhs) {
  return lhs.size == rhs.size &&
         lhs.flags == rhs.flags &&
         lhs.format == rhs.format;
}

struct TextureSpec::Hash {
  typedef TextureSpec argument_type;
  typedef size_t result_type;

  inline size_t operator()(const TextureSpec& spec) const {
    return spec.size.GetHashCode() +
           spec.flags * 7 +
           static_cast<int>(spec.format) * 37;
  }
};

}  // namespace escher
