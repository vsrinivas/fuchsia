// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/unique_frame_buffer.h"

namespace escher {
namespace gles2 {

UniqueFrameBuffer MakeUniqueFrameBuffer() {
  GLuint id = 0;
  glGenFramebuffers(1, &id);
  UniqueFrameBuffer frame_buffer;
  frame_buffer.Reset(id);
  return frame_buffer;
}

}  // namespace gles2
}  // namespace escher
