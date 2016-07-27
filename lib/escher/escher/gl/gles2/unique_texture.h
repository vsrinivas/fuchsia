// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"
#include "escher/gl/gles2/unique_object.h"

namespace escher {
namespace gles2 {

namespace internal {
inline void DeleteTexture(GLuint id) {
  glDeleteTextures(1, &id);
}
}  // internal

typedef UniqueObject<internal::DeleteTexture> UniqueTexture;
UniqueTexture MakeUniqueTexture();
UniqueTexture MakeDepthTexture(const SizeI& size);
UniqueTexture MakeColorTexture(const SizeI& size);
UniqueTexture MakeMipmappedColorTexture(const SizeI& size);

}  // namespace gles2
}  // namespace escher
