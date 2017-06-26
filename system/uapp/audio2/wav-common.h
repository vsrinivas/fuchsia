// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <endian.h>
#include <magenta/compiler.h>
#include <magenta/types.h>

namespace internal {
static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b,
                                             uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) |
         (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8)  |
          static_cast<uint32_t>(a);
}
}

class WAVCommon {
public:
    WAVCommon() { }
    virtual ~WAVCommon() { Close(); }

protected:
    struct __PACKED RIFFChunkHeader {
        uint32_t  four_cc;
        uint32_t  length;

        void FixupEndian() {
            four_cc = htole32(four_cc);
            length  = htole32(length);
        }
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

        void FixupEndian() {
            wave_four_cc      = htole32(wave_four_cc);
            fmt_four_cc       = htole32(fmt_four_cc);
            fmt_chunk_len     = htole32(fmt_chunk_len);
            format            = htole16(format);
            channel_count     = htole16(channel_count);
            frame_rate        = htole32(frame_rate);
            average_byte_rate = htole32(average_byte_rate);
            frame_size        = htole16(frame_size);
            bits_per_sample   = htole16(bits_per_sample);
        }
    };

    enum class InitMode { SOURCE, SINK };
    mx_status_t Initialize(const char* filename, InitMode mode);

    static constexpr uint32_t RIFF_FOUR_CC = internal::make_fourcc('R', 'I', 'F', 'F');
    static constexpr uint32_t WAVE_FOUR_CC = internal::make_fourcc('W', 'A', 'V', 'E');
    static constexpr uint32_t FMT_FOUR_CC  = internal::make_fourcc('f', 'm', 't', ' ');
    static constexpr uint32_t DATA_FOUR_CC = internal::make_fourcc('d', 'a', 't', 'a');

    // WAV/AVI format codes are defined in RFC 2361.  Also, the list goes on for
    // 55 pages, so we don't list the vast majority of them here.
    static constexpr uint16_t FORMAT_UNKNOWN    = 0x0000;
    static constexpr uint16_t FORMAT_LPCM       = 0x0001;
    static constexpr uint16_t FORMAT_MSFT_ADPCM = 0x0002;
    static constexpr uint16_t FORMAT_IEEE_FLOAT = 0x0003;
    static constexpr uint16_t FORMAT_MSFT_ALAW  = 0x0006;
    static constexpr uint16_t FORMAT_MSFT_MULAW = 0x0007;

    void Close();
    mx_status_t Read(void* buf, size_t len);
    mx_status_t Write(const void* buf, size_t len);
    mx_status_t Seek(off_t abs_pos);

    int fd_ = -1;
};

