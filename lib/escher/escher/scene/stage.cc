// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/stage.h"

#include "ftl/logging.h"

namespace escher {

Stage::Stage() : clear_color_(0.012, 0.047, 0.427) {}

Stage::~Stage() {}

void Stage::Resize(SizeI size,
                   float device_pixel_ratio,
                   SizeI viewport_offset) {
  physical_size_ = SizeI(size.width() * device_pixel_ratio,
                         size.height() * device_pixel_ratio);
  viewport_offset_ = SizeI(viewport_offset.width() * device_pixel_ratio,
                           viewport_offset.height() * device_pixel_ratio);
  viewing_volume_ = viewing_volume_.CopyWith(size.width(), size.height());
}

void Stage::set_viewing_volume(ViewingVolume value) {
  // The camera is looking down, so the things on the "floor" of the stage
  // are farthest, and also "lowest" (i.e. they have the smaller z-value).
  FTL_DCHECK(value.near() > value.far());
  viewing_volume_ = std::move(value);
}

}  // namespace escher
