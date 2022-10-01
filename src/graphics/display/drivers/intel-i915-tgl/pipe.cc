// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/pipe.h"

#include <float.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <math.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/poll-until.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-pipe.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-transcoder.h"
#include "src/graphics/display/drivers/intel-i915-tgl/tiling.h"

namespace {

uint32_t float_to_i915_csc_offset(float f) {
  ZX_DEBUG_ASSERT(0 <= f && f < 1.0f);  // Controller::CheckConfiguration validates this

  // f is in [0, 1). Multiply by 2^12 to convert to a 12-bit fixed-point fraction.
  return static_cast<uint32_t>(f * pow(FLT_RADIX, 12));
}

uint32_t float_to_i915_csc_coefficient(float f) {
  tgl_registers::CscCoeffFormat res;
  if (f < 0) {
    f *= -1;
    res.set_sign(1);
  }

  if (f < .125) {
    res.set_exponent(res.kExponent0125);
    f /= .125f;
  } else if (f < .25) {
    res.set_exponent(res.kExponent025);
    f /= .25f;
  } else if (f < .5) {
    res.set_exponent(res.kExponent05);
    f /= .5f;
  } else if (f < 1) {
    res.set_exponent(res.kExponent1);
  } else if (f < 2) {
    res.set_exponent(res.kExponent2);
    f /= 2.0f;
  } else {
    res.set_exponent(res.kExponent4);
    f /= 4.0f;
  }
  f = (f * 512) + .5f;

  if (f >= 512) {
    res.set_mantissa(0x1ff);
  } else {
    res.set_mantissa(static_cast<uint16_t>(f));
  }

  return res.reg_value();
}

uint32_t encode_pipe_color_component(uint8_t component) {
  // Convert to unsigned .10 fixed point format
  return component << 2;
}

}  // namespace

