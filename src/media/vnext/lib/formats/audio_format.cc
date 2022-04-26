// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/formats/audio_format.h"

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

AudioFormat::AudioFormat(fuchsia::mediastreams::AudioSampleFormat sample_format,
                         uint32_t channel_count, uint32_t frames_per_second,
                         std::unique_ptr<Compression> compression,
                         std::unique_ptr<Encryption> encryption)
    : FormatBase(std::move(compression), std::move(encryption)),
      fidl_{.sample_format = sample_format,
            .channel_count = channel_count,
            .frames_per_second = frames_per_second,
            .channel_layout = fuchsia::mediastreams::AudioChannelLayout::WithPlaceholder({})} {}

uint32_t AudioFormat::bytes_per_sample() const {
  switch (fidl_.sample_format) {
    case fuchsia::mediastreams::AudioSampleFormat::UNSIGNED_8:
      return 1;
    case fuchsia::mediastreams::AudioSampleFormat::SIGNED_16:
      return 2;
    case fuchsia::mediastreams::AudioSampleFormat::SIGNED_24_IN_32:
    case fuchsia::mediastreams::AudioSampleFormat::SIGNED_32:
    case fuchsia::mediastreams::AudioSampleFormat::FLOAT:
      return 4;
  }
}

AudioFormat::AudioFormat(fuchsia::mediastreams::AudioFormat audio_format,
                         fuchsia::mediastreams::CompressionPtr compression,
                         fuchsia::mediastreams::EncryptionPtr encryption)
    : FormatBase(std::move(compression), std::move(encryption)), fidl_(std::move(audio_format)) {}

AudioFormat::AudioFormat(fuchsia::mediastreams::AudioFormat audio_format, const FormatBase& base)
    : FormatBase(base), fidl_(std::move(audio_format)) {}

AudioFormat::AudioFormat(const AudioFormat& other)
    : FormatBase(other), fidl_(fidl::Clone(other.fidl_)) {}

uint32_t AudioFormat::bytes_per_frame() const { return bytes_per_sample() * fidl_.channel_count; }

uint64_t AudioFormat::frames_per(zx::duration duration) const {
  // TODO(dalesat): Avoid overflow.
  return ((duration.get() * fidl_.frames_per_second + ZX_SEC(1) - 1) / ZX_SEC(1));
}

uint64_t AudioFormat::bytes_per(zx::duration duration) const {
  return frames_per(duration) * bytes_per_frame();
}

}  // namespace fmlib
