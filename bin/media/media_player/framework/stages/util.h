// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_UTIL_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_UTIL_H_

#include <vector>

#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"

namespace media_player {

bool HasPositiveDemand(const std::vector<Output>& outputs);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_FRAMEWORK_STAGES_UTIL_H_
