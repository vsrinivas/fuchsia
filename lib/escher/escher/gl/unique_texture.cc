// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/unique_texture.h"

#include "ftl/logging.h"
#include <iostream>

namespace escher {

UniqueTexture MakeUniqueTexture() {
  GLuint id = 0;
  glGenTextures(1, &id);
  UniqueTexture texture;
  texture.Reset(id);
  return texture;
}

UniqueTexture MakeDepthTexture(const SizeI& size) {
  UniqueTexture result = MakeUniqueTexture();
  glBindTexture(GL_TEXTURE_2D, result.id());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, size.width(),
               size.height(), 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT,
               nullptr);
  return result;
}

UniqueTexture MakeColorTexture(const SizeI& size) {
  UniqueTexture result = MakeUniqueTexture();
  glBindTexture(GL_TEXTURE_2D, result.id());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  return result;
}

UniqueTexture MakeMipmappedColorTexture(const SizeI& size) {
  UniqueTexture result = MakeUniqueTexture();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, result.id());

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0,
               GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glGenerateMipmap(GL_TEXTURE_2D);
  FTL_DCHECK(glGetError() == GL_NO_ERROR);

  return result;
}

}  // namespace escher
