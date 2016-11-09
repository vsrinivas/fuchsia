// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/gain.h"

#include <limits>

namespace media {

// static
const Gain Gain::Silence = Gain(std::numeric_limits<float>::lowest());

// static
const Gain Gain::Unity = Gain(0.0f);

}  // namespace media
