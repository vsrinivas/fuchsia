// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FIDELITY_RESULTS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FIDELITY_RESULTS_H_

#include <array>

#include "src/media/audio/lib/test/hermetic_fidelity_test.h"

namespace media::audio::test {

class FidelityResults {
 public:
  //
  // Generic fidelity thresholds
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> kFullScaleLimitsDb;
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> kSilenceDb;

  //
  // Format-specific fidelity thresholds (these reflect format precision limitations)
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> kUint8LimitsDb,
      kUint8SinadLimitsDb, kInt16SinadLimitsDb, kInt24SinadLimitsDb, kFloat32SinadLimitsDb;

  //
  // Rate-conversion-specific fidelity thresholds (these reflect low-pass filtering)
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> k44100To48kLimitsDb,
      k44100To48kSinadLimitsDb, k44100Micro48kSinadLimitsDb;
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> k96kMicro48kLimitsDb,
      k96kMicro48kSinadLimitsDb;
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> k48kTo96kLimitsDb,
      k48kTo96kSinadLimitsDb;
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> k24kTo48kTo96kLimitsDb,
      k24kTo48kTo96kSinadLimitsDb;
  static const std::array<double, HermeticFidelityTest::kNumReferenceFreqs> k96kTo48kTo96kLimitsDb,
      k96kTo48kTo96kSinadLimitsDb;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_TEST_API_FIDELITY_RESULTS_H_
