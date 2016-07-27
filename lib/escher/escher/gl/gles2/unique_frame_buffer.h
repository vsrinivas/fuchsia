// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/gl/gles2/unique_object.h"

namespace escher {
namespace gles2 {

namespace internal {
inline void DeleteFramebuffer(GLuint id) {
  glDeleteFramebuffers(1, &id);
}
}  // internal

typedef UniqueObject<internal::DeleteFramebuffer> UniqueFrameBuffer;
UniqueFrameBuffer MakeUniqueFrameBuffer();

}  // namespace gles2
}  // namespace escher
