// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "src/media/audio/lib/test/hermetic_fidelity_test.h"

namespace media::audio::test {

// Frequencies at which we do fidelity testing. We test a broad spectrum of 3 freqs/octave
// (10 freqs/decade), omitting some subsonic frequencies and adding others near the top of the
// audible range. The last five frequencies expressly frame the ultrasound range.
const std::array<uint32_t, HermeticFidelityTest::kNumReferenceFreqs>
    HermeticFidelityTest::kReferenceFrequencies = {
        // clang-format off
  0,      12,     20,     25,     31,     40,     50,     62,     80,     100,
  125,    160,    200,    250,    315,    400,    500,    625,    800,    1000,
  1250,   1600,   2000,   2500,   3150,   4000,   5000,   6300,   8000,   10000,
  12500,  16000,  20000,  21000,  22000,  23000,  23500,  24500,  25000,  26000,
  28000,  31500,
        // clang-format on
};

}  // namespace media::audio::test
