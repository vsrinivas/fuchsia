// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_AUDIO_STREAM_TYPE_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_AUDIO_STREAM_TYPE_H_

#include <memory>
#include <string>
#include <vector>

#include "garnet/bin/mediaplayer/framework/types/stream_type.h"
#include "lib/fxl/logging.h"

namespace media_player {

// Describes the type of an audio stream.
class AudioStreamType : public StreamType {
 public:
  enum class SampleFormat {
    kNone,
    kAny,
    kUnsigned8,
    kSigned16,
    kSigned24In32,
    kFloat
  };

  static std::unique_ptr<StreamType> Create(
      const std::string& encoding, std::unique_ptr<Bytes> encoding_parameters,
      SampleFormat sample_format, uint32_t channels,
      uint32_t frames_per_second) {
    return std::unique_ptr<StreamType>(
        new AudioStreamType(encoding, std::move(encoding_parameters),
                            sample_format, channels, frames_per_second));
  }

  AudioStreamType(const std::string& encoding,
                  std::unique_ptr<Bytes> encoding_parameters,
                  SampleFormat sample_format, uint32_t channels,
                  uint32_t frames_per_second);

  AudioStreamType(const AudioStreamType& other);

  ~AudioStreamType() override;

  const AudioStreamType* audio() const override;

  SampleFormat sample_format() const { return sample_format_; }

  uint32_t channels() const { return channels_; }

  uint32_t frames_per_second() const { return frames_per_second_; }

  uint32_t sample_size() const { return sample_size_; }

  uint32_t bytes_per_frame() const { return sample_size_ * channels_; }

  uint64_t min_buffer_size(uint64_t frame_count) const {
    return frame_count * bytes_per_frame();
  }

  uint64_t frame_count(uint64_t size) const {
    FXL_DCHECK(bytes_per_frame() != 0);
    FXL_DCHECK(size % bytes_per_frame() == 0);
    return size / bytes_per_frame();
  }

  static uint32_t SampleSizeFromFormat(SampleFormat sample_format);

  std::unique_ptr<StreamType> Clone() const override;

 private:
  SampleFormat sample_format_;
  uint32_t channels_;
  uint32_t frames_per_second_;
  uint32_t sample_size_;
};

// Describes a set of audio stream types.
class AudioStreamTypeSet : public StreamTypeSet {
 public:
  static std::unique_ptr<StreamTypeSet> Create(
      const std::vector<std::string>& encodings,
      AudioStreamType::SampleFormat sample_format, Range<uint32_t> channels,
      Range<uint32_t> frames_per_second) {
    return std::unique_ptr<StreamTypeSet>(new AudioStreamTypeSet(
        encodings, sample_format, channels, frames_per_second));
  }

  AudioStreamTypeSet(const std::vector<std::string>& encodings,
                     AudioStreamType::SampleFormat sample_format,
                     Range<uint32_t> channels,
                     Range<uint32_t> frames_per_second);

  ~AudioStreamTypeSet() override;

  const AudioStreamTypeSet* audio() const override;

  AudioStreamType::SampleFormat sample_format() const { return sample_format_; }

  Range<uint32_t> channels() const { return channels_; }

  Range<uint32_t> frames_per_second() const { return frames_per_second_; }

  std::unique_ptr<StreamTypeSet> Clone() const override;

  bool Includes(const StreamType& type) const override;

 private:
  AudioStreamType::SampleFormat sample_format_;
  Range<uint32_t> channels_;
  Range<uint32_t> frames_per_second_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_TYPES_AUDIO_STREAM_TYPE_H_
