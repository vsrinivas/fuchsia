// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vp9_accelerator.h"

#include <cstring>

#include "codec_adapter_vaapi_decoder.h"
#include "va/va.h"
#include "vaapi_utils.h"

VaapiVP9Picture::VaapiVP9Picture(scoped_refptr<VASurface> va_surface) : va_surface_(va_surface) {}

VaapiVP9Picture::~VaapiVP9Picture() = default;

VP9Accelerator::VP9Accelerator(CodecAdapterVaApiDecoder* adapter) : adapter_(adapter) {
  FX_DCHECK(adapter_);
}

VP9Accelerator::~VP9Accelerator() = default;

scoped_refptr<media::VP9Picture> VP9Accelerator::CreateVP9Picture() {
  auto surface = adapter_->GetVASurface();
  auto surface_ptr = std::make_shared<VaapiVP9Picture>(surface);
  return surface_ptr;
}

VP9Accelerator::Status VP9Accelerator::SubmitDecode(
    scoped_refptr<media::VP9Picture> pic, const media::Vp9SegmentationParams& seg,
    const media::Vp9LoopFilterParams& lf, const media::Vp9ReferenceFrameVector& reference_frames,
    base::OnceClosure done_cb) {
  // |done_cb| should be null as we return false from
  // NeedsCompressedHeaderParsed().
  DCHECK(!done_cb);

  const media::Vp9FrameHeader* frame_hdr = pic->frame_hdr.get();
  DCHECK(frame_hdr);

  VADecPictureParameterBufferVP9 pic_param{};
  VASliceParameterBufferVP9 slice_param{};
  VAStatus status = VA_STATUS_SUCCESS;

  auto checked_width = safemath::MakeCheckedNum(frame_hdr->frame_width).Cast<uint16_t>();
  auto checked_height = safemath::MakeCheckedNum(frame_hdr->frame_height).Cast<uint16_t>();
  if (!checked_width.IsValid() || !checked_height.IsValid()) {
    FX_SLOG(ERROR, "Invalid frame dimensions", KV("frame_width", frame_hdr->frame_width),
            KV("frame_height", frame_hdr->frame_height));
    return Status::kFail;
  }
  pic_param.frame_width = checked_width.ValueOrDie();
  pic_param.frame_height = checked_height.ValueOrDie();
  ZX_ASSERT(media::kVp9NumRefFrames == std::size(pic_param.reference_frames));
  for (size_t i = 0; i < std::size(pic_param.reference_frames); ++i) {
    auto ref_pic = reference_frames.GetFrame(i);
    if (ref_pic) {
      pic_param.reference_frames[i] =
          static_cast<VaapiVP9Picture*>(ref_pic.get())->GetVASurfaceID();
    } else {
      pic_param.reference_frames[i] = VA_INVALID_SURFACE;
    }
  }

// Ignore warnings about conversions, since the caller should have validated
// the actual sizes of the parameters.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#define FHDR_TO_PP_PF1(a) pic_param.pic_fields.bits.a = frame_hdr->a
#define FHDR_TO_PP_PF2(a, b) pic_param.pic_fields.bits.a = b
  FHDR_TO_PP_PF2(subsampling_x, frame_hdr->subsampling_x == 1);
  FHDR_TO_PP_PF2(subsampling_y, frame_hdr->subsampling_y == 1);
  FHDR_TO_PP_PF2(frame_type, frame_hdr->IsKeyframe() ? 0 : 1);
  FHDR_TO_PP_PF1(show_frame);
  FHDR_TO_PP_PF1(error_resilient_mode);
  FHDR_TO_PP_PF1(intra_only);
  FHDR_TO_PP_PF1(allow_high_precision_mv);
  FHDR_TO_PP_PF2(mcomp_filter_type, frame_hdr->interpolation_filter);
  FHDR_TO_PP_PF1(frame_parallel_decoding_mode);
  FHDR_TO_PP_PF1(reset_frame_context);
  FHDR_TO_PP_PF1(refresh_frame_context);
  FHDR_TO_PP_PF2(frame_context_idx, frame_hdr->frame_context_idx_to_save_probs);
  FHDR_TO_PP_PF2(segmentation_enabled, seg.enabled);
  FHDR_TO_PP_PF2(segmentation_temporal_update, seg.temporal_update);
  FHDR_TO_PP_PF2(segmentation_update_map, seg.update_map);
  FHDR_TO_PP_PF2(last_ref_frame, frame_hdr->ref_frame_idx[0]);
  FHDR_TO_PP_PF2(last_ref_frame_sign_bias,
                 frame_hdr->ref_frame_sign_bias[media::Vp9RefType::VP9_FRAME_LAST]);
  FHDR_TO_PP_PF2(golden_ref_frame, frame_hdr->ref_frame_idx[1]);
  FHDR_TO_PP_PF2(golden_ref_frame_sign_bias,
                 frame_hdr->ref_frame_sign_bias[media::Vp9RefType::VP9_FRAME_GOLDEN]);
  FHDR_TO_PP_PF2(alt_ref_frame, frame_hdr->ref_frame_idx[2]);
  FHDR_TO_PP_PF2(alt_ref_frame_sign_bias,
                 frame_hdr->ref_frame_sign_bias[media::Vp9RefType::VP9_FRAME_ALTREF]);
  FHDR_TO_PP_PF2(lossless_flag, frame_hdr->quant_params.IsLossless());
#undef FHDR_TO_PP_PF2
#undef FHDR_TO_PP_PF1

  pic_param.filter_level = lf.level;
  pic_param.sharpness_level = lf.sharpness;
  pic_param.log2_tile_rows = frame_hdr->tile_rows_log2;
  pic_param.log2_tile_columns = frame_hdr->tile_cols_log2;
  pic_param.frame_header_length_in_bytes = frame_hdr->uncompressed_header_size;
  pic_param.first_partition_size = frame_hdr->header_size_in_bytes;

  SafeArrayMemcpy(pic_param.mb_segment_tree_probs, seg.tree_probs);
  SafeArrayMemcpy(pic_param.segment_pred_probs, seg.pred_probs);

  pic_param.profile = frame_hdr->profile;
  pic_param.bit_depth = frame_hdr->bit_depth;
  DCHECK((pic_param.profile == 0 && pic_param.bit_depth == 8) ||
         (pic_param.profile == 2 && pic_param.bit_depth == 10));

  slice_param.slice_data_size = frame_hdr->frame_size;
  slice_param.slice_data_offset = 0;
  slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;

  static_assert(std::extent<decltype(media::Vp9SegmentationParams::feature_enabled)>() ==
                    std::extent<decltype(slice_param.seg_param)>(),
                "seg_param array of incorrect size");
  for (size_t i = 0; i < std::size(slice_param.seg_param); ++i) {
    VASegmentParameterVP9& seg_param = slice_param.seg_param[i];
#define SEG_TO_SP_SF(a, b) seg_param.segment_flags.fields.a = b
    SEG_TO_SP_SF(segment_reference_enabled,
                 seg.FeatureEnabled(i, media::Vp9SegmentationParams::SEG_LVL_REF_FRAME));
    SEG_TO_SP_SF(segment_reference,
                 seg.FeatureData(i, media::Vp9SegmentationParams::SEG_LVL_REF_FRAME));
    SEG_TO_SP_SF(segment_reference_skipped,
                 seg.FeatureEnabled(i, media::Vp9SegmentationParams::SEG_LVL_SKIP));
#undef SEG_TO_SP_SF
#pragma clang diagnostic pop

    SafeArrayMemcpy(seg_param.filter_level, lf.lvl[i]);

    seg_param.luma_dc_quant_scale = seg.y_dequant[i][0];
    seg_param.luma_ac_quant_scale = seg.y_dequant[i][1];
    seg_param.chroma_dc_quant_scale = seg.uv_dequant[i][0];
    seg_param.chroma_ac_quant_scale = seg.uv_dequant[i][1];
  }

  VABufferID pic_params_buffer_id;
  status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          VAPictureParameterBufferType, sizeof(pic_param), 1, &pic_param,
                          &pic_params_buffer_id);

  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for pic_param failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  ScopedBufferID picture_params(pic_params_buffer_id);

  VABufferID slice_params_buffer_id;
  status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          VASliceParameterBufferType, sizeof(slice_param), 1, &slice_param,
                          &slice_params_buffer_id);

  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for slice_params failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  ScopedBufferID slice_params(slice_params_buffer_id);

  // Always re-create |encoded_data| because reusing the buffer causes horrific
  // artifacts in decoded buffers. TODO(b/169725321): This seems to be a driver
  // bug, fix it and reuse the buffer.
  VABufferID encoded_data_buffer_id;
  status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          VASliceDataBufferType, static_cast<unsigned int>(frame_hdr->frame_size),
                          1, const_cast<uint8_t*>(frame_hdr->data), &encoded_data_buffer_id);

  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "vaCreateBuffer for encoded_data failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  ScopedBufferID encoded_data(encoded_data_buffer_id);

  auto va_surface_id = static_cast<VaapiVP9Picture*>(pic.get())->GetVASurfaceID();

  status = vaBeginPicture(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          va_surface_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "BeginPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }
  std::vector<VABufferID> buffers{picture_params.id(), slice_params.id(), encoded_data.id()};

  status = vaRenderPicture(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                           buffers.data(), static_cast<int>(buffers.size()));
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "RenderPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  status = vaEndPicture(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id());
  if (status != VA_STATUS_SUCCESS) {
    FX_SLOG(ERROR, "EndPicture failed", KV("error_str", vaErrorStr(status)));
    return Status::kFail;
  }

  return Status::kOk;
}

