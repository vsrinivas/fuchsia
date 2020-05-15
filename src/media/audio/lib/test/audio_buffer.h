// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_BUFFER_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_BUFFER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/virtualaudio/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/vmo.h>

#include <memory>

#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"
#include "src/media/audio/lib/wav/wav_reader.h"

namespace media::audio::test {

// TODO(49807): This file is sufficiently complex that it should be tested

template <fuchsia::media::AudioSampleFormat SampleFormat>
struct SampleFormatTraits {
  using SampleT = void;
};

template <>
struct SampleFormatTraits<fuchsia::media::AudioSampleFormat::UNSIGNED_8> {
  using SampleT = uint8_t;
  static constexpr SampleT kSilentValue = 0x80;
  static constexpr SampleT kSequentialIncrement = 1;
  static void PrintSample(SampleT sample) { printf("%02x", 0x0ff & sample); }
};

template <>
struct SampleFormatTraits<fuchsia::media::AudioSampleFormat::SIGNED_16> {
  using SampleT = int16_t;
  static constexpr SampleT kSilentValue = 0;
  static constexpr SampleT kSequentialIncrement = 1;
  static void PrintSample(SampleT sample) { printf("%04x", 0x0ffff & sample); }
};

template <>
struct SampleFormatTraits<fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32> {
  using SampleT = int32_t;
  static constexpr SampleT kSilentValue = 0;
  static constexpr SampleT kSequentialIncrement = 0x100;  // LSB is ignored
  static void PrintSample(SampleT sample) {
    printf("%02x", 0x0ff & (sample >> 24));  // MSB first
    printf("%02x", 0x0ff & (sample >> 16));
    printf("%02x", 0x0ff & (sample >> 8));
    printf("%02x", 0x0ff & sample);
  }
};

template <>
struct SampleFormatTraits<fuchsia::media::AudioSampleFormat::FLOAT> {
  using SampleT = float;
  static constexpr SampleT kSilentValue = 0;
  static constexpr SampleT kSequentialIncrement = 1e-5;
  static void PrintSample(SampleT sample) { printf("%.6f", sample); }
};

// A buffer of audio data. Each entry in the vector is a single sample.
template <fuchsia::media::AudioSampleFormat SampleFormat>
struct AudioBuffer {
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;

  Format format;
  std::vector<SampleT> samples;

  AudioBuffer(const Format& f, size_t num_frames) : format(f), samples(num_frames * f.channels()) {
    FX_CHECK(sizeof(SampleT) == f.bytes_per_frame() / f.channels());
  }

  size_t NumFrames() const { return samples.size() / format.channels(); }
  size_t NumBytes() const { return NumFrames() * format.bytes_per_frame(); }
  size_t SampleIndex(size_t frame, size_t chan) const { return frame * format.channels() + chan; }
  SampleT SampleAt(size_t frame, size_t chan) const { return samples[SampleIndex(frame, chan)]; }

  // For debugging, display the given range of frames.
  void Display(size_t start_frame, size_t end_frame) const {
    printf("\n\n Frames %zu to %zu: ", start_frame, end_frame);
    for (auto frame = start_frame; frame < end_frame; ++frame) {
      if (frame % 16 == 0) {
        printf("\n [%6lu] ", frame);
      } else {
        printf(" ");
      }
      for (auto chan = 0u; chan < format.channels(); ++chan) {
        auto offset = frame * format.channels() + chan;
        SampleFormatTraits<SampleFormat>::PrintSample(samples[offset]);
      }
    }
    printf("\n");
  }
};

// A slice of an AudioBuffer.
template <fuchsia::media::AudioSampleFormat SampleFormat>
struct AudioBufferSlice {
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;

  const AudioBuffer<SampleFormat>* buf;
  size_t start_frame;
  size_t end_frame;

  AudioBufferSlice() : buf(nullptr), start_frame(0), end_frame(0) {}
  AudioBufferSlice(const AudioBuffer<SampleFormat>* b)
      : buf(b), start_frame(0), end_frame(b->NumFrames()) {}
  AudioBufferSlice(const AudioBuffer<SampleFormat>* b, size_t s, size_t e)
      : buf(b), start_frame(std::min(s, b->NumFrames())), end_frame(std::min(e, b->NumFrames())) {}

  const Format& format() const { return buf->format; }
  size_t NumFrames() const { return end_frame - start_frame; }
  size_t NumBytes() const { return NumFrames() * buf->format.bytes_per_frame(); }
  size_t SampleIndex(size_t frame, size_t chan) const {
    return buf->SampleIndex(start_frame + frame, chan);
  }
  SampleT SampleAt(size_t frame, size_t chan) const {
    return buf->SampleAt(start_frame + frame, chan);
  }
};

// Construct a stream of silent audio data.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioBuffer<SampleFormat> GenerateSilentAudio(Format format, size_t num_frames) {
  AudioBuffer<SampleFormat> buf(format, num_frames);
  std::fill(buf.samples.begin(), buf.samples.end(), SampleFormatTraits<SampleFormat>::kSilentValue);
  return buf;
}

// Construct a stream of synthetic audio data that is sequentially incremented. For integer types,
// payload data values increase by 1. For FLOAT, data increases by 10^-5.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioBuffer<SampleFormat> GenerateSequentialAudio(
    Format format, size_t num_frames,
    typename AudioBuffer<SampleFormat>::SampleT first_val =
        SampleFormatTraits<SampleFormat>::kSequentialIncrement) {
  AudioBuffer<SampleFormat> out(format, num_frames);
  for (size_t sample = 0; sample < out.samples.size(); ++sample) {
    out.samples[sample] = first_val;
    first_val += SampleFormatTraits<SampleFormat>::kSequentialIncrement;
    if (SampleFormat == fuchsia::media::AudioSampleFormat::FLOAT && first_val > 1) {
      first_val = -1;
    }
  }
  return out;
}

