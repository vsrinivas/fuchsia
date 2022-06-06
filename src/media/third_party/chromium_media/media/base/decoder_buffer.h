// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_BASE_DECODER_BUFFER_H_
#define MEDIA_BASE_DECODER_BUFFER_H_

#include <vector>

#include <lib/stdcompat/span.h>
#include "media/base/decrypt_config.h"

#include <lib/fit/defer.h>
#include <lib/media/codec_impl/codec_buffer.h>

namespace media {
class DecoderBuffer {
 public:
  DecoderBuffer(cpp20::span<const uint8_t> buffer,
                const CodecBuffer* maybe_codec_buffer,
                uint32_t buffer_start_offset,
                fit::deferred_callback return_input_packet)
      : buffer_(buffer),
        maybe_codec_buffer_(maybe_codec_buffer),
        buffer_start_offset_(buffer_start_offset),
        return_input_packet_(std::move(return_input_packet)) {
    ZX_DEBUG_ASSERT(static_cast<bool>(return_input_packet_));
  }

  explicit DecoderBuffer(cpp20::span<const uint8_t> buffer) : buffer_(buffer) {}

  // Explicitly disable copying
  DecoderBuffer(const DecoderBuffer&) = delete;
  DecoderBuffer& operator=(const DecoderBuffer&) = delete;

  // Explicitly enable moving
  DecoderBuffer(DecoderBuffer&&) = default;
  DecoderBuffer& operator=(DecoderBuffer&& other) = default;

  const uint8_t* data() const { return buffer_.data(); }
  size_t data_size() const { return buffer_.size(); }

  const uint8_t* side_data() const { return side_data_.get(); }
  size_t side_data_size() const { return side_data_size_; }

  const DecryptConfig* decrypt_config() const { return nullptr; }

  const CodecBuffer* codec_buffer() const { return maybe_codec_buffer_; }
  uint32_t buffer_start_offset() const { return buffer_start_offset_; }

 private:
  cpp20::span<const uint8_t> buffer_;

  // If codec_buffer_, the data_ is also available at codec_buffer_.base() +
  // buffer_start_offset_ and potentially at codec_buffer_.phys_base() +
  // buffer_start_offset_.
  const CodecBuffer* maybe_codec_buffer_ = nullptr;
  // If codec_buffer_, this is the offset at which data_ starts within
  // codec_buffer_.
  uint32_t buffer_start_offset_ = 0;
  // If codec_buffer_, ~return_input_packet_ will recycle the input packet, so
  // the portion of codec_buffer_ can be re-used.
  fit::deferred_callback return_input_packet_;

  // Side data. Used for alpha channel in VPx, and for text cues.
  size_t side_data_size_;
  std::unique_ptr<uint8_t[]> side_data_;
};

}  // namespace media

#endif  // MEDIA_BASE_DECODER_BUFFER_H_
