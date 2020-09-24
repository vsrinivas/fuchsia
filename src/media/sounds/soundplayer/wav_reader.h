// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_SOUNDS_SOUNDPLAYER_WAV_READER_H_
#define SRC_MEDIA_SOUNDS_SOUNDPLAYER_WAV_READER_H_

#include <lib/fit/result.h>

#include <vector>

#include "src/media/sounds/soundplayer/sound.h"

namespace soundplayer {

class WavReader {
 public:
  WavReader();

  ~WavReader();

  // Processes the file. |fd| must be positioned at the beginning of the file. This method does
  // not close |fd| regardless of the result, but will leave |fd| at an arbitrary position.
  fit::result<Sound, zx_status_t> Process(int fd);

  // Processes the buffer.
  fit::result<Sound, zx_status_t> Process(const uint8_t* data, size_t size);

 private:
  struct FourCc {
    FourCc();
    FourCc(uint8_t a, uint8_t b, uint8_t c, uint8_t d);
    explicit FourCc(uint32_t value);
    uint32_t value_;

    bool operator==(FourCc other) const { return value_ == other.value_; }
    bool operator!=(FourCc other) const { return value_ != other.value_; }
  };

  struct Data {
    zx::vmo vmo_;
    uint64_t size_;
  };

  static const FourCc kRiff;
  static const FourCc kWave;
  static const FourCc kFmt;
  static const FourCc kData;

  WavReader& Fail(zx_status_t status = ZX_ERR_IO);

  bool healthy() const;

  bool GetBytes(size_t count, void* dest);

  const void* data();

  size_t bytes_remaining() const;

  bool Skip(size_t count);

  WavReader& operator>>(Sound& value);
  WavReader& operator>>(FourCc& value);
  WavReader& operator>>(uint16_t& value);
  WavReader& operator>>(uint32_t& value);
  WavReader& operator>>(fuchsia::media::AudioStreamType& value);
  WavReader& operator>>(Data& value);

  zx_status_t WriteDataNoConversion(const zx::vmo& vmo, const void* data, uint32_t* size_in_out);
  zx_status_t WriteData24To32(const zx::vmo& vmo, const void* data, uint32_t* size_in_out);

  zx_status_t status_ = ZX_ERR_BAD_STATE;
  const uint8_t* buffer_;
  size_t size_;
  size_t bytes_consumed_ = 0;
  fit::function<zx_status_t(const zx::vmo&, const void*, uint32_t*)> data_writer_;
};

}  // namespace soundplayer

#endif  // SRC_MEDIA_SOUNDS_SOUNDPLAYER_WAV_READER_H_
