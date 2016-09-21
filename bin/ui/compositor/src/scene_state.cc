// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/src/scene_state.h"

namespace compositor {

SceneState::SceneState(mojo::gfx::composition::SceneTokenPtr scene_token,
                       const std::string& label)
    : scene_token_(scene_token.Pass()),
      scene_def_(SceneLabel(scene_token_->value, label)),
      weak_factory_(this) {
  FTL_DCHECK(scene_token_);
}

SceneState::~SceneState() {}

std::ostream& operator<<(std::ostream& os, SceneState* scene_state) {
  if (!scene_state)
    return os << "null";
  return os << scene_state->scene_def()->FormattedLabel();
}

}  // namespace compositor
