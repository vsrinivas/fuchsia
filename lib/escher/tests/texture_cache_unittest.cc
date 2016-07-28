// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "escher/gl/texture.h"
#include "escher/gl/texture_cache.h"
#include "escher/gl/texture_descriptor.h"

using namespace escher;

namespace {

class TextureCacheForTest : public TextureCache {


 protected:
  // Create a new texture.  Return 0 if unsuccessful.
  virtual GLuint MakeTexture(const TextureDescriptor& descriptor) const {
    return next_texture_id_++;
  }

  virtual void DeleteTextures(size_t count, GLuint* ids) const {}

 private:
  mutable GLuint next_texture_id_ = 1;
};

}  // namespace


TEST(TextureCache, CreationAndReuse) {
  TextureCacheForTest cache;

  SizeI small(64, 64);
  SizeI medium(256, 256);
  SizeI large(1024, 1024);

  GLuint colorId1 = 0;
  GLuint colorId2 = 0;
  GLuint mipmapId1 = 0;
  GLuint depthId1 = 0;

  // Obtain 4 textures.
  {
    Texture color1 = cache.GetColorTexture(small);
    Texture color2 = cache.GetColorTexture(medium);
    Texture mipmap1 = cache.GetMipmappedColorTexture(small);
    Texture depth1 = cache.GetDepthTexture(small);

    colorId1 = color1.id();
    colorId2 = color2.id();
    mipmapId1 = mipmap1.id();
    depthId1 = depth1.id();

    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(0, cache.GetUnusedTextureCount());
  }
  // All textures are returned to the cache.
  EXPECT_EQ(0, cache.GetUsedTextureCount());
  EXPECT_EQ(4, cache.GetUnusedTextureCount());

  // Obtain 4 more textures, with the same specificiations.
  {
    Texture color1 = cache.GetColorTexture(small);
    Texture color2 = cache.GetColorTexture(medium);
    Texture mipmap1 = cache.GetMipmappedColorTexture(small);
    Texture depth1 = cache.GetDepthTexture(small);

    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(0, cache.GetUnusedTextureCount());
    EXPECT_EQ(colorId1, color1.id());
    EXPECT_EQ(colorId2, color2.id());
    EXPECT_EQ(mipmapId1, mipmap1.id());
    EXPECT_EQ(depthId1, depth1.id());
  }
  // All textures are returned to the cache.
  EXPECT_EQ(0, cache.GetUsedTextureCount());
  EXPECT_EQ(4, cache.GetUnusedTextureCount());

  // Obtain 4 textures with different specifications... the unused ones cannot
  // be reused.
  {
    Texture color1 = cache.GetColorTexture(large);
    Texture color2 = cache.GetColorTexture(large);
    Texture mipmap1 = cache.GetMipmappedColorTexture(large);
    Texture depth1 = cache.GetDepthTexture(large);

    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(4, cache.GetUnusedTextureCount());
  }
  // All textures are returned to the cache.
  EXPECT_EQ(0, cache.GetUsedTextureCount());
  EXPECT_EQ(8, cache.GetUnusedTextureCount());
}

// TODO(jjosh): this is more a test of Texture copy/move semantics.
TEST(TextureCache, TextureAssignment) {
  TextureCacheForTest cache;

  SizeI small(64, 64);
  SizeI large(256, 256);

  GLuint colorId1 = 0;
  GLuint colorId2 = 0;
  GLuint mipmapId1 = 0;
  GLuint depthId1 = 0;

  TextureDescriptor colorDesc1;
  colorDesc1.size = small;
  colorDesc1.format = TextureDescriptor::Format::kRGBA;

  TextureDescriptor colorDesc2;
  colorDesc1.size = large;
  colorDesc1.format = TextureDescriptor::Format::kRGBA;

  TextureDescriptor mipmapDesc1;
  mipmapDesc1.size = small;
  mipmapDesc1.format = TextureDescriptor::Format::kRGBA;
  mipmapDesc1.mipmapped = true;

  TextureDescriptor depthDesc1;
  depthDesc1.size = small;
  depthDesc1.format = TextureDescriptor::Format::kDepth;

  // Sanity check TextureDescriptor equality.
  EXPECT_FALSE(colorDesc1 == colorDesc2);
  EXPECT_FALSE(colorDesc1 == mipmapDesc1);
  EXPECT_FALSE(colorDesc1 == depthDesc1);

  {
    Texture color1 = cache.GetTexture(colorDesc1);
    Texture color2 = cache.GetTexture(colorDesc2);
    Texture mipmap1 = cache.GetTexture(mipmapDesc1);
    Texture depth1 = cache.GetTexture(depthDesc1);

    // Descriptors of each Texture should be as expected.
    colorId1 = color1.id();
    colorId2 = color2.id();
    mipmapId1 = mipmap1.id();
    depthId1 = depth1.id();
    EXPECT_EQ(color1.descriptor(), colorDesc1);
    EXPECT_EQ(color2.descriptor(), colorDesc2);
    EXPECT_EQ(mipmap1.descriptor(), mipmapDesc1);
    EXPECT_EQ(depth1.descriptor(), depthDesc1);

    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(0, cache.GetUnusedTextureCount());

    // Swap color1 with another Texture with same descriptor.
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_NE(colorId1, color1.id());
    EXPECT_EQ(color1.descriptor(), colorDesc1);
    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(1, cache.GetUnusedTextureCount());

    // Swap it again.  This will retrieve the one that was just returned to
    // the cache.
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_EQ(colorId1, color1.id());
    // ... a few more times.
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_NE(colorId1, color1.id());
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_EQ(colorId1, color1.id());
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_NE(colorId1, color1.id());
    color1 = cache.GetTexture(colorDesc1);
    EXPECT_EQ(colorId1, color1.id());
  }
}