namespace i915_tgl {

Pipe::Pipe(fdf::MmioBuffer* mmio_space, tgl_registers::Platform platform, tgl_registers::Pipe pipe,
           PowerWellRef pipe_power)
    : mmio_space_(mmio_space),
      platform_(platform),
      pipe_(pipe),
      pipe_power_(std::move(pipe_power)) {}

// static
void Pipe::ResetPipe(tgl_registers::Pipe pipe, fdf::MmioBuffer* mmio_space) {
  tgl_registers::PipeRegs pipe_regs(pipe);

  // Disable planes, bottom color, and cursor
  // TODO(fxbug.dev/109368): Add support for Skylake/Kaby Lake (num of panels'
  // = 3).
  constexpr size_t kNumPanelsTigerLake = 7;

  for (size_t i = 0; i < kNumPanelsTigerLake; i++) {
    pipe_regs.PlaneControl(i).FromValue(0).WriteTo(mmio_space);
    pipe_regs.PlaneSurface(i).FromValue(0).WriteTo(mmio_space);
  }
  auto cursor_ctrl = pipe_regs.CursorCtrl().ReadFrom(mmio_space);
  cursor_ctrl.set_mode_select(tgl_registers::CursorCtrl::kDisabled);
  cursor_ctrl.WriteTo(mmio_space);
  pipe_regs.CursorBase().FromValue(0).WriteTo(mmio_space);
  pipe_regs.PipeBottomColor().FromValue(0).WriteTo(mmio_space);
}

// static
void Pipe::ResetTranscoder(tgl_registers::Trans transcoder, fdf::MmioBuffer* mmio_space) {
  tgl_registers::TranscoderRegs transcoder_regs(transcoder);

  // Disable transcoder and wait for it to stop. These are the "Disable
  // Transcoder" steps from:
  //
  // Tiger Lake - IHD-OS-TGL-Vol 12-12.21
  // * "DSI Transcoder Disable Sequence" pages 128-129 (Incomplete)
  // * "Sequences for DisplayPort" > "Disable Sequence" pages 147-148 (Incomplete)
  // * "Sequences for HDMI and DVI" > "Disable Sequence" pages 150-151
  // * "Sequences for WD" > "Disable Sequence" pages 151-152 (Incomplete)
  // Kaby Lake - IHD-OS-KBL-Vol 12-1.17
  // * "Sequences for DisplayPort" > "Disable Sequence" pages 115-116 (Incomplete)
  // * "Sequences for HDMI" > "Disable Sequence" page 118
  // Skylake - IHD-OS-SKL-Vol 12-05.16
  // * "Sequences for DisplayPort" > "Disable Sequence" pages 115-116 (Incomplete)
  // * "Sequences for HDMI and DVI" > "Disable Sequence" page 118
  //
  // The transcoder should be turned off only after the associated backlight,
  // audio, and image planes are disabled.
  auto transcoder_config = transcoder_regs.Config().ReadFrom(mmio_space);
  transcoder_config.set_enabled_target(false).WriteTo(mmio_space);

  // Wait for off status in TRANS_CONF, timeout after two frames.
  // Here we wait for 60 msecs, which is enough to guarantee to include two
  // whole frames in ~50 fps.
  constexpr size_t kTransConfStatusWaitTimeoutMs = 60;
  if (!PollUntil([&] { return !transcoder_config.ReadFrom(mmio_space).enabled(); }, zx::msec(1),
                 kTransConfStatusWaitTimeoutMs)) {
    // Because this is a logical "reset", we only log failures rather than
    // crashing the driver.
    zxlogf(WARNING, "Failed to reset transcoder");
    return;
  }

  // Disable transcoder DDI select and clock select.
  auto transcoder_ddi_control = transcoder_regs.DdiControl().ReadFrom(mmio_space);

  // `set_ddi_tiger_lake()` works on both Tiger Lake and Skylake / Kaby Lake
  // when passed std::nullopt, because nullopt translates to zeroing out all the
  // field's bits, and on Kaby Lake the highest bit of "ddi_tiger_lake" is
  // reserved to be zero, so it is safe to set the whole field to zero.
  transcoder_ddi_control.set_enabled(false).set_ddi_tiger_lake(std::nullopt).WriteTo(mmio_space);

  if (transcoder != tgl_registers::TRANS_EDP) {
    auto transcoder_clock_select = transcoder_regs.ClockSelect().ReadFrom(mmio_space);

    // `set_ddi_tiger_lake()` works on both Tiger Lake and Skylake / Kaby Lake
    // when passed std::nullopt, because nullopt translates to zeroing out all
    // the field's bits, and on Kaby Lake the highest bit of
    // "ddi_clock_tiger_lake" is reserved to be zero, so it is safe to set the
    // whole field to zero.
    transcoder_clock_select.set_ddi_clock_tiger_lake(std::nullopt).WriteTo(mmio_space);
  }
}

void Pipe::Reset() {
  ResetPipe(pipe_, mmio_space_);
  zxlogf(DEBUG, "Reset pipe %d", pipe_id());
}

void Pipe::ResetActiveTranscoder() {
  if (in_use()) {
    ResetTranscoder(connected_transcoder_id(), mmio_space_);
    zxlogf(DEBUG, "Reset active transcoder %d for pipe %d", connected_transcoder_id(), pipe_id());
  }
}

void Pipe::Detach() {
  attached_display_ = INVALID_DISPLAY_ID;
  attached_edp_ = false;
}

void Pipe::AttachToDisplay(uint64_t id, bool is_edp) {
  attached_display_ = id;
  attached_edp_ = is_edp;
}

void Pipe::ApplyModeConfig(const display_mode_t& mode) {
  tgl_registers::TranscoderRegs trans_regs(connected_transcoder_id());

  // Configure the rest of the transcoder
  uint32_t h_active = mode.h_addressable - 1;
  uint32_t h_sync_start = h_active + mode.h_front_porch;
  uint32_t h_sync_end = h_sync_start + mode.h_sync_pulse;
  uint32_t h_total = h_active + mode.h_blanking;

  uint32_t v_active = mode.v_addressable - 1;
  uint32_t v_sync_start = v_active + mode.v_front_porch;
  uint32_t v_sync_end = v_sync_start + mode.v_sync_pulse;
  uint32_t v_total = v_active + mode.v_blanking;

  auto h_total_reg = trans_regs.HTotal().FromValue(0);
  h_total_reg.set_count_total(h_total);
  h_total_reg.set_count_active(h_active);
  h_total_reg.WriteTo(mmio_space_);
  auto v_total_reg = trans_regs.VTotal().FromValue(0);
  v_total_reg.set_count_total(v_total);
  v_total_reg.set_count_active(v_active);
  v_total_reg.WriteTo(mmio_space_);

  auto h_sync_reg = trans_regs.HSync().FromValue(0);
  h_sync_reg.set_sync_start(h_sync_start);
  h_sync_reg.set_sync_end(h_sync_end);
  h_sync_reg.WriteTo(mmio_space_);
  auto v_sync_reg = trans_regs.VSync().FromValue(0);
  v_sync_reg.set_sync_start(v_sync_start);
  v_sync_reg.set_sync_end(v_sync_end);
  v_sync_reg.WriteTo(mmio_space_);

  // Assume it is not interlacing...
  trans_regs.VSyncShift()
      .ReadFrom(mmio_space_)
      .set_second_field_vsync_shift(0)
      .WriteTo(mmio_space_);

  // The Intel docs say that H/VBlank should be programmed with the same H/VTotal
  trans_regs.HBlank().FromValue(h_total_reg.reg_value()).WriteTo(mmio_space_);
  trans_regs.VBlank().FromValue(v_total_reg.reg_value()).WriteTo(mmio_space_);

  tgl_registers::PipeRegs pipe_regs(pipe_id());
  auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
  pipe_size.set_horizontal_source_size(mode.h_addressable - 1);
  pipe_size.set_vertical_source_size(mode.v_addressable - 1);
  pipe_size.WriteTo(mmio_space_);
}

void Pipe::LoadActiveMode(display_mode_t* mode) {
  tgl_registers::TranscoderRegs trans_regs(connected_transcoder_id());

  auto h_total_reg = trans_regs.HTotal().ReadFrom(mmio_space_);
  uint32_t h_total = h_total_reg.count_total();
  uint32_t h_active = h_total_reg.count_active();
  auto v_total_reg = trans_regs.VTotal().ReadFrom(mmio_space_);
  uint32_t v_total = v_total_reg.count_total();
  uint32_t v_active = v_total_reg.count_active();

  auto h_sync_reg = trans_regs.HSync().ReadFrom(mmio_space_);
  uint32_t h_sync_start = h_sync_reg.sync_start();
  uint32_t h_sync_end = h_sync_reg.sync_end();
  auto v_sync_reg = trans_regs.VSync().ReadFrom(mmio_space_);
  uint32_t v_sync_start = v_sync_reg.sync_start();
  uint32_t v_sync_end = v_sync_reg.sync_end();

  mode->h_addressable = h_active + 1;
  mode->h_front_porch = h_sync_start - h_active;
  mode->h_sync_pulse = h_sync_end - h_sync_start;
  mode->h_blanking = h_total - h_active;

  mode->v_addressable = v_active + 1;
  mode->v_front_porch = v_sync_start - v_active;
  mode->v_sync_pulse = v_sync_end - v_sync_start;
  mode->v_blanking = v_total - v_active;

  mode->flags = 0;
  auto transcoder_ddi_control = trans_regs.DdiControl().ReadFrom(mmio_space_);
  if (transcoder_ddi_control.vsync_polarity_not_inverted()) {
    mode->flags |= MODE_FLAG_VSYNC_POSITIVE;
  }
  if (transcoder_ddi_control.hsync_polarity_not_inverted()) {
    mode->flags |= MODE_FLAG_HSYNC_POSITIVE;
  }
  if (trans_regs.Config().ReadFrom(mmio_space_).interlaced_display()) {
    mode->flags |= MODE_FLAG_INTERLACED;
  }

  // If we're reusing hardware state, make sure the pipe source size matches
  // the display mode size, since we never scale pipes.
  tgl_registers::PipeRegs pipe_regs(pipe_);
  auto pipe_size = pipe_regs.PipeSourceSize().FromValue(0);
  pipe_size.set_horizontal_source_size(mode->h_addressable - 1);
  pipe_size.set_vertical_source_size(mode->v_addressable - 1);
  pipe_size.WriteTo(mmio_space_);
}

void Pipe::ApplyConfiguration(const display_config_t* config, const config_stamp_t* config_stamp,
                              const SetupGttImageFunc& get_gtt_region_fn) {
  ZX_ASSERT(config);
  ZX_ASSERT(config_stamp);

  if (config_stamps_.empty()) {
    // Initialize the seqno for the first configuration applied to the pipe.
    constexpr uint64_t kInitialConfigStampSeqno = 1ul;
    config_stamps_front_seqno_.emplace(kInitialConfigStampSeqno);
  }
  config_stamps_.push_back(*config_stamp);

  auto current_config_stamp_seqno = *config_stamps_front_seqno_ + config_stamps_.size() - 1;

  tgl_registers::pipe_arming_regs_t regs;
  tgl_registers::PipeRegs pipe_regs(pipe_);

  if (config->cc_flags) {
    float zero_offset[3] = {};
    SetColorConversionOffsets(
        true, config->cc_flags & COLOR_CONVERSION_PREOFFSET ? config->cc_preoffsets : zero_offset);
    SetColorConversionOffsets(false, config->cc_flags & COLOR_CONVERSION_POSTOFFSET
                                         ? config->cc_postoffsets
                                         : zero_offset);

    float identity[3][3] = {
        {
            1,
            0,
            0,
        },
        {
            0,
            1,
            0,
        },
        {
            0,
            0,
            1,
        },
    };
    for (uint32_t i = 0; i < 3; i++) {
      for (uint32_t j = 0; j < 3; j++) {
        float val = config->cc_flags & COLOR_CONVERSION_COEFFICIENTS ? config->cc_coefficients[i][j]
                                                                     : identity[i][j];

        auto reg = pipe_regs.CscCoeff(i, j).ReadFrom(mmio_space_);
        reg.coefficient(i, j).set(float_to_i915_csc_coefficient(val));
        reg.WriteTo(mmio_space_);
      }
    }
  }
  regs.csc_mode = pipe_regs.CscMode().ReadFrom(mmio_space_).reg_value();

  auto bottom_color = pipe_regs.PipeBottomColor().FromValue(0);
  bottom_color.set_csc_enable(!!config->cc_flags);
  bool has_color_layer = config->layer_count && config->layer_list[0]->type == LAYER_TYPE_COLOR;
  if (has_color_layer) {
    color_layer_t* layer = &config->layer_list[0]->cfg.color;
    ZX_DEBUG_ASSERT(layer->format == ZX_PIXEL_FORMAT_RGB_x888 ||
                    layer->format == ZX_PIXEL_FORMAT_ARGB_8888);
    uint32_t color = *reinterpret_cast<const uint32_t*>(layer->color_list);

    bottom_color.set_r(encode_pipe_color_component(static_cast<uint8_t>(color >> 16)));
    bottom_color.set_g(encode_pipe_color_component(static_cast<uint8_t>(color >> 8)));
    bottom_color.set_b(encode_pipe_color_component(static_cast<uint8_t>(color)));
  }
  regs.pipe_bottom_color = bottom_color.reg_value();

  bool scaler_1_claimed = false;
  for (unsigned plane = 0; plane < 3; plane++) {
    primary_layer_t* primary = nullptr;
    for (unsigned j = 0; j < config->layer_count; j++) {
      layer_t* layer = config->layer_list[j];
      if (layer->type == LAYER_TYPE_PRIMARY && (layer->z_index - has_color_layer) == plane) {
        primary = &layer->cfg.primary;
        break;
      }
    }
    ConfigurePrimaryPlane(plane, primary, !!config->cc_flags, &scaler_1_claimed, &regs,
                          current_config_stamp_seqno, get_gtt_region_fn);
  }
  cursor_layer_t* cursor = nullptr;
  if (config->layer_count &&
      config->layer_list[config->layer_count - 1]->type == LAYER_TYPE_CURSOR) {
    cursor = &config->layer_list[config->layer_count - 1]->cfg.cursor;
  }
  ConfigureCursorPlane(cursor, !!config->cc_flags, &regs, current_config_stamp_seqno);

  if (platform_ != tgl_registers::Platform::kTigerLake) {
    pipe_regs.CscMode().FromValue(regs.csc_mode).WriteTo(mmio_space_);
  }
  pipe_regs.PipeBottomColor().FromValue(regs.pipe_bottom_color).WriteTo(mmio_space_);
  pipe_regs.CursorBase().FromValue(regs.cur_base).WriteTo(mmio_space_);
  pipe_regs.CursorPos().FromValue(regs.cur_pos).WriteTo(mmio_space_);
  for (unsigned i = 0; i < tgl_registers::kImagePlaneCount; i++) {
    pipe_regs.PlaneSurface(i).FromValue(regs.plane_surf[i]).WriteTo(mmio_space_);
  }
  pipe_regs.PipeScalerWinSize(0).FromValue(regs.ps_win_sz[0]).WriteTo(mmio_space_);
  if (pipe_ != tgl_registers::PIPE_C) {
    pipe_regs.PipeScalerWinSize(1).FromValue(regs.ps_win_sz[1]).WriteTo(mmio_space_);
  }
}

void Pipe::ConfigurePrimaryPlane(uint32_t plane_num, const primary_layer_t* primary,
                                 bool enable_csc, bool* scaler_1_claimed,
                                 tgl_registers::pipe_arming_regs_t* regs,
                                 uint64_t config_stamp_seqno,
                                 const SetupGttImageFunc& setup_gtt_image) {
  tgl_registers::PipeRegs pipe_regs(pipe_id());

  auto plane_ctrl = pipe_regs.PlaneControl(plane_num).ReadFrom(mmio_space_);
  if (primary == nullptr) {
    plane_ctrl.set_plane_enable(0).WriteTo(mmio_space_);
    regs->plane_surf[plane_num] = 0;
    return;
  }
  plane_ctrl.set_render_decompression(false).set_allow_double_buffer_update_disable(1);

  const image_t* image = &primary->image;

  uint32_t base_address = static_cast<uint32_t>(setup_gtt_image(image, primary->transform_mode));
  uint32_t plane_width;
  uint32_t plane_height;
  uint32_t stride;
  uint32_t x_offset;
  uint32_t y_offset;
  if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY ||
      primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
    plane_width = primary->src_frame.width;
    plane_height = primary->src_frame.height;
    stride = width_in_tiles(image->type, image->width, image->pixel_format);
    x_offset = primary->src_frame.x_pos;
    y_offset = primary->src_frame.y_pos;
  } else {
    uint32_t tile_height = height_in_tiles(image->type, image->height, image->pixel_format);
    uint32_t tile_px_height = get_tile_px_height(image->type, image->pixel_format);
    uint32_t total_height = tile_height * tile_px_height;

    plane_width = primary->src_frame.height;
    plane_height = primary->src_frame.width;
    stride = tile_height;
    x_offset = total_height - primary->src_frame.y_pos - primary->src_frame.height;
    y_offset = primary->src_frame.x_pos;
  }

