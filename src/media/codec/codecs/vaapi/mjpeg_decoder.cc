// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mjpeg_decoder.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

#include <memory>

#include <va/va.h>

#include "geometry.h"

namespace media {

MJPEGDecoder::MJPEGAccelerator::MJPEGAccelerator() = default;
MJPEGDecoder::MJPEGAccelerator::~MJPEGAccelerator() = default;

MJPEGDecoder::MJPEGDecoder(std::unique_ptr<MJPEGAccelerator> accelerator)
    : accelerator_(std::move(accelerator)) {}

MJPEGDecoder::~MJPEGDecoder() = default;

void MJPEGDecoder::SetStream(int32_t id, const DecoderBuffer& decoder_buffer) {
  stream_id_ = id;
  stream_data_ = decoder_buffer.data();
  stream_bytes_left_ = decoder_buffer.data_size();

  FX_DCHECK(stream_data_);
  FX_DCHECK(stream_bytes_left_);
}

bool MJPEGDecoder::Flush() {
  Reset();
  return true;
}

void MJPEGDecoder::Reset() { pending_parse_result_.reset(); }

MJPEGDecoder::DecodeResult MJPEGDecoder::Decode() {
  if (!pending_parse_result_.has_value()) {
    JpegParseResult parse_result;
    if (!ParseJpegPicture(stream_data_, stream_bytes_left_, &parse_result)) {
      FX_SLOG(WARNING, "ParseJpegPicture failed");
      return kDecodeError;
    }

    pending_parse_result_ = std::move(parse_result);
  }

  ZX_ASSERT(pending_parse_result_.has_value());
  auto clean_pending_result = fit::defer([this] { pending_parse_result_.reset(); });

  const gfx::Size new_coded_size(
      safemath::strict_cast<int>(pending_parse_result_->frame_header.coded_width),
      safemath::strict_cast<int>(pending_parse_result_->frame_header.coded_height));

  const gfx::Rect new_visible_size(
      safemath::strict_cast<int>(pending_parse_result_->frame_header.visible_width),
      safemath::strict_cast<int>(pending_parse_result_->frame_header.visible_height));

  // TODO(stefanbossbaly): Currently we only support YUV420
  if (VaFormatFromFrameHeader(pending_parse_result_->frame_header) != VA_RT_FORMAT_YUV420) {
    return kDecodeError;
  }

  // Alert callee to a configuration change
  if ((pic_size_ != new_coded_size) || (visible_rect_ != new_visible_size)) {
    pic_size_ = new_coded_size;
    visible_rect_ = new_visible_size;

    // We won't output the frame this call, so save the pending parse result for the next |Decode()|
    // call.
    clean_pending_result.cancel();

    return kConfigChange;
  }

  std::shared_ptr<JPEGPicture> pic = accelerator_->CreateJPEGPicture();
  if (!pic) {
    return kRanOutOfSurfaces;
  }

  pic->set_frame_header(pending_parse_result_->frame_header);
  pic->set_visible_rect(new_visible_size);
  pic->set_bitstream_id(stream_id_);

  auto status = accelerator_->SubmitDecode(pic, pending_parse_result_.value());
  if (status != MJPEGAccelerator::Status::kOk) {
    return kDecodeError;
  }

  if (!accelerator_->OutputPicture(pic)) {
    return kDecodeError;
  }

  // TODO(stefanbossbaly): Right now we limit the caller of stream processor to only submit one
  // JPEG encoded frame at a time. We could update this implementation to allow the client to submit
  // fractional or multiple frames per buffer.
  return kRanOutOfStreamData;
}

gfx::Size MJPEGDecoder::GetPicSize() const { return pic_size_; }

gfx::Rect MJPEGDecoder::GetVisibleRect() const { return visible_rect_; }

VideoCodecProfile MJPEGDecoder::GetProfile() const {
  // TODO(stefanbossbaly): Fix this
  return VIDEO_CODEC_PROFILE_UNKNOWN;
}

uint8_t MJPEGDecoder::GetBitDepth() const { return 8; }

size_t MJPEGDecoder::GetRequiredNumOfPictures() const { return 1; }

size_t MJPEGDecoder::GetNumReferenceFrames() const { return 0; }

bool MJPEGDecoder::IsCurrentFrameKeyframe() const { return true; }

uint32_t MJPEGDecoder::VaFormatFromFrameHeader(const JpegFrameHeader& frame_header) {
  // There should be three components: 1 for grayscale and two for color.
  if (frame_header.num_components != 3u) {
    return kInvalidVaFormat;
  }

  const uint8_t y_plane_hori = frame_header.components[0u].horizontal_sampling_factor;
  const uint8_t y_plane_vert = frame_header.components[0u].vertical_sampling_factor;
  const uint8_t u_plane_hori = frame_header.components[1u].horizontal_sampling_factor;
  const uint8_t u_plane_vert = frame_header.components[1u].vertical_sampling_factor;
  const uint8_t v_plane_hori = frame_header.components[2u].horizontal_sampling_factor;
  const uint8_t v_plane_vert = frame_header.components[2u].vertical_sampling_factor;

  if ((u_plane_hori != 1u) || (u_plane_vert != 1u) || (v_plane_hori != 1u) ||
      (v_plane_vert != 1u)) {
    return kInvalidVaFormat;
  }

  if ((y_plane_hori == 2) && (y_plane_vert == 2)) {
    return VA_RT_FORMAT_YUV420;
  } else if ((y_plane_hori == 2) && (y_plane_vert == 1)) {
    return VA_RT_FORMAT_YUV422;
  } else if ((y_plane_hori == 1) && (y_plane_vert == 1)) {
    return VA_RT_FORMAT_YUV444;
  }

  return kInvalidVaFormat;
}

}  // namespace media
