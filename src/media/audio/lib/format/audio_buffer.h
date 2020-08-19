// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_FORMAT_AUDIO_BUFFER_H_
#define SRC_MEDIA_AUDIO_LIB_FORMAT_AUDIO_BUFFER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cmath>
#include <memory>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format/format.h"
#include "src/media/audio/lib/format/traits.h"

namespace media::audio {

template <fuchsia::media::AudioSampleFormat SampleFormat>
class AudioBufferSlice;

// A buffer of audio data. Each entry in the vector is a single sample.
template <fuchsia::media::AudioSampleFormat SampleFormat>
class AudioBuffer {
 public:
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;

  AudioBuffer(const Format& f, size_t num_frames)
      : format_(f), samples_(num_frames * f.channels()) {
    FX_CHECK(SampleFormat == f.sample_format());
  }

  AudioBuffer(const TypedFormat<SampleFormat>& f, size_t num_frames)
      : format_(f), samples_(num_frames * f.channels()) {
    FX_CHECK(SampleFormat == f.sample_format());
  }

  const Format& format() const { return format_; }
  const std::vector<SampleT>& samples() const { return samples_; }
  std::vector<SampleT>& samples() { return samples_; }

  size_t NumFrames() const { return samples_.size() / format_.channels(); }
  size_t NumBytes() const { return NumFrames() * format_.bytes_per_frame(); }
  size_t SampleIndex(size_t frame, size_t chan) const { return frame * format_.channels() + chan; }
  SampleT SampleAt(size_t frame, size_t chan) const { return samples_[SampleIndex(frame, chan)]; }

  // For debugging, display the given range of frames.
  void Display(size_t start_frame, size_t end_frame) const {
    start_frame = std::min(start_frame, NumFrames());
    end_frame = std::min(end_frame, NumFrames());
    printf("\n\n Frames %zu to %zu: ", start_frame, end_frame);

    for (auto frame = start_frame; frame < end_frame; ++frame) {
      if (frame % 16 == 0) {
        printf("\n [%6lu] ", frame);
      } else {
        printf(" ");
      }
      for (auto chan = 0u; chan < format_.channels(); ++chan) {
        auto offset = frame * format_.channels() + chan;
        printf("%s", SampleFormatTraits<SampleFormat>::ToString(samples_[offset]).c_str());
      }
    }
    printf("\n");
  }

 private:
  friend class AudioBufferSlice<SampleFormat>;

  Format format_;
  std::vector<SampleT> samples_;
};

// A slice of an AudioBuffer.
// Maintains (but does not own) a pointer to the parent AudioBuffer.
template <fuchsia::media::AudioSampleFormat SampleFormat>
class AudioBufferSlice {
 public:
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;

  AudioBufferSlice() : buf_(nullptr), start_frame_(0), end_frame_(0) {}

  AudioBufferSlice(const AudioBuffer<SampleFormat>* b)
      : buf_(b), start_frame_(0), end_frame_(b->NumFrames()) {}

  AudioBufferSlice(const AudioBuffer<SampleFormat>* b, size_t s, size_t e)
      : buf_(b),
        start_frame_(std::min(s, b->NumFrames())),
        end_frame_(std::min(e, b->NumFrames())) {}

  const AudioBuffer<SampleFormat>* buf() const { return buf_; }
  const Format& format() const {
    FX_CHECK(buf_);
    return buf_->format();
  }
  size_t start_frame() const { return start_frame_; }
  size_t end_frame() const { return end_frame_; }
  bool empty() const { return !buf_ || start_frame_ == end_frame_; }

  size_t NumFrames() const { return end_frame_ - start_frame_; }
  size_t NumBytes() const { return NumFrames() * format().bytes_per_frame(); }
  size_t NumSamples() const { return NumFrames() * format().channels(); }
  size_t SampleIndex(size_t frame, size_t chan) const {
    FX_CHECK(buf_);
    return buf_->SampleIndex(start_frame_ + frame, chan);
  }
  SampleT SampleAt(size_t frame, size_t chan) const {
    FX_CHECK(buf_);
    return buf_->SampleAt(start_frame_ + frame, chan);
  }

  // Return a buffer containing the given channel only.
  AudioBuffer<SampleFormat> GetChannel(size_t chan) {
    auto new_format = Format::Create({
                                         .sample_format = SampleFormat,
                                         .channels = 1,
                                         .frames_per_second = format().frames_per_second(),
                                     })
                          .take_value();
    AudioBuffer<SampleFormat> out(new_format, NumFrames());
    for (size_t frame = 0; frame < NumFrames(); frame++) {
      out.samples()[frame] = SampleAt(frame, chan);
    }
    return out;
  }

 private:
  const AudioBuffer<SampleFormat>* buf_;
  size_t start_frame_;
  size_t end_frame_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_LIB_FORMAT_AUDIO_BUFFER_H_
