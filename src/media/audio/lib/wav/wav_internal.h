// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_WAV_WAV_INTERNAL_H_
#define SRC_MEDIA_AUDIO_LIB_WAV_WAV_INTERNAL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>
#include <zircon/types.h>

namespace media::audio::wav::internal {

//
// Struct and const definitions related to RIFF file format
//

// Encode a 32-bit 'fourcc' value from these 4 byte values
inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// Return a displayable string of the fourcc
inline std::string fourcc_to_string(uint32_t fourcc) {
  return std::string() + char(fourcc & 0x0FF) + char(fourcc >> 8 & 0x0FF) +
         char(fourcc >> 16 & 0x0FF) + char(fourcc >> 24 & 0x0FF);
}

// clang-format off
constexpr uint32_t RIFF_FOUR_CC = make_fourcc('R', 'I', 'F', 'F');
constexpr uint32_t WAVE_FOUR_CC = make_fourcc('W', 'A', 'V', 'E');
constexpr uint32_t FMT_FOUR_CC  = make_fourcc('f', 'm', 't', ' ');
constexpr uint32_t DATA_FOUR_CC = make_fourcc('d', 'a', 't', 'a');
constexpr uint16_t FORMAT_LPCM  = 0x0001;
constexpr uint16_t FORMAT_FLOAT = 0x0003;
// clang-format on

// The RIFF file specification (and the child specification for WAV content)
// defines the layout and contents of WAV audio files.
//
// RIFF files consist of so-called _chunks_ (self-describing sections of the
// file). These files begin with a RIFF header chunk that describes the primary
// format of the file contents, followed by the data itself (in a chunk of its
// own). Additional chunks may also be present, containing metadata and/or other
// information to support optional features. Because all chunks include a length
// field, any unknown chunks can be safely skipped by file readers.
//
// The WAV file format specifies an initial 'RIFF' chunk of type 'WAVE' (length
// 24), followed by two required Subchunks: 'fmt ' (length 24) and 'data'
// (length 8 + the size of the subsequent audio data). Audio data should
// immediately follow these first 8 bytes of the 'data' subchunk. Once the
// entirety of audio data has been written into the file, the 'length' field for
// the 'data' subchunk should be updated with the number of bytes of audio.
// Likewise, the overall length for the parent 'RIFF' chunk (which conceptually
// contains the two 'fmt ' and 'data' subchunks) must be updated at this point,
// to describe its total size (including subchunk headers and the audio data).
// Thus, although all audio data follows the file headers, we must update the
// headers once all audio has been written.
//
// ** Note, lest our RiffChunkHeader struct definition mislead the uninformed **
// These struct definitions actually conceptually relocate the final 32-bit
// value of the initial RIFF chunk (the RIFF format-type) into the subsequent
// 'fmt ' subchunk instead. Because the sequence of fields is maintained, this
// does not create a problem. We do this so that we can reuse our RIFF struct
// definition for the 'data' subchunk as well.
struct __PACKED RiffChunkHeader {
  uint32_t four_cc;
  uint32_t length = 0;

  // RIFF files are stored in little-endian, regardless of host-architecture.
  void FixupEndianForWriting() {
    four_cc = htole32(four_cc);
    length = htole32(length);
  }
  void FixupEndianForReading() {
    four_cc = letoh32(four_cc);
    length = letoh32(length);
  }
};

// As mentioned above, the WAVE_FOUR_CC is actually a menber of the previous
// RIFF chunk, but we include it here so that we can manage our parent 'RIFF'
// chunk and our 'data' subchunk with common code.
struct __PACKED WavHeader {
  uint32_t wave_four_cc = WAVE_FOUR_CC;
  uint32_t fmt_four_cc = FMT_FOUR_CC;
  uint32_t fmt_chunk_len = sizeof(WavHeader) - offsetof(WavHeader, format);
  uint16_t format = 0;
  uint16_t channel_count = 0;
  uint32_t frame_rate = 0;
  uint32_t average_byte_rate = 0;
  uint16_t frame_size = 0;
  uint16_t bits_per_sample = 0;

  // RIFF files are stored in little-endian, regardless of host-architecture.
  void FixupEndianForWriting() {
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
  void FixupEndianForReading() {
    // clang-format off
    wave_four_cc      = letoh32(wave_four_cc);
    fmt_four_cc       = letoh32(fmt_four_cc);
    fmt_chunk_len     = letoh32(fmt_chunk_len);
    format            = letoh16(format);
    channel_count     = letoh16(channel_count);
    frame_rate        = letoh32(frame_rate);
    average_byte_rate = letoh32(average_byte_rate);
    frame_size        = letoh16(frame_size);
    bits_per_sample   = letoh16(bits_per_sample);
    // clang-format on
  }

  void set_format(fuchsia::media::AudioSampleFormat f) {
    format = (f == fuchsia::media::AudioSampleFormat::FLOAT) ? FORMAT_FLOAT : FORMAT_LPCM;
  }
  fuchsia::media::AudioSampleFormat sample_format() const {
    if (format == FORMAT_FLOAT) {
      return fuchsia::media::AudioSampleFormat::FLOAT;
    }
    switch (bits_per_sample) {
      case 8:
        return fuchsia::media::AudioSampleFormat::UNSIGNED_8;
      case 16:
        return fuchsia::media::AudioSampleFormat::SIGNED_16;
      case 24:
      case 32:
        return fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    }
    FX_CHECK(false) << "format " << format << " bits_per_sample " << bits_per_sample;
    __UNREACHABLE;
  }
};

}  // namespace media::audio::wav::internal

#endif  // SRC_MEDIA_AUDIO_LIB_WAV_WAV_INTERNAL_H_
