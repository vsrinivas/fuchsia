// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unordered_map>

#include "ftl/macros.h"
#include "escher/geometry/size_i.h"
#include "escher/gl/texture.h"
#include "escher/gl/unique_texture.h"

namespace escher {

class TextureCache {
 public:
  TextureCache();
  ~TextureCache();

  Texture GetDepthTexture(const SizeI& size);
  Texture GetColorTexture(const SizeI& size);
  Texture GetMipmappedColorTexture(const SizeI& size);

  Texture GetTexture(const TextureDescriptor& descriptor);
  void PutTexture(Texture texture);

  void Clear();

 private:
  std::unordered_multimap<TextureDescriptor,
                          UniqueTexture,
                          TextureDescriptor::Hash> cache_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TextureCache);
};

}  // namespace escher
