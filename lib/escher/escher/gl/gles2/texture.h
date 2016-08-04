// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "ftl/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/texture_spec.h"
#include "escher/gl/gles2/resource.h"

namespace escher {
namespace gles2 {

class Texture : public Resource<TextureSpec> {
 public:
  using Resource<TextureSpec>::Resource;
  Texture() {}
  Texture(const Texture& other) = default;
  Texture(Texture&& other) = default;

  Texture& operator=(const Texture& other) = default;
  Texture& operator=(Texture&& other) = default;

  const SizeI& size() const { return spec().size; }
  int width() const { return size().width(); }
  int height() const { return size().height(); }
  TextureSpec::Format format() const { return spec().format; }
  bool HasFlag(TextureSpec::Flag flag) const { return spec().HasFlag(flag); }
  bool IsRenderbuffer() const { return HasFlag(TextureSpec::kRenderbuffer); }
  bool IsMipmapped() const { return HasFlag(TextureSpec::kMipmapped); }

  // Cube maps, 3D textures, etc. not supported yet.
  GLenum GetGLTarget() const { return GL_TEXTURE_2D; }
  GLint GetGLInternalFormat() const;
  GLenum GetGLFormat() const;
  GLenum GetGLType() const;

  void SetImage(GLubyte* data);
};

}  // namespace gles2
}  // namespace escher
