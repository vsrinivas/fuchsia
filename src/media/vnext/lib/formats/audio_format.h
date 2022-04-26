// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_AUDIO_FORMAT_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_AUDIO_FORMAT_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include "src/media/vnext/lib/formats/format_base.h"

namespace fmlib {

// Describes the format of an audio elementary stream, possibly compressed, possibly encrypted.
class AudioFormat : public FormatBase {
 public:
  AudioFormat() = default;

  AudioFormat(fuchsia::mediastreams::AudioSampleFormat sample_format, uint32_t channel_count,
              uint32_t frames_per_second, std::unique_ptr<Compression> compression = nullptr,
              std::unique_ptr<Encryption> encryption = nullptr);

  explicit AudioFormat(fuchsia::mediastreams::AudioFormat audio_format,
                       fuchsia::mediastreams::CompressionPtr compression = nullptr,
                       fuchsia::mediastreams::EncryptionPtr encryption = nullptr);

  AudioFormat(fuchsia::mediastreams::AudioFormat audio_format, const FormatBase& base);

  AudioFormat(AudioFormat&& other) = default;

  AudioFormat& operator=(AudioFormat&& other) = default;

  // Returns this audio format, without compression or encryption information, as a
  // |fuchsia::mediastreams::AudioFormat|.
  fuchsia::mediastreams::AudioFormat fidl() const { return fidl::Clone(fidl_); }
  operator fuchsia::mediastreams::AudioFormat() const { return fidl::Clone(fidl_); }

  AudioFormat Clone() const { return AudioFormat(*this); }

  fuchsia::mediastreams::AudioSampleFormat sample_format() const { return fidl_.sample_format; }

  uint32_t channel_count() const { return fidl_.channel_count; }

  uint32_t frames_per_second() const { return fidl_.frames_per_second; }

  // Returns the size in bytes of a sample.
  uint32_t bytes_per_sample() const;

  // Returns the size in bytes of a frame.
  uint32_t bytes_per_frame() const;

  // Returns the size in frames of a clip in this format of the given duration. Rounds up.
  uint64_t frames_per(zx::duration duration) const;

  // Returns the size in bytes of a clip in this format of the given duration. Rounds up to the
  // nearest frame size.
  uint64_t bytes_per(zx::duration duration) const;

 private:
  // We don't expose this, because we want copies to require a Clone call.
  explicit AudioFormat(const AudioFormat& other);

  fuchsia::mediastreams::AudioFormat fidl_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_AUDIO_FORMAT_H_