  if (plane_width == primary->dest_frame.width && plane_height == primary->dest_frame.height) {
    auto plane_pos = pipe_regs.PlanePosition(plane_num).FromValue(0);
    plane_pos.set_x_pos(primary->dest_frame.x_pos);
    plane_pos.set_y_pos(primary->dest_frame.y_pos);
    plane_pos.WriteTo(mmio_space_);

    // If there's a scaler pointed at this plane, immediately disable it
    // in case there's nothing else that will claim it this frame.
    if (scaled_planes_[pipe_id()][plane_num]) {
      uint32_t scaler_idx = scaled_planes_[pipe_id()][plane_num] - 1;
      pipe_regs.PipeScalerCtrl(scaler_idx).ReadFrom(mmio_space_).set_enable(0).WriteTo(mmio_space_);
      scaled_planes_[pipe_id()][plane_num] = 0;
      regs->ps_win_sz[scaler_idx] = 0;
    }
  } else {
    pipe_regs.PlanePosition(plane_num).FromValue(0).WriteTo(mmio_space_);

    auto ps_ctrl = pipe_regs.PipeScalerCtrl(*scaler_1_claimed).ReadFrom(mmio_space_);
    ps_ctrl.set_mode(ps_ctrl.kDynamic);
    if (platform_ != tgl_registers::Platform::kTigerLake) {
      // The mode bits are different in Tiger Lake.
      if (primary->src_frame.width > 2048) {
        float max_dynamic_height = static_cast<float>(plane_height) *
                                   tgl_registers::PipeScalerCtrl::kDynamicMaxVerticalRatio2049;
        if (static_cast<uint32_t>(max_dynamic_height) < primary->dest_frame.height) {
          // TODO(stevensd): This misses some cases where 7x5 can be used.
          ps_ctrl.set_enable(ps_ctrl.k7x5);
        }
      }
    }

    ps_ctrl.set_binding(plane_num + 1);
    ps_ctrl.set_enable(1);
    ps_ctrl.WriteTo(mmio_space_);

    auto ps_win_pos = pipe_regs.PipeScalerWinPosition(*scaler_1_claimed).FromValue(0);
    ps_win_pos.set_x_pos(primary->dest_frame.x_pos);
    ps_win_pos.set_x_pos(primary->dest_frame.y_pos);
    ps_win_pos.WriteTo(mmio_space_);

    auto ps_win_size = pipe_regs.PipeScalerWinSize(*scaler_1_claimed).FromValue(0);
    ps_win_size.set_x_size(primary->dest_frame.width);
    ps_win_size.set_y_size(primary->dest_frame.height);
    regs->ps_win_sz[*scaler_1_claimed] = ps_win_size.reg_value();

    scaled_planes_[pipe_id()][plane_num] = (*scaler_1_claimed) + 1;
    *scaler_1_claimed = true;
  }

