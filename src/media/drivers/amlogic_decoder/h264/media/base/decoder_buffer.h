// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_
#define SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_

#include "media/base/decrypt_config.h"

namespace media {
class DecoderBuffer {
 public:
  const uint8_t* data() const { return nullptr; }
  size_t data_size() const { return 0; }
  const DecryptConfig* decrypt_config() const { return 0; }
};

}  // namespace media
#endif  // SRC_MEDIA_DRIVERS_AMLOGIC_DECODER_H264_MEDIA_BASE_DECODER_BUFFER_H_
