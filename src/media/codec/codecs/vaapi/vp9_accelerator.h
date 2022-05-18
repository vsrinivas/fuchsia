// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_VP9_ACCELERATOR_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_VP9_ACCELERATOR_H_

#include <optional>

#include <src/lib/fxl/macros.h>

#include "media/gpu/vp9_decoder.h"
#include "vaapi_utils.h"

class CodecAdapterVaApiDecoder;

class VaapiVP9Picture : public media::VP9Picture {
 public:
  explicit VaapiVP9Picture(scoped_refptr<VASurface> va_surface);
  ~VaapiVP9Picture() override;

  VaapiVP9Picture(const VaapiVP9Picture&) = delete;
  VaapiVP9Picture& operator=(const VaapiVP9Picture&) = delete;

  scoped_refptr<VASurface> va_surface() const { return va_surface_; }
  VASurfaceID GetVASurfaceID() const { return va_surface_->id(); }

 private:
  // Since the Vp9Decoder will not call SubmitDecode() on duplicated pictures and instead only calls
  // OutputPicture() we can just create a VP9Picture object that has the same underlying surface.
  // The Vp9Decoder will then call OutputPicture() which will call vaSyncSurface() and then
  // ProcessOutput() on same underlying surface but at a different bitstream_id indicating a
  // different timestamp
  scoped_refptr<VP9Picture> CreateDuplicate() override {
    return std::make_shared<VaapiVP9Picture>(va_surface());
  }

  scoped_refptr<VASurface> va_surface_;
};

class VP9Accelerator : public media::VP9Decoder::VP9Accelerator {
 public:
  explicit VP9Accelerator(CodecAdapterVaApiDecoder* adapter);
  ~VP9Accelerator() override;

  VP9Accelerator(const VP9Accelerator&) = delete;
  VP9Accelerator& operator=(const VP9Accelerator&) = delete;

  scoped_refptr<media::VP9Picture> CreateVP9Picture() override;
  Status SubmitDecode(scoped_refptr<media::VP9Picture> pic, const media::Vp9SegmentationParams& seg,
                      const media::Vp9LoopFilterParams& lf,
                      const media::Vp9ReferenceFrameVector& reference_frames,
                      base::OnceClosure done_cb) override;

  bool OutputPicture(scoped_refptr<media::VP9Picture> pic) override;
  bool NeedsCompressedHeaderParsed() const override;
  bool GetFrameContext(scoped_refptr<media::VP9Picture> pic,
                       media::Vp9FrameContext* frame_ctx) override;

 private:
  CodecAdapterVaApiDecoder* adapter_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_VP9_ACCELERATOR_H_
