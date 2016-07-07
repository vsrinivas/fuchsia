// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/texture.h"

namespace escher {

Texture::Texture() {}

Texture::Texture(TextureDescriptor descriptor, UniqueTexture texture)
  : descriptor_(std::move(descriptor)), texture_(std::move(texture)) {}

Texture::~Texture() {}

Texture::Texture(Texture&& other)
  : descriptor_(std::move(other.descriptor_)),
    texture_(std::move(other.texture_)) {}

Texture& Texture::operator=(Texture&& other) {
  std::swap(descriptor_, other.descriptor_);
  std::swap(texture_, other.texture_);
  return *this;
}

Texture Texture::Make(TextureDescriptor descriptor) {
  UniqueTexture texture;
  if (descriptor.factory)
    texture = descriptor.factory(descriptor.size);
  return Texture(std::move(descriptor), std::move(texture));
}

}  // namespace escher
