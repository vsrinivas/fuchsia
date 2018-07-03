// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_CONSTANTS_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_CONSTANTS_H_

#include <stdint.h>

namespace media {
namespace audio {

constexpr int32_t kMaxInt24In32 = std::numeric_limits<int32_t>::max() & ~0x0FF;
constexpr int32_t kMinInt24In32 = std::numeric_limits<int32_t>::min();

constexpr int32_t kFloatToInt8 = -std::numeric_limits<int8_t>::min();
constexpr int32_t kFloatToInt16 = -std::numeric_limits<int16_t>::min();
constexpr int64_t kFloatToInt24In32 = -static_cast<int64_t>(kMinInt24In32);

constexpr int32_t kOffsetInt8ToUint8 =
    std::numeric_limits<uint8_t>::min() - std::numeric_limits<int8_t>::min();

constexpr float kInt8ToFloat = 1.0f / kFloatToInt8;
constexpr float kInt16ToFloat = 1.0f / kFloatToInt16;
constexpr double kInt24In32ToFloat = 1.0 / kFloatToInt24In32;

// The number of fractional bits used when expressing timestamps (in frame
// units) as fixed point integers.  Ultimately, this determines the resolution
// that a source of PCM frames may be sampled at; there are 2^frac_bits
// positions between audio frames that the source stream may be sampled at.
//
// Using 64-bit signed timestamps means that we have 50 bits of whole frame
// units to work with.  At 192KHz, this allows for ~186.3 years of usable range
// before rollover when starting from a frame counter of 0.
//
// With 13 bits of fractional position, a mix job's interpolation precision is
// only +/-61 ppm. Across multiple jobs we stay in sync, but for any single mix,
// this is our granularity. As an example, when resampling a 48 kHz audio
// packet, the "clicks on the dial" of our actual resampling rates are multiples
// of 6 Hz. Again, we do correct any positional error at mix job boundaries.
//
// This also affects our interpolation accuracy: because fractional position has
// a potential error of 2^-13, the worst-case error for interpolated values is
// [pos_err * max_intersample_delta]. This means full-scale very high-frequency
// signals are only guaranteed bit-for-bit accurate in the top 13 bits.
// TODO(mpuryear): MTWN-86 Consider even more fractional position precision.
constexpr uint32_t kPtsFractionalBits = 13;
// Used in places where PTS must be an integral number of frames.
constexpr uint32_t kPtsFractionalMask = (1 << kPtsFractionalBits) - 1;

// Compile time constant guaranteed to never be used as a valid generation ID
// (by the various things which use generation IDs to track state changes).
constexpr uint32_t kInvalidGenerationId = 0;

}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_CONSTANTS_H_
