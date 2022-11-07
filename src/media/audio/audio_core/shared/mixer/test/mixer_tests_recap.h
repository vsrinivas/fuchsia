// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_RECAP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_RECAP_H_

namespace media::audio::test {

// Static-only class to summarize the results of a fidelity test pass.
class MixerTestsRecap {
 public:
  MixerTestsRecap() = delete;
  ~MixerTestsRecap() = delete;
  static void PrintFidelityResultsSummary();

 private:
  static void PrintFrequencyResponseSummary();
  static void PrintSinadSummary();
  static void PrintOutOfBandRejectionSummary();
  static void PrintPhaseResponseSummary();
  static void PrintNoiseFloorSummary();
  static void PrintDynamicRangeSummary();
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_RECAP_H_
