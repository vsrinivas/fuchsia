// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "escher/gl/texture.h"
#include "escher/gl/texture_cache.h"
#include "escher/gl/texture_spec.h"

using namespace escher;

namespace {

class TextureCacheForTest : public TextureCache {
 protected:
  // Create a new texture.  Return 0 if unsuccessful.
  virtual GLuint MakeTexture(const TextureSpec& spec) const {
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

  TextureSpec colorSpec1;
  colorSpec1.size = small;
  colorSpec1.format = TextureSpec::Format::kRGBA;

  TextureSpec colorSpec2;
  colorSpec1.size = large;
  colorSpec1.format = TextureSpec::Format::kRGBA;

  TextureSpec mipmapSpec1;
  mipmapSpec1.size = small;
  mipmapSpec1.format = TextureSpec::Format::kRGBA;
  mipmapSpec1.SetFlag(TextureSpec::Flag::kMipmapped);

  TextureSpec depthSpec1;
  depthSpec1.size = small;
  depthSpec1.format = TextureSpec::Format::kDepth;

  // Sanity check TextureSpec equality.
  EXPECT_FALSE(colorSpec1 == colorSpec2);
  EXPECT_FALSE(colorSpec1 == mipmapSpec1);
  EXPECT_FALSE(colorSpec1 == depthSpec1);

  {
    Texture color1 = cache.GetTexture(colorSpec1);
    Texture color2 = cache.GetTexture(colorSpec2);
    Texture mipmap1 = cache.GetTexture(mipmapSpec1);
    Texture depth1 = cache.GetTexture(depthSpec1);

    // specs of each Texture should be as expected.
    colorId1 = color1.id();
    colorId2 = color2.id();
    mipmapId1 = mipmap1.id();
    depthId1 = depth1.id();
    EXPECT_EQ(color1.spec(), colorSpec1);
    EXPECT_EQ(color2.spec(), colorSpec2);
    EXPECT_EQ(mipmap1.spec(), mipmapSpec1);
    EXPECT_EQ(depth1.spec(), depthSpec1);

    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(0, cache.GetUnusedTextureCount());

    // Swap color1 with another Texture with same spec.
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_NE(colorId1, color1.id());
    EXPECT_EQ(color1.spec(), colorSpec1);
    EXPECT_EQ(4, cache.GetUsedTextureCount());
    EXPECT_EQ(1, cache.GetUnusedTextureCount());

    // Swap it again.  This will retrieve the one that was just returned to
    // the cache.
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_EQ(colorId1, color1.id());
    // ... a few more times.
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_NE(colorId1, color1.id());
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_EQ(colorId1, color1.id());
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_NE(colorId1, color1.id());
    color1 = cache.GetTexture(colorSpec1);
    EXPECT_EQ(colorId1, color1.id());
  }
}
