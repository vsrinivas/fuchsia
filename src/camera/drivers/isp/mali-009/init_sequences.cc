// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/types.h>

#include <hwreg/bitfields.h>

#include "arm-isp.h"
#include "global_regs.h"
#include "pingpong_regs.h"

namespace camera {

void ArmIspDevice::IspLoadSeq_linear() {
  ping::InputFormatter_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0)
      .set_input_bitwidth_select(0x2)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_gain_wdr(0x1)
      .set_bypass_frame_stitch(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_NoiseLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_noise_level_0(0)
      .set_noise_level_1(0)
      .set_noise_level_2(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_linear_data_src(0)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_NoiseLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_noise_level_0(0x40)
      .set_noise_level_1(0)
      .set_noise_level_2(0)
      .WriteTo(&isp_mmio_local_);
}

void ArmIspDevice::IspLoadSeq_settings() {
  InputPort_Config0::Get()
      .ReadFrom(&isp_mmio_)
      .set_preset(0x2)
      .set_vs_use_field(0)
      .set_vs_toggle(0)
      .set_vs_polarity(0)
      .set_vs_polarity_acl(0)
      .set_hs_use_acl(0)
      .set_vc_c_select(0)
      .set_vc_r_select(0)
      .set_hs_xor_vs(0)
      .set_hs_polarity(0)
      .set_hs_polarity_acl(0)
      .set_hs_polarity_hs(0)
      .set_hs_polarity_vc(0)
      .set_hc_r_select(0)
      .set_acl_polarity(0)
      .WriteTo(&isp_mmio_);

  InputPort_Config1::Get()
      .ReadFrom(&isp_mmio_)
      .set_field_polarity(0)
      .set_field_toggle(0)
      .set_aclg_window0(0x1)
      .set_aclg_hsync(0x1)
      .set_aclg_window2(0x1)
      .set_aclg_acl(0)
      .set_aclg_vsync(0)
      .set_hsg_window1(0)
      .set_hsg_hsync(0)
      .set_hsg_vsync(0)
      .set_hsg_window2(0)
      .set_fieldg_vsync(0x1)
      .set_fieldg_window2(0)
      .set_fieldg_field(0)
      .set_field_mode(0)
      .WriteTo(&isp_mmio_);

  InputPort_HorizontalCrop0::Get()
      .ReadFrom(&isp_mmio_)
      .set_hc_limit(0xffff)
      .set_hc_start0(0)
      .WriteTo(&isp_mmio_);

  InputPort_HorizontalCrop1::Get()
      .ReadFrom(&isp_mmio_)
      .set_hc_size0(0)
      .set_hc_start1(0)
      .WriteTo(&isp_mmio_);

  IspGlobal_Config0::Get().ReadFrom(&isp_mmio_).set_global_fsm_reset(0).WriteTo(&isp_mmio_);

  InputPort_VerticalCrop1::Get()
      .ReadFrom(&isp_mmio_)
      .set_vc_start(0)
      .set_vc_size(0)
      .WriteTo(&isp_mmio_);

  IspGlobal_Config2::Get()
      .ReadFrom(&isp_mmio_)
      .set_interline_blanks_min(0x20)
      .set_interframe_blanks_min(0x20)
      .WriteTo(&isp_mmio_);

  InputPort_Config3::Get()
      .ReadFrom(&isp_mmio_)
      .set_mode_request(0x1)
      .set_freeze_config(0)
      .WriteTo(&isp_mmio_);

  IspGlobal_Config3::Get()
      .ReadFrom(&isp_mmio_)
      .set_mcu_override_config_select(0)
      .set_mcu_ping_pong_config_select(0)
      .set_multi_context_mode(0)
      .WriteTo(&isp_mmio_);

  IspGlobal_Config4::Get()
      .ReadFrom(&isp_mmio_)
      .set_ping_locked(0)
      .set_pong_locked(0)
      .set_ping_pong_config_select(0x1)
      .WriteTo(&isp_mmio_);

  IspGlobalInterrupt_MaskVector::Get()
      .ReadFrom(&isp_mmio_)
      .set_isp_start(0x1)
      .set_isp_done(0x1)
      .set_ctx_management_error(0x1)
      .set_broken_frame_error(0x1)
      .set_metering_af_done(0x1)
      .set_metering_aexp_done(0x1)
      .set_metering_awb_done(0x1)
      .set_metering_aexp_1024_bin_hist_done(0x1)
      .set_metering_antifog_hist_done(0x1)
      .set_lut_init_done(0x1)
      .set_fr_y_dma_write_done(0x1)
      .set_fr_uv_dma_write_done(0x1)
      .set_ds_y_dma_write_done(0x1)
      .set_linearization_done(0x1)
      .set_static_dpc_done(0x1)
      .set_ca_correction_done(0x1)
      .set_iridix_done(0x1)
      .set_three_d_liut_done(0x1)
      .set_wdg_timer_timed_out(0x1)
      .set_frame_collision_error(0x1)
      .set_luma_variance_done(0x1)
      .set_dma_error_interrupt(0x1)
      .set_input_port_safely_stopped(0x1)
      .WriteTo(&isp_mmio_);

  IspGlobalInterrupt_ClearVector::Get()
      .ReadFrom(&isp_mmio_)
      .set_isp_start(0x1)
      .set_isp_done(0x1)
      .set_ctx_management_error(0x1)
      .set_broken_frame_error(0x1)
      .set_metering_af_done(0x1)
      .set_metering_aexp_done(0x1)
      .set_metering_awb_done(0x1)
      .set_metering_aexp_1024_bin_hist_done(0x1)
      .set_metering_antifog_hist_done(0x1)
      .set_lut_init_done(0x1)
      .set_fr_y_dma_write_done(0x1)
      .set_fr_uv_dma_write_done(0x1)
      .set_ds_y_dma_write_done(0x1)
      .set_linearization_done(0x1)
      .set_static_dpc_done(0x1)
      .set_ca_correction_done(0x1)
      .set_iridix_done(0x1)
      .set_three_d_liut_done(0x1)
      .set_wdg_timer_timed_out(0x1)
      .set_frame_collision_error(0x1)
      .set_luma_variance_done(0x1)
      .set_dma_error_interrupt(0x1)
      .set_input_port_safely_stopped(0x1)
      .WriteTo(&isp_mmio_);

  IspGlobalInterrupt_PulseMode::Get().ReadFrom(&isp_mmio_).set_value(0x0).WriteTo(&isp_mmio_);

  IspGlobalInterrupt_Clear::Get().ReadFrom(&isp_mmio_).set_value(0).WriteTo(&isp_mmio_);

  IspGlobalInterrupt_StatusVector::Get()
      .ReadFrom(&isp_mmio_)
      .set_isp_start(0)
      .set_isp_done(0)
      .set_ctx_management_error(0)
      .set_broken_frame_error(0)
      .set_metering_af_done(0)
      .set_metering_aexp_done(0)
      .set_metering_awb_done(0)
      .set_metering_aexp_1024_bin_hist_done(0)
      .set_metering_antifog_hist_done(0)
      .set_lut_init_done(0)
      .set_fr_y_dma_write_done(0)
      .set_fr_uv_dma_write_done(0)
      .set_ds_y_dma_write_done(0)
      .set_linearization_done(0)
      .set_static_dpc_done(0)
      .set_ca_correction_done(0)
      .set_iridix_done(0)
      .set_three_d_liut_done(0)
      .set_wdg_timer_timed_out(0)
      .set_frame_collision_error(0)
      .set_luma_variance_done(0)
      .set_dma_error_interrupt(0)
      .set_input_port_safely_stopped(0)
      .WriteTo(&isp_mmio_);

  IspGlobalMonitor_Status::Get()
      .ReadFrom(&isp_mmio_)
      .set_broken_frame_status(0x3)
      .WriteTo(&isp_mmio_);

  IspGlobalMonitor_Failures::Get()
      .ReadFrom(&isp_mmio_)
      .set_fr_y_dma_wfifo_fail_full(0x1)
      .set_fr_y_dma_wfifo_fail_empty(0)
      .set_fr_uv_dma_wfifo_fail_full(0)
      .set_fr_uv_dma_wfifo_fail_empty(0)
      .set_ds_y_dma_wfifo_fail_full(0)
      .set_ds_y_dma_wfifo_fail_empty(0)
      .set_ds_uv_dma_wfifo_fail_full(0)
      .set_ds_uv_dma_wfifo_fail_empty(0)
      .set_temper_dma_wfifo_fail_empty(0)
      .set_temper_dma_wfifo_fail_full(0)
      .set_temper_dma_rfifo_fail_empty(0)
      .set_temper_dma_rfifo_fail_full(0)
      .WriteTo(&isp_mmio_);

  IspGlobalMonitor_ClearError::Get()
      .ReadFrom(&isp_mmio_)
      .set_broken_frame_error_clear(0)
      .set_context_error_clr(0)
      .WriteTo(&isp_mmio_);

  InputPort_VerticalCrop0::Get()
      .ReadFrom(&isp_mmio_)
      .set_hc_size1(0)
      .set_vc_limit(0xffff)
      .WriteTo(&isp_mmio_);

  IspGlobalChickenBit::Get()
      .ReadFrom(&isp_mmio_)
      .set_dma_writer_timeout_disable(0)
      .WriteTo(&isp_mmio_);

  IspGlobalDbg::Get()
      .ReadFrom(&isp_mmio_)
      .set_mode_en(0x1)
      .set_clear_frame_cnt(0)
      .WriteTo(&isp_mmio_);

  IspGlobal_Config1::Get()
      .ReadFrom(&isp_mmio_)
      .set_flush_hblank(0x20)
      .set_isp_monitor_select(0)
      .WriteTo(&isp_mmio_);
}

void ArmIspDevice::IspLoadSeq_fs_lin_2exp() {
  ping::Crossbar_Channel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_channel1_select(0)
      .set_channel2_select(0x2)
      .set_channel3_select(0x1)
      .set_channel4_select(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_gain_wdr(0)
      .set_bypass_frame_stitch(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x7)
      .set_input_bitwidth_select(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_linear_data_src(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x1)
      .WriteTo(&isp_mmio_local_);
}

void ArmIspDevice::IspLoadSeq_fs_lin_3exp() {
  ping::Crossbar_Channel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_channel1_select(0)
      .set_channel2_select(0x2)
      .set_channel3_select(0x1)
      .set_channel4_select(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_gain_wdr(0)
      .set_bypass_frame_stitch(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x7)
      .set_input_bitwidth_select(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_linear_data_src(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x2)
      .WriteTo(&isp_mmio_local_);
}

void ArmIspDevice::IspLoadSeq_fs_lin_4exp() {
  ping::Crossbar_Channel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_channel1_select(0)
      .set_channel2_select(0x2)
      .set_channel3_select(0x2)
      .set_channel4_select(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_gain_wdr(0)
      .set_bypass_frame_stitch(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x7)
      .set_input_bitwidth_select(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_linear_data_src(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x3)
      .WriteTo(&isp_mmio_local_);
}

void ArmIspDevice::IspLoadSeq_settings_context() {
  ping::FrameStitch_BlackLevel1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_black_level_short(0xf0)
      .set_black_level_very_short(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_vh_slope(0xc8)
      .set_aa_slope(0xb4)
      .set_va_slope(0xb4)
      .set_uu_slope(0xb2)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_Hist1::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::ChromaticAberrationCorrection_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mesh_scale(0)
      .set_enable(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_BlackLevelOut::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean1Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Config0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_np_mult(0x180)
      .set_ms_np_mult(0x600)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontendNp_ExpThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xffff)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringIhist_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_skip_x(0x1)
      .set_skip_y(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Config1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_svs_np_mult(0x600)
      .set_lm_alpha_mov_slope(0xc00)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Config2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ms_alpha_mov_slope(0x180)
      .set_svs_alpha_mov_slope(0x180)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_RmOffCenterMult::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x6ea)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_GainRB::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_gain_r(0x100)
      .set_gain_b(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma1HighThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x5dc)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_ConsistencyThreshMov::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Sad::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sad_slope(0x40)
      .set_sad_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Config11::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sharp_alt_ld(0x1c)
      .set_sharp_alt_ldu(0x46)
      .set_sharp_alt_lu(0x1d)
      .set_sad_amp(0x8)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_ConsistencyThreshLvl::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x80000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UCenter::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x800)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Lm::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_noise_thresh(0)
      .set_lm_pos_weight(0x14)
      .set_lm_neg_weight(0x2)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontendNp_Ratio::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_short_ratio(0)
      .set_long_ratio(0x4)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMedNoise::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_med_noise_alpha_thresh(0x40)
      .set_lm_med_noise_intensity_thresh(0x200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_GlobalSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x333)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_DebugReg::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcBlendSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_HorizontalThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_thresh_0h(0)
      .set_thresh_1h(0)
      .set_thresh_2h(0)
      .set_thresh_4h(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcBlend::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_mc_blend_thresh(0)
      .set_lm_mc_blend_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_ActiveDim::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_active_width(0x780)
      .set_active_height(0x438)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CrRefLowAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::IridixGain_Gain::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcThreshSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_SadThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvSeg1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfbd8)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcThreshThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_SatHighThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xc83)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcThreshOffset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ChromaticAberrationCorrection_Mesh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mesh_width(0x40)
      .set_mesh_height(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CbRefMaxAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1ff)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcMagThreshSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffRg::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x10a5)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean2Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcMagThreshThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_WbOffset::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LmMcMag::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_mc_mag_thresh_offset(0)
      .set_lm_mc_mag_lblend_thresh(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma2HighThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Failures::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_axi_fail_bresp(0)
      .set_axi_fail_awmaxwait(0)
      .set_axi_fail_wmaxwait(0)
      .set_axi_fail_wxact_ostand(0)
      .set_vi_fail_active_width(0)
      .set_vi_fail_active_height(0)
      .set_vi_fail_interline_blanks(0)
      .set_vi_fail_interframe_blanks(0)
      .set_video_alarm(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Config3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_wb_offset(0x100)
      .set_exposure_mask_thresh(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HueLow::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_hue_low_slope(0xf8)
      .set_hue_low_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Config4::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bwb_select(0x1)
      .set_use_3x3_max(0x1)
      .set_mcoff_mode_enable(0)
      .set_lm_alg_select(0)
      .set_mcoff_nc_enable(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_McoffMax0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_l_max(0xdac)
      .set_mcoff_m_max(0xdac)
      .WriteTo(&isp_mmio_local_);

  ping::ChromaticAberrationCorrection_Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_line_offset(0x40)
      .set_plane_offset(0x1000)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_McoffMax1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_s_max(0xdac)
      .set_mcoff_vs_max(0xdac)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_ContextNo::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::FrameStitch_McoffScaler0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_l_scaler(0x800)
      .set_mcoff_lm_scaler(0x800)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CbRefHighAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_WhiteLevelAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3ff)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_McoffScaler1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_lms_scaler(0x800)
      .set_mcoff_nc_thresh_low(0x20)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HueStrength::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x300)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe7b4)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_McoffNc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mcoff_nc_thresh_high(0x40)
      .set_mcoff_nc_scale(0x80)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HueLowThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x7ad)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_Gain1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_gain_r(0x100)
      .set_gain_gr(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_FwdAlpha::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_Gain2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_gain_gb(0x100)
      .set_gain_b(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::ChromaticAberrationCorrection_MeshReload::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DigitalGain_Gain::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xffff)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_PerceptControl::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_fwd_percept_control(0x2)
      .set_rev_percept_control(0x2)
      .set_strength_inroi(0x70)
      .WriteTo(&isp_mmio_local_);

  ping::DigitalGain_Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_global_offset(0x1e)
      .set_use_lut(0)
      .set_use_exp_mask(0x1)
      .set_black_reflect(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Luma3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_offset_high(0)
      .set_luma_slope_high(0x6a4)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetFe_Offset00::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_MaxD::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_max_d_strength(0x333)
      .set_max_ud_strength(0x333)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_Config1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_svariance(0xa)
      .set_bright_pr(0xdc)
      .set_contrast(0xb4)
      .set_filter_mux(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetFe_Offset01::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HueHigh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_hue_high_slope(0x320)
      .set_hue_high_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_Hist3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x5)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetFe_Offset10::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetFe_Offset11::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::SquareBe_BlackLevelIn::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x7d0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::Sqrt_BlackLevelIn::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_HorizontalRoi::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_roi_hor_start(0)
      .set_roi_hor_end(0xffff)
      .WriteTo(&isp_mmio_local_);

  ping::IridixGain_Offset::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::Top_ActiveDim::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_active_width(0x780)
      .set_active_height(0x438)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_BlackLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_rggb_start_pre_mirror(0)
      .set_rggb_start_post_mirror(0)
      .set_cfa_pattern(0)
      .set_linear_data_src(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma2Low::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma2_low_slope(0x190)
      .set_luma2_low_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_DebugSel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HueHighThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x960)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_MinDStrength::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1f8b)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_DynamicDefectPixel0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_dp_slope(0xaa)
      .set_dp_threshold(0xfc3)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_Bg::Get().ReadFrom(&isp_mmio_local_).set_value(0xc4).WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xffff)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_DynamicDefectPixel1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_dpdev_threshold(0x8000)
      .set_dp_blend(0)
      .WriteTo(&isp_mmio_local_);

  ping::SquareBe_BlackLevelOut::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_HistThresh34::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xd0)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_GreenEqualization0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ge_strength(0x40)
      .set_ge_threshold(0xaa)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Hsl::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_hsl_slope(0x20)
      .set_hsl_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_GreenEqualization1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ge_slope(0x11d)
      .set_ge_sens(0x80)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_Thresh1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x4000)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_line_thresh(0)
      .set_sigma_in(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank0Base::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x5000000)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_Thresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_thresh_short(0)
      .set_thresh_long(0)
      .WriteTo(&isp_mmio_local_);

  ping::Temper_Config0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_enable(0x1)
      .set_temper2_mode(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_video_test_gen(0)
      .set_bypass_input_formatter(0)
      .set_bypass_decompander(0)
      .set_bypass_sensor_offset_wdr(0)
      .set_bypass_gain_wdr(0x1)
      .set_bypass_frame_stitch(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Config12::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_uu_sh_slope(0xb2)
      .set_lg_det_thresh(0x8)
      .set_grey_det_thresh(0x8)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma1Low::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma1_low_slope(0x6e)
      .set_luma1_low_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_digital_gain(1)
      .set_bypass_frontend_sensor_offset(0x1)
      .set_bypass_fe_sqrt(1)
      .set_bypass_raw_frontend(1)
      .set_bypass_defect_pixel(1)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetPreShading_Offset00::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CbRefMinAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontendNp_NpOff::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_np_off(0)
      .set_np_off_reflect(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffRb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1016)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_sinter(1)
      .set_bypass_temper(1)
      .set_bypass_ca_correction(1)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_Thresh2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_HistThresh01::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x10)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_square_be(1)
      .set_bypass_sensor_offset_pre_shading(1)
      .set_bypass_radial_shading(1)
      .set_bypass_mesh_shading(1)
      .set_bypass_white_balance(1)
      .set_bypass_iridix_gain(1)
      .set_bypass_iridix(1)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Center::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_center_x(0x3c0)
      .set_center_y(0x21c)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_BlkStatus::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass4::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_mirror(0x1)
      .set_bypass_demosaic_rgb(1)
      .set_bypass_demosaic_rgbir(0x1)
      .set_bypass_pf_correction(1)
      .set_bypass_ccm(1)
      .set_bypass_cnr(1)
      .set_bypass_3d_lut(0x1)
      .set_bypass_nonequ_gamma(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Temper_Config1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_frame_delay(0)
      .set_log_enable(0x1)
      .set_show_alpha(0)
      .set_show_alphaab(0)
      .set_mixer_select(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_BypassFr::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_fr_crop(0)
      .set_bypass_fr_gamma_rgb(1)
      .set_bypass_fr_sharpen(1)
      .set_bypass_fr_cs_conv(1)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Scale::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_uv_var1_scale(0x10)
      .set_uv_var2_scale(0x10)
      .WriteTo(&isp_mmio_local_);

  ping::Top_BypassDs::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_ds_crop(0)
      .set_bypass_ds_scaler(1)
      .set_bypass_ds_gamma_rgb(1)
      .set_bypass_ds_sharpen(1)
      .set_bypass_ds_cs_conv(1)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetPreShading_Offset01::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Isp::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_isp_processing_fr_bypass_mode(0)
      .set_isp_raw_bypass(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_SatLow::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sat_low_slope(0x100)
      .set_sat_low_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Disable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ae_switch(0)
      .set_af_switch(0)
      .set_awb_switch(0)
      .set_ihist_disable(0)
      .set_aexp_src(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_Thresh3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xc000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_BlackLevelAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Crossbar_Channel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_channel1_select(0)
      .set_channel2_select(0x2)
      .set_channel3_select(0x1)
      .set_channel4_select(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean1Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_Select::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_test_pattern_off_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::Temper_Config2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_recursion_limit(0x2)
      .set_delta(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xed8)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_PatternType::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_iridix_on(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_RBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetPreShading_Offset10::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_GBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe7b4)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_BBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::SinterNoiseProfile_NoiseLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_noise_level_0(0)
      .set_noise_level_1(0)
      .set_noise_level_2(0)
      .set_noise_level_3(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Clip::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_clip_str_max(0x118)
      .set_clip_str_min(0x118)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_RForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_GtmSelect::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_GForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_global_offset(0x78)
      .set_use_lut(0)
      .set_use_exp_mask(0)
      .set_black_reflect(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_Hist4::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffa)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_BForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_HslThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_SquareRootEnable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_RgbGradient::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaSlopeLowD::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x4000)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh0_RgbGradientStart::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_NodesUsed::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_nodes_used_horiz(0x21)
      .set_nodes_used_vert(0x21)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank3Base::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_SatSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x5d)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_RevAlpha::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xed8)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Uv::DmaWriter_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_base_mode(0)
      .set_frame_write_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_BlackLevel::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_Select::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_test_pattern_off_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_BlackLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_control_r(0x4c)
      .set_alpha_undershoot(0xa)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_MinUdStrength::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1fa4)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_PatternType::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank4Base::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_RBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaThreshHighD::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfa0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_HistThresh12::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x20)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_GBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvSeg1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x258)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_BBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Threshold0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_vh_thresh(0x64)
      .set_aa_thresh(0x14)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_RForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank1Base::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_GForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_Thresh1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Sharpen_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_BForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma1LowThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xcd)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_RgbGradient::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaSlopeHighD::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x4000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_Rg::Get().ReadFrom(&isp_mmio_local_).set_value(0xc7).WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh1_RgbGradientStart::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffGr::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x101f)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Primary::DmaWriter_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_base_mode(0)
      .set_frame_write_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe7b4)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_UseColorCorrectedRgb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::Sqrt_BlackLevelOut::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x7d0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar1Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_VCenter::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x800)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_Config0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_variance_space(0xf)
      .set_variance_intensity(0x7)
      .set_slope_max(0x80)
      .set_slope_min(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_Select::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_test_pattern_off_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_Thresh2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe7b4)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_PatternType::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_RBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaLowUd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_thresh_low_ud(0x4e)
      .set_luma_offset_low_ud(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_GBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_UuSh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_uu_sh_thresh(0xf0)
      .set_uu_sh_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_SatLowThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x2d0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_BBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::RawFrontend_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ge_enable(0x1)
      .set_dp_enable(0x1)
      .set_show_dynamic_defect_pixel(0)
      .set_dark_disable(0)
      .set_bright_disable(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CrRefMaxAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1ff)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_RForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_DebugSel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_GForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_Thresh3::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_BForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_FrameCount::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_frame_icount(0xadf4)
      .set_frame_wcount(0xde78)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_RgbGradient::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaSlopeLowUd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x4000)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh2_RgbGradientStart::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CbRefLowAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringHistAexp_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_skip_x(0x1)
      .set_skip_y(0)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_VerticalThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_thresh_0v(0)
      .set_thresh_1v(0)
      .set_thresh_2v(0)
      .set_thresh_4v(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Offset0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_vh_offset(0x800)
      .set_aa_offset(0x800)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Debug::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_Strength::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_strength_0(0xff)
      .set_strength_1(0xff)
      .set_strength_2(0xff)
      .set_strength_4(0xff)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_max_bank(0)
      .set_bank0_restart(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_Select::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_test_pattern_off_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::TemperNoiseProfile_NoiseLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_noise_level_0(0x40)
      .set_noise_level_1(0)
      .set_noise_level_2(0)
      .set_noise_level_3(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Threshold1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_va_thresh(0x64)
      .set_uu_thresh(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_PatternType::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_NodesUsed::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_nodes_used_horiz(0x21)
      .set_nodes_used_vert(0x21)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_DarkEnh::Get().ReadFrom(&isp_mmio_local_).set_value(0x400).WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_RBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaThreshHighUd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfa0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_GBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_LineOffset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1e00)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_BBackgnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Offset1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_va_offset(0x800)
      .set_uu_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvSeg1Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_RForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_VerticalRoi::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_roi_ver_start(0)
      .set_roi_ver_end(0xffff)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_WhiteLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xd9999)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_GForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma2LowThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xd48)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Luma1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_thresh_low(0x12c)
      .set_luma_offset_low(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_SharpenAlgSelect::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_BForegnd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8ffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffBb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x171)
      .WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_RgbGradient::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaSlopeHighUd::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x4000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_Hist0::Get().ReadFrom(&isp_mmio_local_).set_value(0).WriteTo(&isp_mmio_local_);

  ping::VideoTestGenCh3_RgbGradientStart::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfffff)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Strength2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_purple_strength(0xfff)
      .set_saturation_strength(0x20)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_StrengthOutroi::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x200)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Offset2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sat_offset(0)
      .set_ac_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean2Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Bank2Base::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CrRefHighAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0)
      .set_input_bitwidth_select(0x2)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma2High::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma2_high_slope(0x5dc)
      .set_luma2_high_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_FactorMl::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Luma1High::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma1_high_slope(0x46)
      .set_luma1_high_offset(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_FactorMs::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffGg::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x159)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_NpOff::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_np_off(0)
      .set_np_off_reflect(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_BlackLevel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_KneePoint::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_knee_point0(0)
      .set_knee_point1(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_SharpenAlternate::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sharp_alt_d(0x1c)
      .set_sharp_alt_ud(0x1d)
      .set_np_offset(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Uv::DmaWriter_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_base_mode(0)
      .set_frame_write_on(0)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_KneePoint2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_GlobalOffset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xccc)
      .WriteTo(&isp_mmio_local_);

  ping::InputFormatter_Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_slope0_select(0)
      .set_slope1_select(0)
      .set_slope2_select(0)
      .set_slope3_select(0)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_view_filter(0)
      .set_scale_mode(0x3)
      .set_enable(0x1)
      .set_filter_select(0)
      .set_int_select(0x1)
      .set_rm_enable(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta2Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar2Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrL_Offset0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_00(0)
      .set_offset_01(0)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_base_mode(0x2)
      .set_plane_select(0)
      .set_single_frame(0)
      .set_frame_write_on(0x1)
      .set_axi_xact_comp(0)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrL_Offset1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_10(0)
      .set_offset_11(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffGb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x103a)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrM_Offset0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_00(0)
      .set_offset_01(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_SatHigh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sat_high_slope(0x1f4)
      .set_sat_high_offset(0xfff)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrM_Offset1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_10(0)
      .set_offset_11(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_DmscConfig::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_CrRefMinAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x40)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrS_Offset0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_00(0)
      .set_offset_01(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffRr::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1bb)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_EffectiveKernel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x3f)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAexp_HistThresh45::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe0)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrS_Offset1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_10(0)
      .set_offset_11(0)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_int_config(0xa)
      .set_nlm_en(0x1)
      .set_nonlinear_wkgen(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringHistAexp_NodesUsed::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_nodes_used_horiz(0x21)
      .set_nodes_used_vert(0x21)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrVs_Offset0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_00(0)
      .set_offset_01(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_DeltaFactor::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x96)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetWdrVs_Offset1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_offset_10(0)
      .set_offset_11(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffBr::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x15)
      .WriteTo(&isp_mmio_local_);

  ping::SensorOffsetPreShading_Offset11::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf000)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_StatsMode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_OffCenterMult::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xe95)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_AlphaChannel::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ac_thresh(0x1b3)
      .set_ac_slope(0xcf)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_LumaLowD::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_thresh_low_d(0x8)
      .set_luma_offset_low_d(0)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_DetSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lg_det_slope(0x8000)
      .set_grey_det_slope(0x157c)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Strength1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sat_strength(0xc8)
      .set_luma_strength(0x400)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_SadFiltThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x8)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Threshold2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_sat_thresh(0x171)
      .set_lum_thresh(0x96)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_Mode::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mode_in(0x2)
      .set_output_select(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_ExposureRatio::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_exposure_ratio(0x100)
      .set_ms_exposure_ratio(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_ColorConversionMatrixCoeffBg::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1086)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta1Threshold::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_SvsExposureRatio::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Primary::DmaWriter_WBank::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_wbank_curr(0)
      .set_wbank_active(0x1)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_LongMediumThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_lm_thresh_high(0xf00)
      .set_lm_thresh_low(0xc00)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_FalseColor::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_fc_slope(0x96)
      .set_fc_alias_slope(0x55)
      .set_fc_alias_thresh(0)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_MediumShortThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_ms_thresh_high(0xf00)
      .set_ms_thresh_low(0xc00)
      .WriteTo(&isp_mmio_local_);

  ping::Iridix_CollectionCorrection::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x100)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_ShortVeryShortThresh::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_svs_thresh_high(0xf00)
      .set_svs_thresh_low(0xc00)
      .WriteTo(&isp_mmio_local_);

  ping::Sinter_RmCenter::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_rm_center_x(0x3c0)
      .set_rm_center_y(0x21c)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Luma2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_slope_low(0x3e8)
      .set_luma_thresh_high(0x3e8)
      .WriteTo(&isp_mmio_local_);

  ping::FrameStitch_BlackLevel0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_black_level_long(0xf0)
      .set_black_level_medium(0xf0)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_Sum::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(0x1b29ac)
      .WriteTo(&isp_mmio_local_);
}

