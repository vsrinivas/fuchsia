// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/simple-codec/simple-codec-helper.h>

#include <ddk/debug.h>

namespace audio {

bool IsDaiFormatSupported(const DaiFormat& format,
                          const std::vector<DaiSupportedFormats>& supported) {
  for (size_t i = 0; i < supported.size(); ++i) {
    if (IsDaiFormatSupported(format, supported[i])) {
      return true;
    }
  }
  return false;
}

bool IsDaiFormatSupported(const DaiFormat& format, const DaiSupportedFormats& supported) {
  const sample_format_t sample_format = format.sample_format;
  const frame_format_t frame_format = format.frame_format;
  const uint32_t frame_rate = format.frame_rate;
  const uint8_t bits_per_sample = format.bits_per_sample;
  const uint8_t bits_per_channel = format.bits_per_channel;
  size_t i = 0;
  for (i = 0; i < supported.number_of_channels.size() &&
              supported.number_of_channels[i] != format.number_of_channels;
       ++i) {
  }
  if (i == supported.number_of_channels.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted number of channels");
    return false;
  }

  for (i = 0; i < supported.sample_formats.size() && supported.sample_formats[i] != sample_format;
       ++i) {
  }
  if (i == supported.sample_formats.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted sample format");
    return false;
  }

  for (i = 0; i < supported.frame_formats.size() && supported.frame_formats[i] != frame_format;
       ++i) {
  }
  if (i == supported.frame_formats.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted frame format");
    return false;
  }

  for (i = 0; i < supported.frame_rates.size() && supported.frame_rates[i] != frame_rate; ++i) {
  }
  if (i == supported.frame_rates.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted sample rate");
    return false;
  }

  for (i = 0;
       i < supported.bits_per_sample.size() && supported.bits_per_sample[i] != bits_per_sample;
       ++i) {
  }
  if (i == supported.bits_per_sample.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted bits per sample");
    return false;
  }

  for (i = 0;
       i < supported.bits_per_channel.size() && supported.bits_per_channel[i] != bits_per_channel;
       ++i) {
  }
  if (i == supported.bits_per_channel.size()) {
    zxlogf(DEBUG, "SimpleCodec did not find wanted bits per channel");
    return false;
  }

  if (bits_per_sample > bits_per_channel) {
    zxlogf(DEBUG, "SimpleCodec found bits per sample (%u) too big for the bits per channel (%u)",
           bits_per_sample, bits_per_channel);
    return false;
  }

  return true;
}

}  // namespace audio
