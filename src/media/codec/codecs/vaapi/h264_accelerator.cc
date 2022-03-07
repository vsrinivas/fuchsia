// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "h264_accelerator.h"

#include <va/va_drmcommon.h>

#include "codec_adapter_vaapi_decoder.h"

// from ITU-T REC H.264 spec
// section 8.5.6
// "Inverse scanning process for 4x4 transform coefficients and scaling lists"
static constexpr int kZigzagScan4x4[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};

// section 8.5.7
// "Inverse scanning process for 8x8 transform coefficients and scaling lists"
static constexpr uint8_t kZigzagScan8x8[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

VaapiH264Picture::VaapiH264Picture(scoped_refptr<VASurface> va_surface) : va_surface_(va_surface) {}

VaapiH264Picture::~VaapiH264Picture() = default;

scoped_refptr<media::H264Picture> H264Accelerator::CreateH264Picture(bool is_for_output) {
  auto surface = adapter_->GetVASurface();
  auto surface_ptr = std::make_shared<VaapiH264Picture>(surface);
  return surface_ptr;
}

// Fill |va_pic| with default/neutral values.
static void InitVAPicture(VAPictureH264* va_pic) {
  memset(va_pic, 0, sizeof(*va_pic));
  va_pic->picture_id = VA_INVALID_ID;
  va_pic->flags = VA_PICTURE_H264_INVALID;
}

void FillVAPicture(VAPictureH264* va_pic, scoped_refptr<media::H264Picture> pic) {
  VASurfaceID va_surface_id = VA_INVALID_SURFACE;

  if (!pic->nonexisting)
    va_surface_id = static_cast<VaapiH264Picture*>(pic.get())->GetVASurfaceID();

  va_pic->picture_id = va_surface_id;
  va_pic->frame_idx = pic->frame_num;
  va_pic->flags = 0;

  switch (pic->field) {
    case media::H264Picture::FIELD_NONE:
      break;
    case media::H264Picture::FIELD_TOP:
      va_pic->flags |= VA_PICTURE_H264_TOP_FIELD;
      break;
    case media::H264Picture::FIELD_BOTTOM:
      va_pic->flags |= VA_PICTURE_H264_BOTTOM_FIELD;
      break;
  }

  if (pic->ref) {
    va_pic->flags |=
        pic->long_term ? VA_PICTURE_H264_LONG_TERM_REFERENCE : VA_PICTURE_H264_SHORT_TERM_REFERENCE;
  }

  va_pic->TopFieldOrderCnt = pic->top_field_order_cnt;
  va_pic->BottomFieldOrderCnt = pic->bottom_field_order_cnt;
}

int FillVARefFramesFromDPB(const media::H264DPB& dpb, VAPictureH264* va_pics, int num_pics) {
  media::H264Picture::Vector::const_reverse_iterator rit;
  int i;

  // Return reference frames in reverse order of insertion.
  // Libva does not document this, but other implementations (e.g. mplayer)
  // do it this way as well.
  for (rit = dpb.rbegin(), i = 0; rit != dpb.rend() && i < num_pics; ++rit) {
    if ((*rit)->ref)
      FillVAPicture(&va_pics[i++], *rit);
  }

  return i;
}

H264Accelerator::Status H264Accelerator::SubmitFrameMetadata(
    const media::H264SPS* sps, const media::H264PPS* pps, const media::H264DPB& dpb,
    const media::H264Picture::Vector& ref_pic_listp0,
    const media::H264Picture::Vector& ref_pic_listb0,
    const media::H264Picture::Vector& ref_pic_listb1, scoped_refptr<media::H264Picture> pic) {
  VABufferID pic_param_buf;
  VABufferID iq_matrix_buf_id;
  VAPictureParameterBufferH264 pic_param;
  memset(&pic_param, 0, sizeof(pic_param));

  // Ignore warnings about conversions, since the caller should have validated
  // the actual sizes of the parameters.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#define FROM_SPS_TO_PP(a) pic_param.a = sps->a
#define FROM_SPS_TO_PP2(a, b) pic_param.b = sps->a
  FROM_SPS_TO_PP2(pic_width_in_mbs_minus1, picture_width_in_mbs_minus1);
  // This assumes non-interlaced video
  FROM_SPS_TO_PP2(pic_height_in_map_units_minus1, picture_height_in_mbs_minus1);
  FROM_SPS_TO_PP(bit_depth_luma_minus8);
  FROM_SPS_TO_PP(bit_depth_chroma_minus8);
#undef FROM_SPS_TO_PP
#undef FROM_SPS_TO_PP2

#define FROM_SPS_TO_PP_SF(a) pic_param.seq_fields.bits.a = sps->a
#define FROM_SPS_TO_PP_SF2(a, b) pic_param.seq_fields.bits.b = sps->a
  FROM_SPS_TO_PP_SF(chroma_format_idc);
  FROM_SPS_TO_PP_SF2(separate_colour_plane_flag, residual_colour_transform_flag);
  FROM_SPS_TO_PP_SF(gaps_in_frame_num_value_allowed_flag);
  FROM_SPS_TO_PP_SF(frame_mbs_only_flag);
  FROM_SPS_TO_PP_SF(mb_adaptive_frame_field_flag);
  FROM_SPS_TO_PP_SF(direct_8x8_inference_flag);
  pic_param.seq_fields.bits.MinLumaBiPredSize8x8 = (sps->level_idc >= 31);
  FROM_SPS_TO_PP_SF(log2_max_frame_num_minus4);
  FROM_SPS_TO_PP_SF(pic_order_cnt_type);
  FROM_SPS_TO_PP_SF(log2_max_pic_order_cnt_lsb_minus4);
  FROM_SPS_TO_PP_SF(delta_pic_order_always_zero_flag);
#undef FROM_SPS_TO_PP_SF
#undef FROM_SPS_TO_PP_SF2

#define FROM_PPS_TO_PP(a) pic_param.a = pps->a
  FROM_PPS_TO_PP(pic_init_qp_minus26);
  FROM_PPS_TO_PP(pic_init_qs_minus26);
  FROM_PPS_TO_PP(chroma_qp_index_offset);
  FROM_PPS_TO_PP(second_chroma_qp_index_offset);
#undef FROM_PPS_TO_PP

#define FROM_PPS_TO_PP_PF(a) pic_param.pic_fields.bits.a = pps->a
#define FROM_PPS_TO_PP_PF2(a, b) pic_param.pic_fields.bits.b = pps->a
  FROM_PPS_TO_PP_PF(entropy_coding_mode_flag);
  FROM_PPS_TO_PP_PF(weighted_pred_flag);
  FROM_PPS_TO_PP_PF(weighted_bipred_idc);
  FROM_PPS_TO_PP_PF(transform_8x8_mode_flag);

  pic_param.pic_fields.bits.field_pic_flag = 0;
  FROM_PPS_TO_PP_PF(constrained_intra_pred_flag);
  FROM_PPS_TO_PP_PF2(bottom_field_pic_order_in_frame_present_flag, pic_order_present_flag);
  FROM_PPS_TO_PP_PF(deblocking_filter_control_present_flag);
  FROM_PPS_TO_PP_PF(redundant_pic_cnt_present_flag);
  pic_param.pic_fields.bits.reference_pic_flag = pic->ref;
#undef FROM_PPS_TO_PP_PF
#undef FROM_PPS_TO_PP_PF2

  pic_param.frame_num = pic->frame_num;

#pragma clang diagnostic pop

  InitVAPicture(&pic_param.CurrPic);
  FillVAPicture(&pic_param.CurrPic, std::move(pic));

  // Init reference pictures' array.
  for (int i = 0; i < 16; ++i)
    InitVAPicture(&pic_param.ReferenceFrames[i]);

  // And fill it with picture info from DPB.
  FillVARefFramesFromDPB(dpb, pic_param.ReferenceFrames, base::size(pic_param.ReferenceFrames));

  pic_param.num_ref_frames = static_cast<uint8_t>(sps->max_num_ref_frames);

  VAIQMatrixBufferH264 iq_matrix_buf;
  memset(&iq_matrix_buf, 0, sizeof(iq_matrix_buf));

  if (pps->pic_scaling_matrix_present_flag) {
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 16; ++j)
        iq_matrix_buf.ScalingList4x4[i][kZigzagScan4x4[j]] =
            static_cast<uint8_t>(pps->scaling_list4x4[i][j]);
    }

    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 64; ++j)
        iq_matrix_buf.ScalingList8x8[i][kZigzagScan8x8[j]] =
            static_cast<uint8_t>(pps->scaling_list8x8[i][j]);
    }
  } else {
    for (int i = 0; i < 6; ++i) {
      for (int j = 0; j < 16; ++j)
        iq_matrix_buf.ScalingList4x4[i][kZigzagScan4x4[j]] =
            static_cast<uint8_t>(sps->scaling_list4x4[i][j]);
    }

    for (int i = 0; i < 2; ++i) {
      for (int j = 0; j < 64; ++j)
        iq_matrix_buf.ScalingList8x8[i][kZigzagScan8x8[j]] =
            static_cast<uint8_t>(sps->scaling_list8x8[i][j]);
    }
  }
  VAStatus status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(),
                                   adapter_->context_id(), VAPictureParameterBufferType,
                                   sizeof(pic_param), 1, &pic_param, &pic_param_buf);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "CreateBuffer failed: " << status;
    return Status::kFail;
  }
  status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          VAIQMatrixBufferType, sizeof(iq_matrix_buf), 1, &iq_matrix_buf,
                          &iq_matrix_buf_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "CreateBuffer failed: " << status;
    return Status::kFail;
  }
  slice_buffers_.emplace_back(pic_param_buf);
  slice_buffers_.emplace_back(iq_matrix_buf_id);
  return Status::kOk;
}

