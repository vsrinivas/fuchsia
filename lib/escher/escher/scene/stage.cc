// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/scene/stage.h"

namespace escher {

Stage::Stage() {
}

Stage::~Stage() {
}

void Stage::SetSize(SizeI size) {
  viewing_volume_ = ViewingVolume(std::move(size), viewing_volume_.near(), viewing_volume_.far());
}

}  // namespace escher
