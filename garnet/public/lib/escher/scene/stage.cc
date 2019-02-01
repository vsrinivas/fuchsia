// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/scene/stage.h"

#include "lib/fxl/logging.h"

namespace escher {

Stage::Stage() : clear_color_(0.f, 0.f, 0.f, 0.f) {}

Stage::~Stage() {}

void Stage::set_viewing_volume(ViewingVolume value) {
  viewing_volume_ = std::move(value);
}

}  // namespace escher
