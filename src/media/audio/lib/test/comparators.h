// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_COMPARATORS_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_COMPARATORS_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <cmath>
#include <memory>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/format/audio_buffer.h"

namespace media::audio::test {

struct CompareAudioBufferOptions {
  // See CompareAudioBuffers for a description.
  bool partial = false;

  // If positive, allow the samples to differ from the expected samples by a relative error.
  // If want_slice is empty or RMS(want_slice) == 0, this has no effect.
  // Relative error is defined to be:
  //
  //   abs(RMS(got_slice - want_slice) - RMS(want_slice)) / RMS(want_slice)
  //
  // The term RMS(got_slice - want_slice) is known as RMS error, or RMSE. See:
  // https://en.wikipedia.org/wiki/Root-mean-square_deviation.
  double max_relative_error = 0;

  // These options control additional debugging output of CompareAudioBuffer in failure cases.
  std::string test_label;
  size_t num_frames_per_packet = 100;
};

// Compares got_slice to want_slice, reporting any differences. If got_slice is larger than
// want_slice, the extra suffix should contain silence. If options.partial is true, then got_slice
// should contain a prefix of want_slice, followed by silence.
//
// For example, CompareAudioBuffer succeeds on these inputs
//   got_slice  = {0,1,2,3,4,0,0,0,0,0}
//   want_slice = {0,1,2,3,4}
//   partial    = false
//
// And these inputs:
//   got_slice  = {0,1,2,3,0,0,0,0,0,0}
//   want_slice = {0,1,2,3,4}
//   partial    = true
//
// But not these inputs:
//   got_slice  = {0,1,2,3,0,0,0,0,0,0}
//   want_slice = {0,1,2,3,4}
//   partial    = false
//
// Differences are reported to gtest EXPECT macros.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void CompareAudioBuffers(AudioBufferSlice<SampleFormat> got_slice,
                         AudioBufferSlice<SampleFormat> want_slice,
                         CompareAudioBufferOptions options);

struct ExpectAudioBufferOptions {
  // These options control additional debugging output in failure cases.
  std::string test_label;
  size_t num_frames_per_packet = 100;
};

// Expect that the given slice is silent.
// Equivalent to CompareAudioBuffers(got_slice, AudioBufferSlice(), {.partial = true}).
template <fuchsia::media::AudioSampleFormat SampleFormat>
void ExpectSilentAudioBuffer(AudioBufferSlice<SampleFormat> slice,
                             ExpectAudioBufferOptions options);

// Expect that the given slice is not silent.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void ExpectNonSilentAudioBuffer(AudioBufferSlice<SampleFormat> slice,
                                ExpectAudioBufferOptions options);

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_COMPARATORS_H_
