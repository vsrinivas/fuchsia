// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Convenience func to convert gain multiplier (in fixed-pt 4.28) to decibels
// (in float32). Here, dB refers to Power, so 10x change is +20 dB (not +10)
inline float GainScaleToDb(audio::Gain::AScale gain_scale) {
  return 20.0f *
         std::log10(static_cast<float>(gain_scale) / audio::Gain::kUnityScale);
}

// Numerically compare two buffers of integers.
template <typename T>
bool CompareBuffers(const T* actual, const T* expected, uint32_t buf_size);

// Numerically compare a buffer of integers to a specific value.
template <typename T>
bool CompareBufferToVal(const T* buf, T val, uint32_t buf_size);

}  // namespace test
}  // namespace media