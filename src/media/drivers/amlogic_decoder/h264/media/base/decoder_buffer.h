// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_

#include <vector>

#include "media/base/decrypt_config.h"

namespace media {
class DecoderBuffer {
 public:
  explicit DecoderBuffer(std::vector<uint8_t> data) : data_(std::move(data)) {}
  const uint8_t* data() const { return data_.data(); }
  size_t data_size() const { return data_.size(); }
  const DecryptConfig* decrypt_config() const { return 0; }

 private:
  std::vector<uint8_t> data_;
};

}  // namespace media
#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_
