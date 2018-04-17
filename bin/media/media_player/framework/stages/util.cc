// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/util.h"

namespace media_player {

bool HasPositiveDemand(const std::vector<Output>& outputs) {
  for (const Output& output : outputs) {
    if (output.demand() == Demand::kPositive) {
      return true;
    }
  }

  return false;
}

}  // namespace media_player
