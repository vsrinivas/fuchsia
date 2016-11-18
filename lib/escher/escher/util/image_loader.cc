// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/image_loader.h"

#include <random>
#include "escher/renderer/image.h"

namespace escher {

namespace {
struct RGBA {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};
}  // namespace

std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width,
                                                 uint32_t height) {
  FTL_DCHECK(width % 2 == 0);
  FTL_DCHECK(height % 2 == 0);

  auto ptr = std::make_unique<uint8_t[]>(width * height * sizeof(RGBA));
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  for (uint32_t j = 0; j < height; ++j) {
    for (uint32_t i = 0; i < width; ++i) {
      uint32_t index = j * width + i;
      auto& p = pixels[index];
      p.r = p.g = p.b = (i + j) % 2 ? 0 : 255;
      p.a = 255;
    }
  }

  return ptr;
}

std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width, uint32_t height) {
  auto ptr = std::make_unique<uint8_t[]>(width * height);
  auto pixels = ptr.get();

  std::random_device seed;
  std::default_random_engine prng(seed());
  std::uniform_int_distribution<uint8_t> random;

  for (uint32_t j = 0; j < height; ++j) {
    for (uint32_t i = 0; i < width; ++i) {
      pixels[j * width + i] = random(prng);
    }
  }

  return ptr;
}

}  // namespace escher