// Load audio from a WAV file.
template <fuchsia::media::AudioSampleFormat SampleFormat>
AudioBuffer<SampleFormat> LoadWavFile(const std::string& file_name) {
  auto r = WavReader::Open(file_name).take_value();
  auto format = Format::Create({
      .sample_format = r->sample_format(),
      .channels = r->channel_count(),
      .frames_per_second = r->frame_rate(),
  });

  AudioBuffer<SampleFormat> out(format.value(), r->length_in_frames());
  auto size = r->length_in_bytes();
  auto result = r->Read(&out.samples[0], size);
  FX_CHECK(result.is_ok()) << "Read(" << file_name << ") failed, error: " << result.error();
  FX_CHECK(size == result.value())
      << "Read(" << file_name << ") failed, expected " << size << " bytes, got " << result.value();
  return out;
}

struct CompareAudioBufferOptions {
  bool partial = false;

  // These options control additional debugging output of CompareAudioBuffer in failure cases.
  std::string test_label;
  size_t num_frames_per_packet = 100;
};

// Compares got_buffer to want_buffer, reporting any differences. If got_buffer is larger than
// want_buffer, the extra suffix should be silence. If options.partial is true, then got_buffer
// should contain a prefix of want_buffer, where the suffix should be all zeros.
//
// For example, CompareAudioBuffer succeeds on these inputs
//   got_buffer  = {0,1,2,3,4,0,0,0,0,0}
//   want_buffer = {0,1,2,3,4}
//   partial     = false
//
// And these inputs:
//   got_buffer  = {0,1,2,3,0,0,0,0,0,0}
//   want_buffer = {0,1,2,3,4}
//   partial     = true
//
// But not these inputs:
//   got_buffer  = {0,1,2,3,0,0,0,0,0,0}
//   want_buffer = {0,1,2,3,4}
//   partial     = false
//
// Differences are reported to gtest EXPECT macros.
template <fuchsia::media::AudioSampleFormat SampleFormat>
void CompareAudioBuffers(AudioBufferSlice<SampleFormat> got_slice,
                         AudioBufferSlice<SampleFormat> want_slice,
                         CompareAudioBufferOptions options) {
  using SampleT = typename AudioBuffer<SampleFormat>::SampleT;
  constexpr SampleT kSilentValue = SampleFormatTraits<SampleFormat>::kSilentValue;

  FX_CHECK(got_slice.buf);
  if (want_slice.buf) {
    ASSERT_EQ(got_slice.format().channels(), want_slice.format().channels());
  }

  // Compare sample-by-sample.
  for (size_t frame = 0; frame < got_slice.NumFrames(); frame++) {
    for (size_t chan = 0; chan < got_slice.format().channels(); chan++) {
      SampleT got = got_slice.SampleAt(frame, chan);
      SampleT want = kSilentValue;
      if (frame < want_slice.NumFrames()) {
        want = want_slice.SampleAt(frame, chan);
        if (options.partial && got == kSilentValue && want != got) {
          // Expect that audio data is written one complete frame at a time.
          EXPECT_EQ(0u, chan);
          // Found the end of the prefix.
          want_slice = AudioBufferSlice<SampleFormat>();
          want = kSilentValue;
        }
      }
      if (want != got) {
        size_t raw_frame = got_slice.start_frame + frame;
        ADD_FAILURE() << options.test_label << ": unexpected value at frame " << raw_frame
                      << ", channel " << chan << ":\n   got[" << raw_frame << "] = 0x" << std::hex
                      << got << std::dec << "\n  want[" << raw_frame << "] = 0x" << std::hex
                      << want;

        // Relative to got_slice.buf.
        size_t packet = raw_frame / options.num_frames_per_packet;
        size_t packet_start = packet * options.num_frames_per_packet;
        size_t packet_end = packet_start + options.num_frames_per_packet;

        // Display got/want side-by-side.
        printf("\n\n Frames %zu to %zu (packet %zu), got vs want: ", packet_start, packet_end,
               packet);
        for (auto frame = packet_start; frame < packet_end; ++frame) {
          if (frame % 8 == 0) {
            printf("\n [%6lu] ", frame);
          } else {
            printf(" | ");
          }
          for (auto chan = 0u; chan < got_slice.format().channels(); ++chan) {
            SampleFormatTraits<SampleFormat>::PrintSample(got_slice.buf->SampleAt(frame, chan));
          }
          printf(" vs ");
          for (auto chan = 0u; chan < got_slice.format().channels(); ++chan) {
            // Translate to the equivalent offset in want_slice.buf.
            size_t want_frame = frame + (static_cast<ssize_t>(want_slice.start_frame) -
                                         static_cast<ssize_t>(got_slice.start_frame));
            if (want_slice.buf && want_frame < want_slice.buf->NumFrames()) {
              SampleFormatTraits<SampleFormat>::PrintSample(
                  want_slice.buf->SampleAt(want_frame, chan));
            } else {
              SampleFormatTraits<SampleFormat>::PrintSample(kSilentValue);
            }
          }
        }
        printf("\n");
        return;
      }
    }
  }
}

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_AUDIO_BUFFER_H_
