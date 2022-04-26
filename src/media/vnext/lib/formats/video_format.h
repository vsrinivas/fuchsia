// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_FORMATS_VIDEO_FORMAT_H_
#define SRC_MEDIA_VNEXT_LIB_FORMATS_VIDEO_FORMAT_H_

#include <fuchsia/mediastreams/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/media/vnext/lib/formats/format_base.h"

namespace fmlib {

// Describes the format of a video elementary stream, possibly compressed, possibly encrypted.
class VideoFormat : public FormatBase {
 public:
  VideoFormat() = default;

  VideoFormat(fuchsia::mediastreams::PixelFormat pixel_format,
              fuchsia::mediastreams::ColorSpace color_space, fuchsia::math::Size coded_size,
              fuchsia::math::Size display_size, fuchsia::math::SizePtr aspect_ratio,
              std::unique_ptr<Compression> compression = nullptr,
              std::unique_ptr<Encryption> encryption = nullptr);

  explicit VideoFormat(fuchsia::mediastreams::VideoFormat video_format,
                       fuchsia::mediastreams::CompressionPtr compression = nullptr,
                       fuchsia::mediastreams::EncryptionPtr encryption = nullptr);

  VideoFormat(fuchsia::mediastreams::VideoFormat video_format, const FormatBase& base);

  VideoFormat(VideoFormat&& other) = default;

  VideoFormat& operator=(VideoFormat&& other) = default;

  fuchsia::mediastreams::VideoFormat fidl() const { return fidl::Clone(fidl_); }

  operator fuchsia::mediastreams::VideoFormat() const { return fidl::Clone(fidl_); }

  VideoFormat Clone() const { return VideoFormat(*this); }

  fuchsia::mediastreams::PixelFormat pixel_format() const { return fidl_.pixel_format; }

  fuchsia::sysmem::PixelFormat sysmem_pixel_format() const;

  fuchsia::mediastreams::ColorSpace color_space() const { return fidl_.color_space; }

  fuchsia::sysmem::ColorSpace sysmem_color_space() const;

  fuchsia::math::Size coded_size() const { return fidl_.coded_size; }

  fuchsia::math::Size display_size() const { return fidl_.display_size; }

  const fuchsia::math::SizePtr& aspect_ratio() const { return fidl_.aspect_ratio; }

 private:
  // We don't expose this, because we want copies to require a Clone call.
  VideoFormat(const VideoFormat& other);

  fuchsia::mediastreams::VideoFormat fidl_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_FORMATS_VIDEO_FORMAT_H_
