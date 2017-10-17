// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <endian.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <limits>

#include "garnet/examples/media/wav_record/wav_hdr.h"

namespace examples {
namespace {

static inline constexpr uint32_t make_fourcc(uint8_t a,
                                             uint8_t b,
                                             uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// clang-format off
static constexpr uint32_t RIFF_FOUR_CC = make_fourcc('R', 'I', 'F', 'F');
static constexpr uint32_t WAVE_FOUR_CC = make_fourcc('W', 'A', 'V', 'E');
static constexpr uint32_t FMT_FOUR_CC  = make_fourcc('f', 'm', 't', ' ');
static constexpr uint32_t DATA_FOUR_CC = make_fourcc('d', 'a', 't', 'a');
static constexpr uint16_t FORMAT_LPCM  = 0x0001;
// clang-format on

struct __PACKED RIFFChunkHeader {
  uint32_t four_cc;
  uint32_t length;

  void FixupEndian() {
    four_cc = htole32(four_cc);
    length = htole32(length);
  }
};

struct __PACKED WAVHeader {
  uint32_t wave_four_cc = WAVE_FOUR_CC;
  uint32_t fmt_four_cc = FMT_FOUR_CC;
  uint32_t fmt_chunk_len = sizeof(WAVHeader) - offsetof(WAVHeader, format);
  uint16_t format = FORMAT_LPCM;
  uint16_t channel_count = 0;
  uint32_t frame_rate = 0;
  uint32_t average_byte_rate = 0;
  uint16_t frame_size = 0;
  uint16_t bits_per_sample = 16;

  void FixupEndian() {
    // clang-format off
    wave_four_cc      = htole32(wave_four_cc);
    fmt_four_cc       = htole32(fmt_four_cc);
    fmt_chunk_len     = htole32(fmt_chunk_len);
    format            = htole16(format);
    channel_count     = htole16(channel_count);
    frame_rate        = htole32(frame_rate);
    average_byte_rate = htole32(average_byte_rate);
    frame_size        = htole16(frame_size);
    bits_per_sample   = htole16(bits_per_sample);
    // clang-format on
  }
};

}  // namespace

// static
zx_status_t WavHeader::Write(int fd,
                             uint32_t channel_count,
                             uint32_t frame_rate,
                             size_t payload_len) {
  constexpr uint32_t kOverhead = sizeof(RIFFChunkHeader) + sizeof(WAVHeader);

  if ((payload_len > (std::numeric_limits<uint32_t>::max() - kOverhead)) ||
      (channel_count > std::numeric_limits<uint16_t>::max())) {
    return ZX_ERR_INVALID_ARGS;
  }

  RIFFChunkHeader hdr;
  WAVHeader wav_hdr;
  ssize_t res;

  hdr.four_cc = RIFF_FOUR_CC;
  hdr.length = static_cast<uint32_t>(kOverhead + payload_len);
  hdr.FixupEndian();
  res = ::write(fd, &hdr, sizeof(hdr));
  if (res < 0)
    return ZX_ERR_IO;

  wav_hdr.channel_count = channel_count;
  wav_hdr.frame_rate = frame_rate;
  wav_hdr.frame_size = (wav_hdr.bits_per_sample >> 3) * channel_count;
  wav_hdr.average_byte_rate = wav_hdr.frame_size * frame_rate;
  wav_hdr.FixupEndian();
  res = ::write(fd, &wav_hdr, sizeof(wav_hdr));
  if (res < 0)
    return ZX_ERR_IO;

  hdr.four_cc = DATA_FOUR_CC;
  hdr.length = static_cast<uint32_t>(payload_len);
  hdr.FixupEndian();
  res = ::write(fd, &hdr, sizeof(hdr));
  if (res < 0)
    return ZX_ERR_IO;

  return ZX_OK;
}

}  // namespace examples
