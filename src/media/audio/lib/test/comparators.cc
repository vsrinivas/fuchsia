// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/test/comparators.h"

#include <optional>
#include <sstream>

#include <gtest/gtest.h>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/analysis/analysis.h"

namespace media::audio::test {

namespace {

template <fuchsia::media::AudioSampleFormat SampleFormat>
std::string CompareAudioBuffersShowContext(AudioBufferSlice<SampleFormat> got_slice,
                                           AudioBufferSlice<SampleFormat> want_slice,
                                           CompareAudioBufferOptions options, size_t frame) {
  size_t raw_frame = got_slice.start_frame() + frame;

  // Relative to got_slice.buf.
  size_t packet = raw_frame / options.num_frames_per_packet;
  size_t packet_start = packet * options.num_frames_per_packet;
  size_t packet_end =
      std::min(packet_start + options.num_frames_per_packet, got_slice.buf()->NumFrames());

  // Display got/want side-by-side.
  std::ostringstream out;
  out << fxl::StringPrintf("\n\n Frames %zu to %zu (packet %zu), got vs want: ", packet_start,
                           packet_end, packet);
  for (auto frame = packet_start; frame < packet_end; ++frame) {
    if (frame % 8 == 0) {
      out << fxl::StringPrintf("\n [%6lu] ", frame);
    } else {
      out << " | ";
    }
    for (auto chan = 0u; chan < got_slice.format().channels(); ++chan) {
      out << SampleFormatTraits<SampleFormat>::ToString(got_slice.buf()->SampleAt(frame, chan));
    }
    out << " vs ";
    for (auto chan = 0u; chan < got_slice.format().channels(); ++chan) {
      // Translate to the equivalent offset in want_slice.buf.
      size_t want_frame = frame + (static_cast<ssize_t>(want_slice.start_frame()) -
                                   static_cast<ssize_t>(got_slice.start_frame()));
      if (!want_slice.empty() && want_frame < want_slice.buf()->NumFrames()) {
        out << SampleFormatTraits<SampleFormat>::ToString(
            want_slice.buf()->SampleAt(want_frame, chan));
      } else {
        out << SampleFormatTraits<SampleFormat>::ToString(
            SampleFormatTraits<SampleFormat>::kSilentValue);
      }
    }
  }
  out << "\n";
  return out.str();
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
std::string ExpectAudioBuffersShowContext(AudioBufferSlice<SampleFormat> slice,
                                          ExpectAudioBufferOptions options, size_t frame) {
  size_t raw_frame = slice.start_frame() + frame;

  // Relative to slice.buf.
  size_t packet = raw_frame / options.num_frames_per_packet;
  size_t packet_start = packet * options.num_frames_per_packet;
  size_t packet_end =
      std::min(packet_start + options.num_frames_per_packet, slice.buf()->NumFrames());

  // Display the packet of got containing frame.
  std::ostringstream out;
  out << fxl::StringPrintf("\n\n Frames %zu to %zu (packet %zu): ", packet_start, packet_end,
                           packet);
  for (auto frame = packet_start; frame < packet_end; ++frame) {
    if (frame % 16 == 0) {
      out << fxl::StringPrintf("\n [%6lu] ", frame);
    } else {
      out << " | ";
    }
    for (auto chan = 0u; chan < slice.format().channels(); ++chan) {
      out << SampleFormatTraits<SampleFormat>::ToString(slice.buf()->SampleAt(frame, chan));
    }
  }
  out << "\n";
  return out.str();
}

// Compare with bit-for-bit equality.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void CompareAudioBuffersExact(AudioBufferSlice<SampleFormat> got_slice,
                              AudioBufferSlice<SampleFormat> want_slice,
                              CompareAudioBufferOptions options) {
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  // Compare sample-by-sample.
  for (size_t frame = 0; frame < got_slice.NumFrames(); frame++) {
    for (size_t chan = 0; chan < got_slice.format().channels(); chan++) {
      SampleT got = got_slice.SampleAt(frame, chan);
      SampleT want = SampleFormatTraits<SampleFormat>::kSilentValue;
      if (frame < want_slice.NumFrames()) {
        want = want_slice.SampleAt(frame, chan);
        if (options.partial && got == SampleFormatTraits<SampleFormat>::kSilentValue &&
            want != got) {
          // Expect that audio data is written one complete frame at a time.
          EXPECT_EQ(0u, chan);
          // Found the end of the prefix.
          want_slice = AudioBufferSlice<SampleFormat>();
          want = SampleFormatTraits<SampleFormat>::kSilentValue;
        }
      }
      if (want != got) {
        size_t raw_frame = got_slice.start_frame() + frame;
        ADD_FAILURE() << options.test_label << ": unexpected value at frame " << raw_frame
                      << ", channel " << chan << ":\n   got[" << raw_frame
                      << "] = " << SampleFormatTraits<SampleFormat>::ToString(got) << "\n  want["
                      << raw_frame << "] = " << SampleFormatTraits<SampleFormat>::ToString(want)
                      << CompareAudioBuffersShowContext(got_slice, want_slice, options, frame);
        return;
      }
    }
  }
}

// Compare with approximate equality.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void CompareAudioBuffersApprox(AudioBufferSlice<SampleFormat> got_slice,
                               AudioBufferSlice<SampleFormat> want_slice, double want_slice_rms,
                               CompareAudioBufferOptions options) {
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  // On failure, we print the context around the first sample that differed.
  struct Sample {
    size_t frame, chan;
    SampleT got, want;
  };
  std::optional<Sample> first_difference;
  double diff_ss = 0;  // sum((got_slice.samples[k] - want_slice.samples[k])^2)

  // Compute RMS of got_slice - want_slice.
  for (size_t frame = 0; frame < got_slice.NumFrames(); frame++) {
    for (size_t chan = 0; chan < got_slice.format().channels(); chan++) {
      SampleT got = got_slice.SampleAt(frame, chan);
      SampleT want = SampleFormatTraits<SampleFormat>::kSilentValue;
      if (frame < want_slice.NumFrames()) {
        want = want_slice.SampleAt(frame, chan);
        if (options.partial && got == SampleFormatTraits<SampleFormat>::kSilentValue &&
            want != got) {
          // Expect that audio data is written one complete frame at a time.
          EXPECT_EQ(0u, chan);
          // Found the end of the prefix.
          want_slice = AudioBufferSlice<SampleFormat>();
          want = SampleFormatTraits<SampleFormat>::kSilentValue;
        }
      }
      if (want == got) {
        continue;
      }
      if (first_difference == std::nullopt) {
        first_difference = {.frame = frame, .chan = chan, .got = got, .want = want};
      }
      double diff = SampleFormatTraits<SampleFormat>::ToFloat(got) -
                    SampleFormatTraits<SampleFormat>::ToFloat(want);
      diff_ss += diff * diff;
    }
  }

  if (first_difference == std::nullopt) {
    return;  // bit-for-bit equal
  }

  double diff_rms = sqrt(diff_ss / got_slice.NumSamples());
  double relative_error = diff_rms / want_slice_rms;
  if (relative_error <= options.max_relative_error) {
    return;  // approximately equal
  }

  size_t raw_frame = got_slice.start_frame() + first_difference->frame;
  ADD_FAILURE() << options.test_label << ": relative error " << relative_error << " > "
                << options.max_relative_error << " (diff_rms = " << diff_rms
                << ", want_slice_rms = " << want_slice_rms << ")"
                << "\nDifferences start at frame " << raw_frame << ":\n   got[" << raw_frame
                << "] = " << SampleFormatTraits<SampleFormat>::ToString(first_difference->got)
                << "\n  want[" << raw_frame
                << "] = " << SampleFormatTraits<SampleFormat>::ToString(first_difference->want)
                << CompareAudioBuffersShowContext(got_slice, want_slice, options,
                                                  first_difference->frame);
}

// Expect silent or not.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void ExpectAudioBuffer(AudioBufferSlice<SampleFormat> slice, ExpectAudioBufferOptions options,
                       bool want_silent) {
  FX_CHECK(!slice.empty());
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;

  for (size_t frame = 0; frame < slice.NumFrames(); frame++) {
    for (size_t chan = 0; chan < slice.format().channels(); chan++) {
      SampleT got = slice.SampleAt(frame, chan);
      SampleT silent = SampleFormatTraits<SampleFormat>::kSilentValue;
      if ((got == silent) != want_silent) {
        size_t raw_frame = slice.start_frame() + frame;
        ADD_FAILURE() << options.test_label << ": unexpected value at frame " << raw_frame
                      << ", channel " << chan << ":\n   got[" << raw_frame
                      << "] = " << SampleFormatTraits<SampleFormat>::ToString(got) << "\n  want"
                      << (want_silent ? " == " : " != ")
                      << SampleFormatTraits<SampleFormat>::ToString(silent)
                      << (want_silent ? " (silent)" : " (not silent)")
                      << ExpectAudioBuffersShowContext(slice, options, frame);
        return;
      }
    }
  }
}

}  // namespace

