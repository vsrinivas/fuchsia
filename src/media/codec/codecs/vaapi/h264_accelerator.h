// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_H264_ACCELERATOR_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_H264_ACCELERATOR_H_

#include "media/gpu/h264_decoder.h"
#include "src/lib/fxl/macros.h"
#include "vaapi_utils.h"

class CodecAdapterVaApiDecoder;

class VaapiH264Picture : public media::H264Picture {
 public:
  explicit VaapiH264Picture(scoped_refptr<VASurface> va_surface);

  VaapiH264Picture(const VaapiH264Picture&) = delete;
  VaapiH264Picture& operator=(const VaapiH264Picture&) = delete;

  scoped_refptr<VASurface> va_surface() const { return va_surface_; }
  VASurfaceID GetVASurfaceID() const { return va_surface_->id(); }

  ~VaapiH264Picture() override;

 private:
  scoped_refptr<VASurface> va_surface_;
};

class H264Accelerator : public media::H264Decoder::H264Accelerator {
 public:
  explicit H264Accelerator(CodecAdapterVaApiDecoder* adapter) : adapter_(adapter) {}

  scoped_refptr<media::H264Picture> CreateH264Picture(bool is_for_output) override;

  Status SubmitFrameMetadata(const media::H264SPS* sps, const media::H264PPS* pps,
                             const media::H264DPB& dpb,
                             const media::H264Picture::Vector& ref_pic_listp0,
                             const media::H264Picture::Vector& ref_pic_listb0,
                             const media::H264Picture::Vector& ref_pic_listb1,
                             scoped_refptr<media::H264Picture> pic) override;

  Status SubmitSlice(const media::H264PPS* pps, const media::H264SliceHeader* slice_hdr,
                     const media::H264Picture::Vector& ref_pic_list0,
                     const media::H264Picture::Vector& ref_pic_list1,
                     scoped_refptr<media::H264Picture> pic, const uint8_t* data, size_t size,
                     const std::vector<media::SubsampleEntry>& subsamples) override;

  Status SubmitDecode(scoped_refptr<media::H264Picture> pic) override;

  bool OutputPicture(scoped_refptr<media::H264Picture> pic) override;

  void Reset() override { slice_buffers_.clear(); }

  Status SetStream(base::span<const uint8_t> stream,
                   const media::DecryptConfig* decrypt_config) override {
    return Status::kOk;
  }

 private:
  CodecAdapterVaApiDecoder* adapter_;
  std::vector<ScopedBufferID> slice_buffers_;
};

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_H264_ACCELERATOR_H_