H264Accelerator::Status H264Accelerator::SubmitSlice(
    const media::H264PPS* pps, const media::H264SliceHeader* slice_hdr,
    const media::H264Picture::Vector& ref_pic_list0,
    const media::H264Picture::Vector& ref_pic_list1, scoped_refptr<media::H264Picture> pic,
    const uint8_t* data, size_t size, const std::vector<media::SubsampleEntry>& subsamples) {
  VABufferID slice_param_buffer;
  VABufferID slice_data_buffer;

  VASliceParameterBufferH264 slice_param;
  memset(&slice_param, 0, sizeof(slice_param));

  slice_param.slice_data_size = static_cast<uint32_t>(slice_hdr->nalu_size);
  slice_param.slice_data_offset = 0;
  slice_param.slice_data_flag = VA_SLICE_DATA_FLAG_ALL;
  slice_param.slice_data_bit_offset = static_cast<uint16_t>(slice_hdr->header_bit_size);

  // Ignore warnings about conversions, since the caller should have validated
  // the actual sizes of the parameters.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#define SHDRToSP(a) slice_param.a = slice_hdr->a
  SHDRToSP(first_mb_in_slice);
  slice_param.slice_type = slice_hdr->slice_type % 5;
  SHDRToSP(direct_spatial_mv_pred_flag);

  SHDRToSP(num_ref_idx_l0_active_minus1);
  SHDRToSP(num_ref_idx_l1_active_minus1);
  SHDRToSP(cabac_init_idc);
  SHDRToSP(slice_qp_delta);
  SHDRToSP(disable_deblocking_filter_idc);
  SHDRToSP(slice_alpha_c0_offset_div2);
  SHDRToSP(slice_beta_offset_div2);

  if (((slice_hdr->IsPSlice() || slice_hdr->IsSPSlice()) && pps->weighted_pred_flag) ||
      (slice_hdr->IsBSlice() && pps->weighted_bipred_idc == 1)) {
    SHDRToSP(luma_log2_weight_denom);
    SHDRToSP(chroma_log2_weight_denom);

    SHDRToSP(luma_weight_l0_flag);
    SHDRToSP(luma_weight_l1_flag);

    SHDRToSP(chroma_weight_l0_flag);
    SHDRToSP(chroma_weight_l1_flag);

    for (int i = 0; i <= slice_param.num_ref_idx_l0_active_minus1; ++i) {
      slice_param.luma_weight_l0[i] = slice_hdr->pred_weight_table_l0.luma_weight[i];
      slice_param.luma_offset_l0[i] = slice_hdr->pred_weight_table_l0.luma_offset[i];

      for (int j = 0; j < 2; ++j) {
        slice_param.chroma_weight_l0[i][j] = slice_hdr->pred_weight_table_l0.chroma_weight[i][j];
        slice_param.chroma_offset_l0[i][j] = slice_hdr->pred_weight_table_l0.chroma_offset[i][j];
      }
    }

    if (slice_hdr->IsBSlice()) {
      for (int i = 0; i <= slice_param.num_ref_idx_l1_active_minus1; ++i) {
        slice_param.luma_weight_l1[i] = slice_hdr->pred_weight_table_l1.luma_weight[i];
        slice_param.luma_offset_l1[i] = slice_hdr->pred_weight_table_l1.luma_offset[i];

        for (int j = 0; j < 2; ++j) {
          slice_param.chroma_weight_l1[i][j] = slice_hdr->pred_weight_table_l1.chroma_weight[i][j];
          slice_param.chroma_offset_l1[i][j] = slice_hdr->pred_weight_table_l1.chroma_offset[i][j];
        }
      }
    }
  }

#pragma clang diagnostic pop
  static_assert(base::size(slice_param.RefPicList0) == base::size(slice_param.RefPicList1),
                "Invalid RefPicList sizes");

  for (size_t i = 0; i < base::size(slice_param.RefPicList0); ++i) {
    InitVAPicture(&slice_param.RefPicList0[i]);
    InitVAPicture(&slice_param.RefPicList1[i]);
  }

  for (size_t i = 0; i < ref_pic_list0.size() && i < base::size(slice_param.RefPicList0); ++i) {
    if (ref_pic_list0[i])
      FillVAPicture(&slice_param.RefPicList0[i], ref_pic_list0[i]);
  }
  for (size_t i = 0; i < ref_pic_list1.size() && i < base::size(slice_param.RefPicList1); ++i) {
    if (ref_pic_list1[i])
      FillVAPicture(&slice_param.RefPicList1[i], ref_pic_list1[i]);
  }
  VAStatus status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(),
                                   adapter_->context_id(), VASliceParameterBufferType,
                                   sizeof(slice_param), 1, &slice_param, &slice_param_buffer);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "CreateBuffer failed: " << status;
    return Status::kFail;
  }
  slice_buffers_.emplace_back(slice_param_buffer);
  status = vaCreateBuffer(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                          VASliceDataBufferType, static_cast<uint32_t>(size), 1,
                          const_cast<uint8_t*>(data), &slice_data_buffer);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "CreateBuffer failed: " << status;
    return Status::kFail;
  }
  slice_buffers_.emplace_back(slice_data_buffer);

  return Status::kOk;
}

