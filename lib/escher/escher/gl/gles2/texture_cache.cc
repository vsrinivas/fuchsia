// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/gl/gles2/texture_cache.h"

#include "ftl/logging.h"

namespace escher {
namespace gles2 {

TextureCache::TextureCache() {}

TextureCache::~TextureCache() {}

Texture TextureCache::GetDepthTexture(const SizeI& size) {
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.factory = MakeDepthTexture;
  return GetTexture(descriptor);
}

Texture TextureCache::GetColorTexture(const SizeI& size) {
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.factory = MakeColorTexture;
  return GetTexture(descriptor);
}

Texture TextureCache::GetMipmappedColorTexture(const SizeI& size) {
  TextureDescriptor descriptor;
  descriptor.size = size;
  descriptor.factory = MakeMipmappedColorTexture;
  descriptor.mipmapped = true;
  return GetTexture(descriptor);
}

Texture TextureCache::GetTexture(const TextureDescriptor& descriptor) {
  auto it = cache_.find(descriptor);
  if (it == cache_.end())
    return Texture::Make(descriptor);
  Texture texture(descriptor, std::move(it->second));
  cache_.erase(it);
  return texture;
}

void TextureCache::PutTexture(Texture texture) {
  if (texture)
    cache_.emplace(texture.descriptor(), texture.TakeUniqueTexture());
}

void TextureCache::Clear() {
  cache_.clear();
}

}  // namespace gles2
}  // namespace escher
