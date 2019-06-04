// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "raw_audio.h"

#include <endian.h>
#include <src/lib/fxl/logging.h>

#include <fstream>
#include <iostream>

const uint32_t kAUHeaderMagicConstant = 0x2e736e64;
const uint32_t kLinear16BitSampleCode = 3;
const uint32_t kDataSizeUnknown = 0xffffffff;
const char kPcmMimeType[] = "audio/pcm";

// static
RawAudio RawAudio::FromAUFile(const std::string& filename) {
  std::fstream input_file(filename,
                          std::ios::binary | std::ios::in | std::ios::ate);
  FXL_CHECK(input_file.is_open())
      << "Could not open " << filename << " for reading.";

  const size_t file_size = input_file.tellg();
  input_file.seekg(0);

  FXL_CHECK(file_size > sizeof(uint32_t) * 6)
      << "File is not big enough for AU header.";

  uint32_t au_header[6];
  input_file.read(reinterpret_cast<char*>(&au_header[0]), sizeof(uint32_t) * 6);

  // All AU data is big endian.
  for (size_t i = 0; i < 6; ++i) {
    au_header[i] = betoh32(au_header[i]);
  }

  FXL_CHECK(au_header[0] == kAUHeaderMagicConstant)
      << filename << " is not an AU file.";
  FXL_CHECK(au_header[3] == kLinear16BitSampleCode)
      << "Only 16 bit linear samples are supported.";

  const uint32_t data_offset = au_header[1];
  const uint32_t data_size = au_header[2];
  FXL_CHECK(data_size != kDataSizeUnknown &&
            data_size <= file_size - data_offset);

  const uint32_t frequency = au_header[4];
  const uint32_t channels = au_header[5];

  std::vector<uint8_t> data(data_size, 0);
  input_file.seekg(data_offset);
  input_file.read(reinterpret_cast<char*>(&data[0]), data_size);
  int16_t* samples = reinterpret_cast<int16_t*>(&data[0]);
  // All AU data is big endian.
  for (size_t i = 0; i < data.size() / sizeof(int16_t); ++i) {
    samples[i] = betoh16(samples[i]);
  }

  return RawAudio({.frequency = frequency, .channels = channels},
                  std::move(data));
}

RawAudio::CodecInput RawAudio::BuildCodecInput(
    size_t max_frames_per_packet) const {
  auto interval = max_frames_per_packet * frame_size();
  std::vector<size_t> payload_offsets;
  for (size_t i = 0; i < data_.size(); i += interval) {
    payload_offsets.push_back(i);
  }

  fuchsia::media::PcmFormat pcm_format{
      .pcm_mode = fuchsia::media::AudioPcmMode::LINEAR,
      .bits_per_sample = 16,
      .frames_per_second = layout_.frequency,
      .channel_map = {fuchsia::media::AudioChannelId::CF}};

  fuchsia::media::FormatDetails format;
  format.set_format_details_version_ordinal(0);
  format.mutable_domain()->audio().uncompressed().set_pcm(pcm_format);
  format.set_mime_type(kPcmMimeType);

  return {.data = data_,
          .payload_offsets = std::move(payload_offsets),
          .format = std::move(format)};
}

RawAudio::RawAudio(SignedLinear16BitLayout layout, std::vector<uint8_t> data)
    : layout_(layout), data_(std::move(data)) {}

size_t RawAudio::frame_size() const {
  // One sample per channel per frame.
  return layout_.channels * sizeof(int16_t);
}

size_t RawAudio::frame_count() const { return data_.size() / frame_size(); }