template <fuchsia::media::AudioSampleFormat SampleFormat>
void CompareAudioBuffers(AudioBufferSlice<SampleFormat> got_slice,
                         AudioBufferSlice<SampleFormat> want_slice,
                         CompareAudioBufferOptions options) {
  FX_CHECK(!got_slice.empty());
  if (!want_slice.empty()) {
    ASSERT_EQ(got_slice.format().channels(), want_slice.format().channels());
  }

  if (want_slice.NumFrames() == 0 || options.max_relative_error == 0) {
    CompareAudioBuffersExact(got_slice, want_slice, options);
    return;
  }

  FX_CHECK(options.max_relative_error > 0);
  double want_slice_rms = MeasureAudioRMS(want_slice);
  if (want_slice_rms == 0) {
    CompareAudioBuffersExact(got_slice, want_slice, options);
    return;
  }

  CompareAudioBuffersApprox(got_slice, want_slice, want_slice_rms, options);
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
void ExpectSilentAudioBuffer(AudioBufferSlice<SampleFormat> slice,
                             ExpectAudioBufferOptions options) {
  ExpectAudioBuffer(slice, options, true);
}

template <fuchsia::media::AudioSampleFormat SampleFormat>
void ExpectNonSilentAudioBuffer(AudioBufferSlice<SampleFormat> slice,
                                ExpectAudioBufferOptions options) {
  ExpectAudioBuffer(slice, options, false);
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(T)                                                        \
  template void CompareAudioBuffers<T>(AudioBufferSlice<T> got_slice,         \
                                       AudioBufferSlice<T> want_slice,        \
                                       CompareAudioBufferOptions options);    \
  template void ExpectSilentAudioBuffer<T>(AudioBufferSlice<T> slice,         \
                                           ExpectAudioBufferOptions options); \
  template void ExpectNonSilentAudioBuffer<T>(AudioBufferSlice<T> slice,      \
                                              ExpectAudioBufferOptions options);

INSTANTIATE(fuchsia::media::AudioSampleFormat::UNSIGNED_8)
INSTANTIATE(fuchsia::media::AudioSampleFormat::SIGNED_16)
INSTANTIATE(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32)
INSTANTIATE(fuchsia::media::AudioSampleFormat::FLOAT)

}  // namespace media::audio::test
