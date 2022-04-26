// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_MEDIA_FORMAT_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_MEDIA_FORMAT_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/formats/audio_format.h"
#include "src/media/vnext/lib/formats/compression.h"
#include "src/media/vnext/lib/formats/encryption.h"
#include "src/media/vnext/lib/formats/format_base.h"
#include "src/media/vnext/lib/formats/video_format.h"

namespace fmlib {

// Describes the format of an elementary stream, possibly compressed, possibly encrypted.
class MediaFormat : public FormatBase {
 public:
  explicit MediaFormat(fuchsia::mediastreams::MediaFormat media_format,
                       fuchsia::mediastreams::CompressionPtr compression = nullptr,
                       fuchsia::mediastreams::EncryptionPtr encryption = nullptr);

  MediaFormat(MediaFormat&& other) = default;

  explicit MediaFormat(AudioFormat audio_format);

  explicit MediaFormat(VideoFormat video_format);

  MediaFormat& operator=(MediaFormat&& other) = default;

  // Returns this media format, without compression or encryption information, as a
  // |fuchsia::mediastreams::MediaFormat|.
  fuchsia::mediastreams::MediaFormat fidl() const { return fidl::Clone(fidl_); }
  operator fuchsia::mediastreams::MediaFormat() const { return fidl::Clone(fidl_); }

  MediaFormat Clone() const { return MediaFormat(*this); }

  fuchsia::mediastreams::MediaFormat::Tag Which() const { return fidl_.Which(); }

  bool is_audio() const { return fidl_.is_audio(); }

  bool is_video() const { return fidl_.is_video(); }

  AudioFormat audio() const {
    FX_CHECK(is_audio());
    return AudioFormat(fidl::Clone(fidl_.audio()), *this);
  }

  VideoFormat video() const {
    FX_CHECK(is_video());
    return VideoFormat(fidl::Clone(fidl_.video()), *this);
  }

 private:
  // We don't expose this, because we want copies to require a Clone call.
  explicit MediaFormat(const MediaFormat& other);

  fuchsia::mediastreams::MediaFormat fidl_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_MEDIA_FORMAT_H_
