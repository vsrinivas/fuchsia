// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_ACCELERATOR_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_ACCELERATOR_H_

#include <memory>

#include <va/va.h>

#include "media/parsers/jpeg_parser.h"
#include "mjpeg_decoder.h"
#include "vaapi_utils.h"

class CodecAdapterVaApiDecoder;

class VaapiJpegPicture : public media::JPEGPicture {
 public:
  explicit VaapiJpegPicture(std::shared_ptr<VASurface> va_surface);
  ~VaapiJpegPicture() override;

  // Disallow copying
  VaapiJpegPicture(const VaapiJpegPicture&) = delete;
  VaapiJpegPicture& operator=(const VaapiJpegPicture&) = delete;

  std::shared_ptr<VASurface> va_surface() const { return va_surface_; }
  VASurfaceID GetVASurfaceID() const { return va_surface_->id(); }

 private:
  std::shared_ptr<VASurface> va_surface_;
};

class MJPEGAccelerator : public media::MJPEGDecoder::MJPEGAccelerator {
 public:
  explicit MJPEGAccelerator(CodecAdapterVaApiDecoder* adapter);
  ~MJPEGAccelerator() override;

  // Disallow copying
  MJPEGAccelerator(const MJPEGAccelerator&) = delete;
  MJPEGAccelerator& operator=(const MJPEGAccelerator&) = delete;

  std::shared_ptr<media::JPEGPicture> CreateJPEGPicture() override;
  Status SubmitDecode(std::shared_ptr<media::JPEGPicture> picture,
                      const media::JpegParseResult& parse_result) override;

  // Outputs the decoded picture
  bool OutputPicture(std::shared_ptr<media::JPEGPicture> picture) override;

 private:
  // Populates the |VAPictureParameterBufferJPEGBaseline| struct with the given parameters from the
  // decoded |JpegFrameHeader|.
  static void PopulatePictureParameterBuffer(const media::JpegFrameHeader& frame_header,
                                             VAPictureParameterBufferJPEGBaseline& pic_param);

  // Populates the |VAIQMatrixBufferJPEGBaseline| struct with the given quantization tables from the
  // decoded |JpegQuantizationTable|.
  static void PopulateIQMatrix(
      const media::JpegQuantizationTable q_table[media::kJpegMaxQuantizationTableNum],
      VAIQMatrixBufferJPEGBaseline matrix_buffer);

  // Populates the |VAHuffmanTableBufferJPEGBaseline| struct with the given Huffman tables from the
  // decoded |JpegHuffmanTable|. If the Huffman tables were not specified by the decoded JPEG
  // header, the default Huffman tables will be used.
  static void PopulateHuffmanTable(
      const media::JpegHuffmanTable dc_table[media::kJpegMaxHuffmanTableNumBaseline],
      const media::JpegHuffmanTable ac_table[media::kJpegMaxHuffmanTableNumBaseline],
      VAHuffmanTableBufferJPEGBaseline& huffman_table);

  // Populates the |VASliceParameterBufferJPEGBaseline| struct with the given slice parameters from
  // the decoded |JpegParseResult|.
  static void PopulateSliceParameters(const media::JpegParseResult& parse_result,
                                      VASliceParameterBufferJPEGBaseline& slice_param);

  // The adapter which owns this accelerator. Callbacks will be made to this adapter.
  CodecAdapterVaApiDecoder* adapter_;
};

#endif /* SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_ACCELERATOR_H_ */
