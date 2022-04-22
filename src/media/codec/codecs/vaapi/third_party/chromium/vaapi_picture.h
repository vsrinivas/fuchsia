// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_PICTURE_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_PICTURE_H_


#include <va/va.h>

#include "media/video/video_encode_accelerator.h"
#include "src/media/codec/codecs/vaapi/vaapi_utils.h"

class VaapiPicture : public media::H264Picture {
 public:
  std::shared_ptr<VASurface> va_surface;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_THIRD_PARTY_CHROMIUM_VAAPI_PICTURE_H_
