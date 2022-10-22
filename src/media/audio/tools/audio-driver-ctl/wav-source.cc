// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "wav-source.h"

#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/fit/defer.h>
#include <stdio.h>
#include <zircon/assert.h>

#include <algorithm>

#include <fbl/algorithm.h>

zx_status_t WAVSource::Initialize(const char* filename, uint64_t channels_to_use_bitmask,
                                  Duration duration) {
  duration_ = duration;
  zx_status_t res = WAVCommon::Initialize(filename, InitMode::SOURCE);
  if (res != ZX_OK)
    return res;

  RIFFChunkHeader riff_hdr;
  WAVHeader wav_info;

  auto cleanup = fit::defer([&]() {
    Close();
    payload_len_ = 0;
  });

  // Read and sanity check the top level RIFF header
  res = Read(&riff_hdr, sizeof(riff_hdr));
  if (res != ZX_OK) {
    printf("Failed to read top level RIFF header!\n");
    return res;
  }
  riff_hdr.FixupEndian();

  if (riff_hdr.four_cc != RIFF_FOUR_CC) {
    printf("Missing expected 'RIFF' 4CC (expected 0x%08x got 0x%08x)\n", RIFF_FOUR_CC,
           riff_hdr.four_cc);
    return ZX_ERR_INVALID_ARGS;
  }

  // Read the WAVE header along with its required format chunk.
  res = Read(&wav_info, sizeof(wav_info));
  if (res != ZX_OK) {
    printf("Failed to read top level WAVE header!\n");
    return res;
  }
  wav_info.FixupEndian();

  if (wav_info.wave_four_cc != WAVE_FOUR_CC) {
    printf("Missing expected 'RIFF' 4CC (expected 0x%08x got 0x%08x)\n", WAVE_FOUR_CC,
           wav_info.wave_four_cc);
    return ZX_ERR_INVALID_ARGS;
  }

  if (wav_info.fmt_four_cc != FMT_FOUR_CC) {
    printf("Missing expected 'RIFF' 4CC (expected 0x%08x got 0x%08x)\n", FMT_FOUR_CC,
           wav_info.fmt_four_cc);
    return ZX_ERR_INVALID_ARGS;
  }

  if (!wav_info.frame_size) {
    printf("Bad frame size (%hu)\n", wav_info.frame_size);
    return ZX_ERR_INVALID_ARGS;
  }

  // Sanity check the format of the wave file.  This test app only supports a
  // limited subset of the possible formats.
  if (wav_info.format != FORMAT_LPCM) {
    printf("Unsupported format (0x%08hx) must be LPCM (0x%08hx)\n", wav_info.format, FORMAT_LPCM);
    return ZX_ERR_INVALID_ARGS;
  }

  switch (wav_info.bits_per_sample) {
    case 8:
      audio_format_.sample_format = AUDIO_SAMPLE_FORMAT_8BIT;
      break;
    case 16:
      audio_format_.sample_format = AUDIO_SAMPLE_FORMAT_16BIT;
      break;
    case 32:
      audio_format_.sample_format = AUDIO_SAMPLE_FORMAT_32BIT;
      break;
    default:
      printf("Unsupported bits per sample (%hu)\n", wav_info.bits_per_sample);
      return ZX_ERR_INVALID_ARGS;
  };

  audio_format_.frame_rate = wav_info.frame_rate;
  audio_format_.channels = wav_info.channel_count;
  audio_format_.channels_to_use_bitmask = channels_to_use_bitmask;

  // Skip any extra data in the format chunk
  size_t total_wav_hdr_size = wav_info.fmt_chunk_len + offsetof(WAVHeader, format);
  if (total_wav_hdr_size < sizeof(WAVHeader)) {
    printf("Bad format chunk length in WAV header (%u)\n", wav_info.fmt_chunk_len);
    return ZX_ERR_INVALID_ARGS;
  }

  if (total_wav_hdr_size > sizeof(WAVHeader)) {
    off_t delta = total_wav_hdr_size - sizeof(WAVHeader);
    if (::lseek(fd_, delta, SEEK_CUR) < 0) {
      printf("Error while attempt to skip %zu bytes of extra WAV header\n",
             static_cast<size_t>(delta));
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // Read and skip chunks until we find the data chunk.
  RIFFChunkHeader data_hdr;
  while (true) {
    res = Read(&data_hdr, sizeof(data_hdr));
    if (res != ZX_OK) {
      printf("Failed to find DATA chunk header\n");
      return res;
    }
    data_hdr.FixupEndian();

    if (data_hdr.four_cc == DATA_FOUR_CC)
      break;

    if (::lseek(fd_, data_hdr.length, SEEK_CUR) < 0) {
      printf("Error while attempt to skip %u bytes of 0x%08x chunk\n", data_hdr.length,
             data_hdr.four_cc);
      return ZX_ERR_INVALID_ARGS;
    }
  }

  // If the length of the data chunk is not a multiple of the frame size, log a
  // warning and truncate the length.
  uint16_t leftover;
  payload_len_ = data_hdr.length;
  leftover = static_cast<uint16_t>(payload_len_ % wav_info.frame_size);
  if (leftover) {
    printf("WARNING: Data chunk length (%u) not a multiple of frame size (%hu)\n", payload_len_,
           wav_info.frame_size);
    payload_len_ -= leftover;
  }

  cleanup.cancel();
  return ZX_OK;
}

zx_status_t WAVSource::GetFormat(Format* out_format) {
  if (fd_ < 0)
    return ZX_ERR_BAD_STATE;

  *out_format = audio_format_;
  return ZX_OK;
}

zx_status_t WAVSource::GetFrames(void* buffer, uint32_t buf_space, uint32_t* out_packed) {
  if ((buffer == nullptr) || (out_packed == nullptr))
    return ZX_ERR_INVALID_ARGS;

  if ((fd_ < 0) || finished())
    return ZX_ERR_BAD_STATE;

  const bool loop = std::holds_alternative<LoopingDoneCallback>(duration_);
  if (loop) {
    // We wrap around to allow looping.
    if (payload_played_ >= payload_len_) {
      payload_played_ = 0;
      Seek(0);
    }
  } else {
    ZX_DEBUG_ASSERT(payload_played_ < payload_len_);
  }
  uint32_t todo = std::min(buf_space, payload_len_ - payload_played_);
  zx_status_t res = Read(buffer, todo);
  if (res == ZX_OK) {
    payload_played_ += todo;
    *out_packed = todo;
  }

  return res;
}

bool WAVSource::finished() const {
  const bool loop = std::holds_alternative<LoopingDoneCallback>(duration_);
  if (loop) {
    return !std::get<LoopingDoneCallback>(duration_)();
  } else {
    return payload_played_ >= payload_len_;
  }
}
