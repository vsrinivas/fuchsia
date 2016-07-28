// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/texture.h"

namespace escher {
namespace gles2 {

void Texture::SetImage(GLubyte* data) {
  GLenum target = GetGLTarget();
  glBindTexture(target, id());
  glTexImage2D(target, 0, GetGLInternalFormat(),
               width(), height(),
               0, GetGLFormat(), GetGLType(), data);
}

GLint Texture::GetGLInternalFormat() const {
  switch (format()) {
    case TextureDescriptor::Format::kRGBA:
      return GL_RGBA;
    case TextureDescriptor::Format::kDepth:
      return GL_DEPTH_COMPONENT;
    case TextureDescriptor::Format::kInvalid:
      FTL_DCHECK(false);
      return 0;
  }
}

GLenum Texture::GetGLFormat() const {
  return GetGLInternalFormat();
}

GLenum Texture::GetGLType() const {
  switch (format()) {
    case TextureDescriptor::Format::kRGBA:
      return GL_UNSIGNED_BYTE;
    case TextureDescriptor::Format::kDepth:
      return GL_UNSIGNED_SHORT;
    case TextureDescriptor::Format::kInvalid:
      FTL_DCHECK(false);
      return 0;
  }
}

}  // namespace gles2
}  // namespace escher
