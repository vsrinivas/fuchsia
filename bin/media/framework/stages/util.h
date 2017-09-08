// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/media/framework/stages/stage_impl.h"

namespace media {

bool HasPositiveDemand(const std::vector<Output>& outputs);

}  // namespace media
