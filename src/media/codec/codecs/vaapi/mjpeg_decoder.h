// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_DECODER_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_DECODER_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "media/gpu/accelerated_video_decoder.h"
#include "media/gpu/codec_picture.h"
#include "media/parsers/jpeg_parser.h"

namespace media {

class JPEGPicture : public CodecPicture {
 public:
  JPEGPicture() = default;

  // Disallow copying
  JPEGPicture(const JPEGPicture&) = delete;
  JPEGPicture& operator=(const JPEGPicture&) = delete;

  void set_frame_header(const media::JpegFrameHeader& frame_header) {
    frame_header_ = frame_header;
  }

  const media::JpegFrameHeader& frame_header() const { return frame_header_; }

 protected:
  ~JPEGPicture() override = default;

 private:
  media::JpegFrameHeader frame_header_;
};

// Takes a MJPEG stream and parses and decodes the images contained within that stream.
class MJPEGDecoder : public media::AcceleratedVideoDecoder {
 public:
  class MJPEGAccelerator {
   public:
    enum class Status {
      // Operation completed successfully.
      kOk,

      // Operation failed.
      kFail,
    };

    MJPEGAccelerator();
    virtual ~MJPEGAccelerator();

    // Disallow copying
    MJPEGAccelerator(const MJPEGAccelerator&) = delete;
    MJPEGAccelerator& operator=(const MJPEGAccelerator&) = delete;

    virtual std::shared_ptr<JPEGPicture> CreateJPEGPicture() = 0;
    virtual Status SubmitDecode(std::shared_ptr<media::JPEGPicture> picture,
                                const media::JpegParseResult& parse_result) = 0;
    virtual bool OutputPicture(std::shared_ptr<JPEGPicture> picture) = 0;
  };

  explicit MJPEGDecoder(std::unique_ptr<MJPEGAccelerator> accelerator);
  ~MJPEGDecoder() override;

  // Disallow copying
  MJPEGDecoder(const MJPEGDecoder&) = delete;
  MJPEGDecoder& operator=(const MJPEGDecoder&) = delete;

  // AcceleratedVideoDecoder implementation.
  void SetStream(int32_t id, const DecoderBuffer& decoder_buffer) override;
  [[nodiscard]] bool Flush() override;
  void Reset() override;
  [[nodiscard]] DecodeResult Decode() override;
  gfx::Size GetPicSize() const override;
  gfx::Rect GetVisibleRect() const override;
  VideoCodecProfile GetProfile() const override;
  uint8_t GetBitDepth() const override;
  size_t GetRequiredNumOfPictures() const override;
  size_t GetNumReferenceFrames() const override;
  bool IsCurrentFrameKeyframe() const override;

 private:
  static constexpr uint32_t kInvalidVaFormat = 0u;
  static uint32_t VaFormatFromFrameHeader(const JpegFrameHeader& frame_header);

  // Stream settings
  int32_t stream_id_;
  const uint8_t* stream_data_;
  size_t stream_bytes_left_;

  // Saved parse result when returning |kConfigChange| signaling a resolution change
  std::optional<JpegParseResult> pending_parse_result_;

  // Current coded resolution.
  gfx::Size pic_size_;

  // Visible rectangle on the most recent allocation.
  gfx::Rect visible_rect_;

  // Underlying hardware accelerator that will process JPEG decoding operations
  const std::unique_ptr<MJPEGAccelerator> accelerator_;
};

}  // namespace media

#endif /* SRC_MEDIA_CODEC_CODECS_VAAPI_MJPEG_DECODER_H_ */