void ArmIspDevice::IspLoadCustomSequence() {
  ping::DemosaicRgb_Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_vh_slope(175)
      .set_aa_slope(190)
      .set_va_slope(170)
      .set_uu_slope(157)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_Threshold0::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_vh_thresh(90)
      .set_vh_thresh(50)
      .set_vh_thresh(90)
      .set_vh_thresh(90)
      .WriteTo(&isp_mmio_local_);

  ping::DemosaicRgb_FalseColor::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_fc_slope(150)
      .set_fc_alias_slope(95)
      .WriteTo(&isp_mmio_local_);

  ping::MeshShading_Config::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_mesh_scale(2)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_temper(1)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Enable::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_DeltaFactor::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(150)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UCenter::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(2048)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_VCenter::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(2048)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_GlobalOffset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(3276)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_GlobalSlope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(819)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvSeg1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvSeg1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(64535)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(61000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Umean2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(61000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(57000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(200)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_Vmean2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(57000)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(4095)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar1Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(65280)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(4095)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvVar2Slope::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(65280)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta1Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(500)
      .WriteTo(&isp_mmio_local_);

  ping::ColorNoiseReduction_UvDelta2Offset::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(500)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Misc::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_alpha_undershoot(10)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Luma1::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_thresh_low(300)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Luma2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_luma_slope_low(1000)
      .set_luma_thresh_high(1000)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Sharpen_Clip::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_clip_str_max(256)
      .set_clip_str_min(256)
      .WriteTo(&isp_mmio_local_);

  ping::MeteringAwb_WhiteLevelAwb::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(900)
      .WriteTo(&isp_mmio_local_);

  ping::PurpleFringeCorrection_Strength2::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_saturation_strength(255)
      .WriteTo(&isp_mmio_local_);

  ping::Top_Bypass4::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_bypass_pf_correction(1)
      .WriteTo(&isp_mmio_local_);

  InputPort_Config0::Get().ReadFrom(&isp_mmio_).set_preset(1).WriteTo(&isp_mmio_);

  ping::Sqrt_BlackLevelIn::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(16384)
      .WriteTo(&isp_mmio_local_);

  ping::Sqrt_BlackLevelOut::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1024)
      .WriteTo(&isp_mmio_local_);

  ping::SquareBe_BlackLevelIn::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1024)
      .WriteTo(&isp_mmio_local_);

  ping::SquareBe_BlackLevelOut::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(16384)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Crop_EnableCrop::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Crop_EnableCrop::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Crop_SizeX::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(2176)
      .WriteTo(&isp_mmio_local_);

  ping::FullResolution::Crop_SizeY::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(2720)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Crop_SizeX::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1152)
      .WriteTo(&isp_mmio_local_);

  ping::DownScaled::Crop_SizeY::Get()
      .ReadFrom(&isp_mmio_local_)
      .set_value(1440)
      .WriteTo(&isp_mmio_local_);
}

}  // namespace camera
