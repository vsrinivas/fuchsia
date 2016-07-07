// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/stage.h"

namespace escher {

Stage::Stage() {
}

Stage::~Stage() {
}

void Stage::Resize(SizeI size, float device_pixel_ratio) {
  physical_size_ = std::move(size);
  viewing_volume_ = viewing_volume_.CopyWith(
      physical_size_.width() / device_pixel_ratio,
      physical_size_.height() / device_pixel_ratio);
}

}  // namespace escher
