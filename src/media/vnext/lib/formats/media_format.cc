// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/formats/media_format.h"

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

namespace fmlib {

MediaFormat::MediaFormat(fuchsia::mediastreams::MediaFormat media_format,
                         fuchsia::mediastreams::CompressionPtr compression,
                         fuchsia::mediastreams::EncryptionPtr encryption)
    : FormatBase(std::move(compression), std::move(encryption)), fidl_(std::move(media_format)) {}

MediaFormat::MediaFormat(const MediaFormat& other)
    : FormatBase(other), fidl_(fidl::Clone(other.fidl_)) {}

MediaFormat::MediaFormat(AudioFormat audio_format)
    : FormatBase(std::move(audio_format)),
      fidl_(fuchsia::mediastreams::MediaFormat::WithAudio(std::move(audio_format))) {}

MediaFormat::MediaFormat(VideoFormat video_format)
    : FormatBase(std::move(video_format)),
      fidl_(fuchsia::mediastreams::MediaFormat::WithVideo(std::move(video_format))) {}

}  // namespace fmlib
