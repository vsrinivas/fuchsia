// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_GPU_VP9_PICTURE_H_
#define MEDIA_GPU_VP9_PICTURE_H_

#include <memory>
#include <optional>  // Fuchsia change: include optional library

#include "media/filters/vp9_parser.h"
#include "media/gpu/codec_picture.h"
// Fuchsia change: Remove libraries in favor of "chromium_utils.h"/"geometry.h"
#include "media/video/video_encode_accelerator.h"
//#include "third_party/abseil-cpp/absl/types/optional.h"
#include "chromium_utils.h"
#include "geometry.h"

namespace media {


class V4L2VP9Picture;
class VaapiVP9Picture;

class MEDIA_GPU_EXPORT VP9Picture : public CodecPicture {
 public:
  VP9Picture();

  VP9Picture(const VP9Picture&) = delete;
  VP9Picture& operator=(const VP9Picture&) = delete;

  // TODO(tmathmeyer) remove these and just use static casts everywhere.
// Fuchsia change: Remove AsV4L2VP9Picture and AsVaapiVP9Picture
#if 0
  virtual V4L2VP9Picture* AsV4L2VP9Picture();
  virtual VaapiVP9Picture* AsVaapiVP9Picture();
#endif

  // Create a duplicate instance and copy the data to it. It is used to support
  // VP9 show_existing_frame feature. Return the scoped_refptr pointing to the
  // duplicate instance, or nullptr on failure.
  scoped_refptr<VP9Picture> Duplicate();

  std::unique_ptr<Vp9FrameHeader> frame_hdr;

  // Fuchsia change: use std::optional instead of absl::optional
  std::optional<Vp9Metadata> metadata_for_encoding;

 protected:
  ~VP9Picture() override;

 private:
  // Create a duplicate instance.
  virtual scoped_refptr<VP9Picture> CreateDuplicate();
};

}  // namespace media

#endif  // MEDIA_GPU_VP9_PICTURE_H_