  auto plane_size = pipe_regs.PlaneSurfaceSize(plane_num).FromValue(0);
  plane_size.set_width_minus_1(plane_width - 1);
  plane_size.set_height_minus_1(plane_height - 1);
  plane_size.WriteTo(mmio_space_);

  auto plane_offset = pipe_regs.PlaneOffset(plane_num).FromValue(0);
  plane_offset.set_start_x(x_offset);
  plane_offset.set_start_y(y_offset);
  plane_offset.WriteTo(mmio_space_);

  auto stride_reg = pipe_regs.PlaneSurfaceStride(plane_num).FromValue(0);
  stride_reg.set_stride(stride);
  stride_reg.WriteTo(mmio_space_);

  if (platform_ == tgl_registers::Platform::kTigerLake) {
    auto plane_color_ctl = pipe_regs.PlaneColorControlTigerLake(0).ReadFrom(mmio_space_);
    plane_color_ctl.set_pipe_gamma_enable(0)
        .set_pipe_csc_enable(enable_csc)
        .set_plane_input_csc_enable(0)
        .set_plane_gamma_disable(1)
        .WriteTo(mmio_space_);
  }

  auto plane_key_mask = pipe_regs.PlaneKeyMask(plane_num).FromValue(0);
  if (primary->alpha_mode != ALPHA_DISABLE && !isnan(primary->alpha_layer_val)) {
    plane_key_mask.set_plane_alpha_enable(1);

    uint8_t alpha = static_cast<uint8_t>(round(primary->alpha_layer_val * 255));

    auto plane_key_max = pipe_regs.PlaneKeyMax(plane_num).FromValue(0);
    plane_key_max.set_plane_alpha_value(alpha);
    plane_key_max.WriteTo(mmio_space_);
  }
  plane_key_mask.WriteTo(mmio_space_);
  if (primary->alpha_mode == ALPHA_DISABLE ||
      primary->image.pixel_format == ZX_PIXEL_FORMAT_RGB_x888 ||
      primary->image.pixel_format == ZX_PIXEL_FORMAT_BGR_888x) {
    plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaDisable);
  } else if (primary->alpha_mode == ALPHA_PREMULTIPLIED) {
    plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaPreMultiply);
  } else {
    ZX_ASSERT(primary->alpha_mode == ALPHA_HW_MULTIPLY);
    plane_ctrl.set_alpha_mode(plane_ctrl.kAlphaHwMultiply);
  }

  plane_ctrl.set_plane_enable(1);
  if (platform_ != tgl_registers::Platform::kTigerLake) {
    plane_ctrl.set_pipe_csc_enable_kaby_lake(enable_csc);
  }
  if (platform_ == tgl_registers::Platform::kTigerLake) {
    plane_ctrl.set_source_pixel_format_tiger_lake(plane_ctrl.kFormatRgb8888);
  } else {
    plane_ctrl.set_source_pixel_format_kaby_lake(plane_ctrl.kFormatRgb8888);
  }
  if (primary->image.pixel_format == ZX_PIXEL_FORMAT_ABGR_8888 ||
      primary->image.pixel_format == ZX_PIXEL_FORMAT_BGR_888x) {
    plane_ctrl.set_rgb_color_order(plane_ctrl.kOrderRgbx);
  } else {
    plane_ctrl.set_rgb_color_order(plane_ctrl.kOrderBgrx);
  }
  if (primary->image.type == IMAGE_TYPE_SIMPLE) {
    plane_ctrl.set_tiled_surface(plane_ctrl.kLinear);
  } else if (primary->image.type == IMAGE_TYPE_X_TILED) {
    plane_ctrl.set_tiled_surface(plane_ctrl.kTilingX);
  } else if (primary->image.type == IMAGE_TYPE_Y_LEGACY_TILED) {
    plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYLegacy);
  } else {
    ZX_ASSERT(primary->image.type == IMAGE_TYPE_YF_TILED);
    plane_ctrl.set_tiled_surface(plane_ctrl.kTilingYF);
  }
  if (primary->transform_mode == FRAME_TRANSFORM_IDENTITY) {
    plane_ctrl.set_plane_rotation(plane_ctrl.kIdentity);
  } else if (primary->transform_mode == FRAME_TRANSFORM_ROT_90) {
    plane_ctrl.set_plane_rotation(plane_ctrl.k90deg);
  } else if (primary->transform_mode == FRAME_TRANSFORM_ROT_180) {
    plane_ctrl.set_plane_rotation(plane_ctrl.k180deg);
  } else {
    ZX_ASSERT(primary->transform_mode == FRAME_TRANSFORM_ROT_270);
    plane_ctrl.set_plane_rotation(plane_ctrl.k270deg);
  }
  if (platform_ == tgl_registers::Platform::kTigerLake) {
    plane_ctrl.set_render_decompression(false).set_allow_double_buffer_update_disable(true).WriteTo(
        mmio_space_);
  }

  auto plane_surface = pipe_regs.PlaneSurface(plane_num).ReadFrom(mmio_space_);
  plane_surface.set_surface_base_addr(base_address >> plane_surface.kRShiftCount);
  regs->plane_surf[plane_num] = plane_surface.reg_value();

  latest_config_seqno_of_image_[image->handle] = config_stamp_seqno;
}

