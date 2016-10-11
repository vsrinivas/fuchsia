// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/src/framework/stages/stage.h"

namespace mojo {
namespace media {

bool HasPositiveDemand(const std::vector<Output>& outputs);

}  // namespace media
}  // namespace mojo
