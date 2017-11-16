// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace media {
namespace audio {

// The number of fractional bits used when expressing timestamps (in frame
// units) as fixed point integers.  Ultimately, this determines the resolution
// that a source of PCM frames may be sampled at; there are 2^frac_bits
// positions between audio frames that the source stream may be sampled at.
//
// Using 64-bit signed timestamps means that we have 51 bits of whole frame
// units to work with.  At 192KHz, this allows for ~372.7 years of usable range
// before rollover when starting from a frame counter of 0.
constexpr uint32_t kPtsFractionalBits = 12;

// A compile time constant which is guaranteed to never be used as a valid
// generation ID (by any of the various things which use generation IDs to track
// changes to state).
constexpr uint32_t kInvalidGenerationId = 0;

}  // namespace audio
}  // namespace media
