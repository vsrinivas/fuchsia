// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_WAV_WAV_READER_H_
#define SRC_MEDIA_AUDIO_LIB_WAV_WAV_READER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string>

#include <fbl/unique_fd.h>

namespace media {
namespace audio {

// WavReader can read any WAV file encoded with 8-bit, 16-bit, or 32-bit LPCM or 32-bit IEEE Floats
// (format codes 0x0001 and 0x0003, respectively). 32-bit LPCM will be read as 24-in-32-bit LPCM.
// This covers all files produced by WavWriter.
//
// Not thread safe.
class WavReader {
 public:
  ~WavReader() = default;

  WavReader(WavReader&&) = default;
  WavReader& operator=(WavReader&&) = default;

  static fit::result<std::unique_ptr<WavReader>, zx_status_t> Open(const std::string& file_name);

  fuchsia::media::AudioSampleFormat sample_format() const { return sample_format_; }
  uint32_t channel_count() const { return channel_count_; }
  uint32_t frame_rate() const { return frame_rate_; }
  uint32_t bits_per_sample() const { return bits_per_sample_; }
  uint32_t length_in_bytes() const { return length_; }
  uint32_t length_in_frames() const { return length_ / (bits_per_sample_ / 8 * channel_count_); }

  // Reads up to num_bytes of audio data into buffer, returning the number of bytes read,
  // or an errno on failure.
  fit::result<size_t, int> Read(void* buffer, size_t num_bytes);

 private:
  WavReader() = default;

  fuchsia::media::AudioSampleFormat sample_format_;
  uint32_t channel_count_ = 0;
  uint32_t frame_rate_ = 0;
  uint32_t bits_per_sample_ = 0;
  uint32_t length_ = 0;

  fbl::unique_fd file_;
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_WAV_WAV_READER_H_