void Pipe::ConfigureCursorPlane(const cursor_layer_t* cursor, bool enable_csc,
                                tgl_registers::pipe_arming_regs_t* regs,
                                uint64_t config_stamp_seqno) {
  tgl_registers::PipeRegs pipe_regs(pipe_id());

  auto cursor_ctrl = pipe_regs.CursorCtrl().ReadFrom(mmio_space_);
  // The hardware requires that the cursor has at least one pixel on the display,
  // so disable the plane if there is no overlap.
  if (cursor == nullptr) {
    cursor_ctrl.set_mode_select(cursor_ctrl.kDisabled).WriteTo(mmio_space_);
    regs->cur_base = regs->cur_pos = 0;
    return;
  }

  if (cursor->image.width == 64) {
    cursor_ctrl.set_mode_select(cursor_ctrl.kArgb64x64);
  } else if (cursor->image.width == 128) {
    cursor_ctrl.set_mode_select(cursor_ctrl.kArgb128x128);
  } else if (cursor->image.width == 256) {
    cursor_ctrl.set_mode_select(cursor_ctrl.kArgb256x256);
  } else {
    // The configuration was not properly validated
    ZX_ASSERT(false);
  }
  cursor_ctrl.set_pipe_csc_enable(enable_csc);
  cursor_ctrl.WriteTo(mmio_space_);

  auto cursor_pos = pipe_regs.CursorPos().FromValue(0);
  if (cursor->x_pos < 0) {
    cursor_pos.set_x_sign(1);
    cursor_pos.set_x_pos(-cursor->x_pos);
  } else {
    cursor_pos.set_x_pos(cursor->x_pos);
  }
  if (cursor->y_pos < 0) {
    cursor_pos.set_y_sign(1);
    cursor_pos.set_y_pos(-cursor->y_pos);
  } else {
    cursor_pos.set_y_pos(cursor->y_pos);
  }
  regs->cur_pos = cursor_pos.reg_value();

  uint32_t base_address = static_cast<uint32_t>(reinterpret_cast<uint64_t>(cursor->image.handle));
  auto cursor_base = pipe_regs.CursorBase().ReadFrom(mmio_space_);
  cursor_base.set_cursor_base(base_address >> cursor_base.kPageShift);
  regs->cur_base = cursor_base.reg_value();

  latest_config_seqno_of_image_[cursor->image.handle] = config_stamp_seqno;
}