H264Accelerator::Status H264Accelerator::SubmitDecode(scoped_refptr<media::H264Picture> pic) {
  VASurfaceID va_surface_id = static_cast<VaapiH264Picture*>(pic.get())->GetVASurfaceID();
  VAStatus status = vaBeginPicture(VADisplayWrapper::GetSingleton()->display(),
                                   adapter_->context_id(), va_surface_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "BeginPicture failed: " << status;
    return Status::kFail;
  }
  std::vector<VABufferID> buffers;
  for (auto& buffer : slice_buffers_) {
    buffers.push_back(buffer.id());
  }
  status = vaRenderPicture(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id(),
                           buffers.data(), static_cast<int>(buffers.size()));
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "RenderPicture failed: " << status;
    return Status::kFail;
  }
  status = vaEndPicture(VADisplayWrapper::GetSingleton()->display(), adapter_->context_id());
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "EndPicture failed: " << status;
    return Status::kFail;
  }
  slice_buffers_.clear();
  return Status::kOk;
}

bool H264Accelerator::OutputPicture(scoped_refptr<media::H264Picture> pic) {
  scoped_refptr<VASurface> va_surface = static_cast<VaapiH264Picture*>(pic.get())->va_surface();
  VASurfaceID va_surface_id = static_cast<VaapiH264Picture*>(pic.get())->GetVASurfaceID();
  VAStatus status = vaSyncSurface(VADisplayWrapper::GetSingleton()->display(), va_surface_id);
  if (status != VA_STATUS_SUCCESS) {
    FX_LOGS(WARNING) << "SyncSurface failed: " << status;
    return false;
  }

  return adapter_->ProcessOutput(va_surface, pic->bitstream_id());
}
