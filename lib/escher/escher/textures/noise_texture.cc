// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/textures/noise_texture.h"

#include <memory>
#include <random>

namespace escher {

UniqueTexture MakeNoiseTexture(const SizeI& size) {
  std::random_device seed;
  std::default_random_engine prng(seed());
  std::uniform_int_distribution<GLubyte> random;

  int byte_count = size.area() * 4;
  std::unique_ptr<GLubyte[]> data(new GLubyte[byte_count]);
  for (int i = 0; i < byte_count; ++i)
    data[i] = random(prng);

  UniqueTexture result = MakeUniqueTexture();
  glBindTexture(GL_TEXTURE_2D, result.id());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width(), size.height(), 0,
               GL_RGBA, GL_UNSIGNED_BYTE, data.get());
  return result;
}

}  // namespace escher
