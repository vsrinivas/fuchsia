// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

#include "audio-source.h"

namespace internal {
static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b,
                                             uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(a) << 24) |
         (static_cast<uint32_t>(b) << 16) |
         (static_cast<uint32_t>(c) << 8)  |
          static_cast<uint32_t>(d);
}
}

class WAVSource : public AudioSource {
public:
    WAVSource() { }
    ~WAVSource();
    mx_status_t Initialize(const char* filename);

    mx_status_t GetFormat(Format* out_format) final;
    mx_status_t PackFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) final;
    bool finished() const final { return payload_played_ >= payload_len_; }

private:
    struct __PACKED RIFFChunkHeader {
        uint32_t  four_cc;
        uint32_t  length;
    };

    struct __PACKED WAVHeader {
        uint32_t  wave_four_cc;
        uint32_t  fmt_four_cc;
        uint32_t  fmt_chunk_len;
        uint16_t  format;
        uint16_t  channel_count;
        uint32_t  frame_rate;
        uint32_t  average_byte_rate;
        uint16_t  frame_size;
        uint16_t  bits_per_sample;
    };

    // TODO(johngro): as mentioned before... endianness!
    static constexpr uint32_t RIFF_FOUR_CC = internal::make_fourcc('R', 'I', 'F', 'F');
    static constexpr uint32_t WAVE_FOUR_CC = internal::make_fourcc('W', 'A', 'V', 'E');
    static constexpr uint32_t FMT_FOUR_CC  = internal::make_fourcc('f', 'm', 't', ' ');
    static constexpr uint32_t DATA_FOUR_CC = internal::make_fourcc('d', 'a', 't', 'a');

    static constexpr uint16_t FORMAT_LPCM  = 0x0001;
    static constexpr uint16_t FORMAT_MULAW = 0x0101;
    static constexpr uint16_t FORMAT_ALAW  = 0x0102;
    static constexpr uint16_t FORMAT_ADPCM = 0x0103;

    mx_status_t Read(void* buf, size_t len);

    int source_fd_ = -1;
    uint32_t payload_len_ = 0;
    uint32_t payload_played_ = 0;
    Format audio_format;
};

