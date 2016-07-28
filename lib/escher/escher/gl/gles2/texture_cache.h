// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "ftl/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/gles2/texture.h"

namespace escher {
namespace gles2 {

class TextureCache {
 public:
  TextureCache();
  ~TextureCache();

  Texture GetTexture(const TextureDescriptor& descriptor);

  // Convenient wrappers around GetTexture() that don't require creation
  // of a TextureDescriptor.
  Texture GetDepthTexture(const SizeI& size);
  Texture GetColorTexture(const SizeI& size);
  Texture GetMipmappedColorTexture(const SizeI& size);

  // Delete all unused textures in the cache.
  void Clear();

  size_t GetUnusedTextureCount() const { return cache_.size(); }
  size_t GetUsedTextureCount() const { return used_texture_count_; }

 protected:
  // Create a new texture.  Return 0 if unsuccessful.  Virtual for testing.
  virtual GLuint MakeTexture(const TextureDescriptor& descriptor) const;
  // Virtual for testing.
  virtual void DeleteTextures(size_t count, GLuint* ids) const;

 private:
  // Receive and cache a texture that is not currently needed.
  void RecieveTexture(TextureDescriptor descriptor, GLuint id);

  // Used to return textures when they are no longer needed.
  class TextureNeed : public Need {
   public:
    TextureNeed(
        const TextureDescriptor& descriptor, GLuint id, TextureCache* cache);
    ~TextureNeed() final;

   private:
    TextureDescriptor descriptor_;
    GLuint id_;
    TextureCache* cache_;
  };

  // Store unused cached textures.
  std::unordered_multimap<TextureDescriptor,
                          GLuint,
                          TextureDescriptor::Hash> cache_;

  size_t used_texture_count_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(TextureCache);
};

}  // namespace gles2
}  // namespace escher
