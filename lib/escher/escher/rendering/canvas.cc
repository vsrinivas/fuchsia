// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/rendering/canvas.h"

namespace escher {

void DrawQuad(GLint position, const Quad& quad) {
  glVertexAttribPointer(position, 3, GL_FLOAT, GL_FALSE, 0, quad.data());
  glDrawElements(GL_TRIANGLES, Quad::GetIndexCount(), GL_UNSIGNED_SHORT,
                 Quad::GetIndices());
}

}  // namespace escher
