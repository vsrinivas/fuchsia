// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vout.h"

#include <fbl/alloc_checker.h>

namespace amlogic_display {

namespace {

// List of supported pixel formats
constexpr zx_pixel_format_t kDsiSupportedPixelFormats[4] = {
    ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_ABGR_8888,
    ZX_PIXEL_FORMAT_BGR_888x};

// List of supported features
struct supported_features_t {
  bool supports_afbc;
  bool supports_capture;
};

// TODO(fxb/69025): read feature support from metadata instead of hardcoding.
constexpr supported_features_t kDsiSupportedFeatures = supported_features_t{
    .supports_afbc = true,
    .supports_capture = true,
};

}  // namespace

zx_status_t Vout::InitDsi(zx_device_t* parent, uint32_t panel_type, uint32_t width,
                          uint32_t height) {
  type_ = VoutType::kDsi;

  supports_afbc_ = kDsiSupportedFeatures.supports_afbc;
  supports_capture_ = kDsiSupportedFeatures.supports_capture;

  dsi_.width = width;
  dsi_.height = height;

  const display_setting_t* init_disp_table;
  switch (panel_type) {
    case PANEL_TV070WSM_FT:
      init_disp_table = &kDisplaySettingTV070WSM_FT;
      break;
    case PANEL_P070ACB_FT:
      init_disp_table = &kDisplaySettingP070ACB_FT;
      break;
    case PANEL_TV101WXM_FT:
      init_disp_table = &kDisplaySettingTV101WXM_FT;
      break;
    case PANEL_G101B158_FT:
      init_disp_table = &kDisplaySettingG101B158_FT;
      break;
    case PANEL_TV080WXM_FT:
      init_disp_table = &kDisplaySettingTV080WXM_FT;
      break;
    default:
      DISP_ERROR("Unsupported panel detected!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }

  dsi_.disp_setting.h_active = init_disp_table->h_active;
  dsi_.disp_setting.v_active = init_disp_table->v_active;
  dsi_.disp_setting.h_period = init_disp_table->h_period;
  dsi_.disp_setting.v_period = init_disp_table->v_period;
  dsi_.disp_setting.hsync_width = init_disp_table->hsync_width;
  dsi_.disp_setting.hsync_bp = init_disp_table->hsync_bp;
  dsi_.disp_setting.hsync_pol = init_disp_table->hsync_pol;
  dsi_.disp_setting.vsync_width = init_disp_table->vsync_width;
  dsi_.disp_setting.vsync_bp = init_disp_table->vsync_bp;
  dsi_.disp_setting.vsync_pol = init_disp_table->vsync_pol;
  dsi_.disp_setting.lcd_clock = init_disp_table->lcd_clock;
  dsi_.disp_setting.clock_factor = init_disp_table->clock_factor;
  dsi_.disp_setting.lane_num = init_disp_table->lane_num;
  dsi_.disp_setting.bit_rate_max = init_disp_table->bit_rate_max;

  fbl::AllocChecker ac;
  dsi_.dsi_host = fbl::make_unique_checked<amlogic_display::AmlDsiHost>(&ac, parent, panel_type);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  return ZX_OK;
}

zx_status_t Vout::RestartDisplay(zx_device_t* parent) {
  ddk::PDev pdev;
  zx_status_t status = ddk::PDev::FromFragment(parent, &pdev);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }

  fbl::AllocChecker ac;
  switch (type_) {
    case VoutType::kDsi:
      dsi_.clock = fbl::make_unique_checked<amlogic_display::AmlogicDisplayClock>(&ac);
      if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
      }

      status = dsi_.clock->Init(pdev);
      if (status != ZX_OK) {
        DISP_ERROR("Could not initialize Clock object\n");
        return status;
      }

      // Enable all display related clocks
      status = dsi_.clock->Enable(dsi_.disp_setting);
      if (status != ZX_OK) {
        DISP_ERROR("Could not enable display clocks!\n");
        return status;
      }

      // Program and Enable DSI Host Interface
      status = dsi_.dsi_host->Init(dsi_.clock->GetBitrate());
      if (status != ZX_OK) {
        DISP_ERROR("Could not initialize DSI Host\n");
        return status;
      }

      status = dsi_.dsi_host->HostOn(dsi_.disp_setting);
      if (status != ZX_OK) {
        DISP_ERROR("DSI Host On failed! %d\n", status);
        return status;
      }
      break;
    default:
      DISP_ERROR("Unrecognized Vout type %u\n", type_);
      return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

void Vout::PopulateAddedDisplayArgs(added_display_args_t* args, uint64_t display_id) {
  switch (type_) {
    case VoutType::kDsi:
      args->display_id = display_id;
      args->edid_present = false;
      args->panel.params.height = dsi_.height;
      args->panel.params.width = dsi_.width;
      args->panel.params.refresh_rate_e2 = 6000;  // Just guess that it's 60fps
      args->pixel_format_list = kDsiSupportedPixelFormats;
      args->pixel_format_count = countof(kDsiSupportedPixelFormats);
      args->cursor_info_count = 0;
      break;
    default:
      zxlogf(ERROR, "Unrecognized vout type %u\n", type_);
      return;
  }
}

bool Vout::IsFormatSupported(zx_pixel_format_t format) {
  switch (type_) {
    case VoutType::kDsi:
      for (auto f : kDsiSupportedPixelFormats) {
        if (f == format) {
          return true;
        }
      }
      return false;
    default:
      return false;
  }
}

void Vout::Dump() {
  switch (type_) {
    case VoutType::kDsi:
      DISP_INFO("#############################\n");
      DISP_INFO("Dumping disp_setting structure:\n");
      DISP_INFO("#############################\n");
      DISP_INFO("h_active = 0x%x (%u)\n", dsi_.disp_setting.h_active, dsi_.disp_setting.h_active);
      DISP_INFO("v_active = 0x%x (%u)\n", dsi_.disp_setting.v_active, dsi_.disp_setting.v_active);
      DISP_INFO("h_period = 0x%x (%u)\n", dsi_.disp_setting.h_period, dsi_.disp_setting.h_period);
      DISP_INFO("v_period = 0x%x (%u)\n", dsi_.disp_setting.v_period, dsi_.disp_setting.v_period);
      DISP_INFO("hsync_width = 0x%x (%u)\n", dsi_.disp_setting.hsync_width,
                dsi_.disp_setting.hsync_width);
      DISP_INFO("hsync_bp = 0x%x (%u)\n", dsi_.disp_setting.hsync_bp, dsi_.disp_setting.hsync_bp);
      DISP_INFO("hsync_pol = 0x%x (%u)\n", dsi_.disp_setting.hsync_pol,
                dsi_.disp_setting.hsync_pol);
      DISP_INFO("vsync_width = 0x%x (%u)\n", dsi_.disp_setting.vsync_width,
                dsi_.disp_setting.vsync_width);
      DISP_INFO("vsync_bp = 0x%x (%u)\n", dsi_.disp_setting.vsync_bp, dsi_.disp_setting.vsync_bp);
      DISP_INFO("vsync_pol = 0x%x (%u)\n", dsi_.disp_setting.vsync_pol,
                dsi_.disp_setting.vsync_pol);
      DISP_INFO("lcd_clock = 0x%x (%u)\n", dsi_.disp_setting.lcd_clock,
                dsi_.disp_setting.lcd_clock);
      DISP_INFO("lane_num = 0x%x (%u)\n", dsi_.disp_setting.lane_num, dsi_.disp_setting.lane_num);
      DISP_INFO("bit_rate_max = 0x%x (%u)\n", dsi_.disp_setting.bit_rate_max,
                dsi_.disp_setting.bit_rate_max);
      DISP_INFO("clock_factor = 0x%x (%u)\n", dsi_.disp_setting.clock_factor,
                dsi_.disp_setting.clock_factor);
      break;
    default:
      DISP_ERROR("Unrecognized Vout type %u\n", type_);
  }
}

}  // namespace amlogic_display
