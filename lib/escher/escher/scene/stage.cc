// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/stage.h"

namespace escher {

Stage::Stage() {
}

Stage::~Stage() {
}

void Stage::Resize(SizeI size,
                   float device_pixel_ratio,
                   SizeI viewport_offset) {
  physical_size_ = SizeI(size.width() * device_pixel_ratio,
                         size.height() * device_pixel_ratio);
  viewport_offset_ = SizeI(viewport_offset.width() * device_pixel_ratio,
                           viewport_offset.height() * device_pixel_ratio);
  viewing_volume_ = viewing_volume_.CopyWith(size.width(), size.height());
}

}  // namespace escher
