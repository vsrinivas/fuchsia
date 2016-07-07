// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/size_i.h"
#include "escher/gl/unique_texture.h"

namespace escher {

UniqueTexture MakeNoiseTexture(const SizeI& size);

}  // namespace escher
