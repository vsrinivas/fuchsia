// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>

#include "src/media/audio/lib/test/hermetic_fidelity_test.h"

namespace media::audio::test {

// Frequencies at which we do fidelity testing. We test a broad spectrum of 3 freqs/octave
// (10 freqs/decade), omitting some subsonic frequencies and adding others near the top of the
// audible range. The last five frequencies expressly frame the ultrasound range.
//
// We adjust these frequencies so that an integral number of wavelengths fit into our power-of-two-
// sized analysis buffer. This eliminates our need to window the output before analysis, which in
// turn leads to crisper results. The limitations of float32 "soften" most measurements, except
// where positions/values align so cleanly in the buffer that minimal information is lost to float-
// epsilon-imprecision. In those cases (frequencies that translate into a high-factor number of
// wavelengths in the analysis buffer), we are susceptible to "harmonic spike" measurements. Ideally
// each frequency translates to a PRIME number of periods in the analysis buffer, but since
// "periods" equals "floor-or-ceiling of frequency / frame_rate * analysis_buffer_size", we must at
// least avoid frequencies containing every non-2 factor of our frame rate (for 48khz and 96khz this
// is ** 375 **). Thus in our frequency list we avoid 375, 3000, 7500, 21000, 31500, etc.
const std::array<int32_t, HermeticFidelityTest::kNumReferenceFreqs>
    HermeticFidelityTest::kReferenceFrequencies = {
        // clang-format off
  0,      12,     20,     25,     31,     40,     50,     62,     80,     100,
  125,    160,    200,    250,    315,    400,    500,    625,    800,    1000,
  1250,   1600,   2000,   2500,   3150,   4000,   5000,   6300,   8000,   10000,
  12500,  16000,  20000,  20900,  22000,  23000,  23500,  24500,  25000,  26000,
  28000,  31400,
        // clang-format on
};

}  // namespace media::audio::test
