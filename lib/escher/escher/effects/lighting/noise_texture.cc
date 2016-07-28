// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/effects/lighting/noise_texture.h"

#include "escher/gl/texture_cache.h"

#include <memory>
#include <random>

namespace escher {

Texture MakeNoiseTexture(const SizeI& size, TextureCache* cache) {
  std::random_device seed;
  std::default_random_engine prng(seed());
  std::uniform_int_distribution<GLubyte> random;

  int byte_count = size.area() * 4;
  std::unique_ptr<GLubyte[]> data(new GLubyte[byte_count]);
  for (int i = 0; i < byte_count; ++i)
    data[i] = random(prng);

  Texture result = cache->GetColorTexture(size);
  result.SetImage(data.get());
  return result;
}

}  // namespace escher