std::optional<config_stamp_t> Pipe::GetVsyncConfigStamp(
    const std::vector<uint64_t>& image_handles) {
  uint64_t min_config_seqno = std::numeric_limits<uint64_t>::max();
  for (const uint64_t handle : image_handles) {
    auto config_it = latest_config_seqno_of_image_.find(handle);
    if (config_it == latest_config_seqno_of_image_.end()) {
      continue;
    }
    min_config_seqno = std::min(min_config_seqno, config_it->second);
  }

  if (min_config_seqno == std::numeric_limits<uint64_t>::max()) {
    // Display device may carry garbage contents in the registers, for example
    // if the driver restarted. In that case none of the images stored in the
    // device register will be recognized by the driver, so we just return a
    // null config stamp to ignore it.
    zxlogf(DEBUG, "%s: NO valid images for the display.", __func__);
    return std::nullopt;
  }
  if (config_stamps_.empty() || !config_stamps_front_seqno_.has_value()) {
    // Vsync signals could be sent to the driver before the first
    // ApplyConfiguration() is called. In that case the Vsync signal should be
    // just ignored by the driver, so we return a null config stamp.
    zxlogf(DEBUG, "%s: No config has been applied.", __func__);
    return std::nullopt;
  }
  if (config_stamps_front_seqno_ > min_config_seqno) {
    zxlogf(ERROR, "%s: Device returns a config with seqno (%lu) that is already evicted.", __func__,
           min_config_seqno);
    return std::nullopt;
  }

  // Since config stamps in the linked list have consecutive seqnos, we can just
  // remove the first |min_config_seqno - *config_stamps_front_seqno_| elements
  // to evict all config stamps older than that with seqno |min_config_seqno|.
  config_stamps_.erase(
      config_stamps_.begin(),
      std::next(config_stamps_.begin(), min_config_seqno - *config_stamps_front_seqno_));
  config_stamps_front_seqno_.emplace(min_config_seqno);

  ZX_DEBUG_ASSERT(!config_stamps_.empty());
  return config_stamps_.front();
}

void Pipe::SetColorConversionOffsets(bool preoffsets, const float vals[3]) {
  tgl_registers::PipeRegs pipe_regs(pipe_id());

  for (uint32_t i = 0; i < 3; i++) {
    float offset = vals[i];
    auto offset_reg = pipe_regs.CscOffset(preoffsets, i).FromValue(0);
    if (offset < 0) {
      offset_reg.set_sign(1);
      offset *= -1;
    }
    offset_reg.set_magnitude(float_to_i915_csc_offset(offset));
    offset_reg.WriteTo(mmio_space_);
  }
}

}  // namespace i915_tgl
