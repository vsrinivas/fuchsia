// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"
#include "escher/gl/texture.h"
#include "escher/gl/texture_cache.h"

namespace escher {

Texture MakeNoiseTexture(const SizeI& size, TextureCache* cache);

}  // namespace escher
