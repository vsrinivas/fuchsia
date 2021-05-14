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
// (format codes 0x0001 and 0x0003, respectively). Packed-24 files will be expanded to padded-24
// streams. 24-bit and 32-bit files are provided to clients as 24-in-32-bit LPCM streams.
// This covers all common WAV file types, including any file produced by WavWriter.
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

  // Read up to num_bytes of audio into buffer; return number of bytes read, or errno on failure.
  fit::result<size_t, int> Read(void* buffer, size_t num_bytes);
  // Prepare to Read from the beginning of the data section (again).
  int Reset();

 private:
  WavReader() = default;

  fuchsia::media::AudioSampleFormat sample_format_;
  uint32_t channel_count_ = 0;
  uint32_t frame_rate_ = 0;
  uint32_t bits_per_sample_ = 0;
  uint32_t length_ = 0;
  uint32_t header_size_ = 0;

  // If the file is packed-24, we'll expand to padded-24, on-the-fly.
  // This 12k intermediate buffer should provide good performance even at high bit rates.
  static constexpr int64_t kPacked24BufferSize = 0x3000;
  bool packed_24_ = false;
  int32_t last_modulo_4_ = 0;
  std::unique_ptr<uint8_t[]> packed_24_buffer_;  // only used for 'packed-24'

  fbl::unique_fd file_;
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_WAV_WAV_READER_H_