bool VP9Accelerator::OutputPicture(scoped_refptr<media::VP9Picture> pic) {
  scoped_refptr<VASurface> va_surface = static_cast<VaapiVP9Picture*>(pic.get())->va_surface();
  VASurfaceID va_surface_id = static_cast<VaapiVP9Picture*>(pic.get())->GetVASurfaceID();
  VAStatus status = vaSyncSurface(VADisplayWrapper::GetSingleton()->display(), va_surface_id);

  if (status != VA_STATUS_SUCCESS) {
    // Get more information of the error, if possible. vaQuerySurfaceError can only be called iff
    // vaSyncSurface returns VA_STATUS_ERROR_DECODING_ERROR. If that is the case then we call
    // vaQuerySurfaceError which will return an array of macroblock error structures which tells us
    // what offending macroblocks caused the error and what type of error was encountered.
    bool detailed_query = false;
    if (status == VA_STATUS_ERROR_DECODING_ERROR) {
      VASurfaceDecodeMBErrors* decode_mb_errors;
      VAStatus query_status = vaQuerySurfaceError(VADisplayWrapper::GetSingleton()->display(),
                                                  va_surface_id, VA_STATUS_ERROR_DECODING_ERROR,
                                                  reinterpret_cast<void**>(&decode_mb_errors));

      if (query_status == VA_STATUS_SUCCESS) {
        detailed_query = true;
        FX_SLOG(ERROR, "SyncSurface failed due to the following macroblock errors ...");

        // Limit the amount of errors we can display, just to ensure we don't enter an infinite loop
        // or spam the log with messages
        static constexpr uint32_t kMaxMBErrors = 10u;
        uint32_t mb_error_count = 0u;

        while ((decode_mb_errors != nullptr) && (decode_mb_errors->status != -1) &&
               (mb_error_count < kMaxMBErrors)) {
          FX_SLOG(ERROR, "SyncSurface a macroblock error",
                  KV("decode_error", (decode_mb_errors->decode_error_type == VADecodeSliceMissing)
                                         ? "VADecodeSliceMissing"
                                         : "VADecodeMBError"),
                  KV("start_mb", decode_mb_errors->start_mb),
                  KV("end_mb", decode_mb_errors->end_mb), KV("num_mb", decode_mb_errors->num_mb));
          decode_mb_errors++;
          mb_error_count++;
        }
      }
    }

    // If the error was not VA_STATUS_ERROR_DECODING_ERROR or vaQuerySurfaceError returned an error,
    // just log a generic error message.
    if (!detailed_query) {
      FX_SLOG(ERROR, "SyncSurface failed", KV("error_str", vaErrorStr(status)));
    }

    return false;
  }

  return adapter_->ProcessOutput(va_surface, pic->bitstream_id());
}

bool VP9Accelerator::NeedsCompressedHeaderParsed() const { return false; }

bool VP9Accelerator::GetFrameContext(scoped_refptr<media::VP9Picture> pic,
                                     media::Vp9FrameContext* frame_ctx) {
  return false;
}
