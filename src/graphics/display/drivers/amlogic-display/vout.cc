// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/vout.h"

#include <fbl/alloc_checker.h>

namespace amlogic_display {

namespace {

// List of supported pixel formats
constexpr zx_pixel_format_t kDsiSupportedPixelFormats[4] = {
    ZX_PIXEL_FORMAT_ARGB_8888, ZX_PIXEL_FORMAT_RGB_x888, ZX_PIXEL_FORMAT_ABGR_8888,
    ZX_PIXEL_FORMAT_BGR_888x};
// TODO(fxb/69236): Add more supported formats
zx_pixel_format_t kHdmiSupportedPixelFormats[] = {ZX_PIXEL_FORMAT_ARGB_8888,
                                                  ZX_PIXEL_FORMAT_RGB_x888};

// List of supported features
struct supported_features_t {
  bool afbc;
  bool capture;
  bool hpd;
};

// TODO(fxb/69025): read feature support from metadata instead of hardcoding.
constexpr supported_features_t kDsiSupportedFeatures = supported_features_t{
    .afbc = true,
    .capture = true,
    .hpd = false,
};

constexpr supported_features_t kHdmiSupportedFeatures = supported_features_t{
    .afbc = false,
    .capture = false,
    .hpd = true,
};

}  // namespace

zx_status_t Vout::InitDsi(zx_device_t* parent, uint32_t panel_type, uint32_t width,
                          uint32_t height) {
  type_ = VoutType::kDsi;

  supports_afbc_ = kDsiSupportedFeatures.afbc;
  supports_capture_ = kDsiSupportedFeatures.capture;
  supports_hpd_ = kDsiSupportedFeatures.hpd;

  dsi_.width = width;
  dsi_.height = height;

  auto dsi_host = amlogic_display::DsiHost::Create(parent, panel_type);
  if (dsi_host.is_error()) {
    DISP_ERROR("Could not create DSI host: %s\n", dsi_host.status_string());
    return dsi_host.status_value();
  }
  dsi_.dsi_host = std::move(dsi_host.value());
  ZX_ASSERT(dsi_.dsi_host);

  ddk::PDev pdev;
  auto status = ddk::PDev::FromFragment(parent, &pdev);
  if (status != ZX_OK) {
    DISP_ERROR("Could not get PDEV protocol\n");
    return status;
  }
  auto clock = amlogic_display::Clock::Create(pdev, kBootloaderDisplayEnabled);
  if (clock.is_error()) {
    DISP_ERROR("Could not create Clock: %s\n", clock.status_string());
    return clock.status_value();
  }

  dsi_.clock = std::move(clock.value());
  ZX_ASSERT(dsi_.clock);

  DISP_INFO("Fixed panel type is %d", dsi_.dsi_host->panel_type());
  switch (dsi_.dsi_host->panel_type()) {
    case PANEL_TV070WSM_FT:
    case PANEL_TV070WSM_FT_9365:
      dsi_.disp_setting = kDisplaySettingTV070WSM_FT;
      break;
    case PANEL_P070ACB_FT:
      dsi_.disp_setting = kDisplaySettingP070ACB_FT;
      break;
    case PANEL_KD070D82_FT_9365:
    case PANEL_KD070D82_FT:
      dsi_.disp_setting = kDisplaySettingKD070D82_FT;
      break;
    case PANEL_TV101WXM_FT_9365:
    case PANEL_TV101WXM_FT:
      dsi_.disp_setting = kDisplaySettingTV101WXM_FT;
      break;
    case PANEL_G101B158_FT:
      dsi_.disp_setting = kDisplaySettingG101B158_FT;
      break;
    case PANEL_TV080WXM_FT:
      dsi_.disp_setting = kDisplaySettingTV080WXM_FT;
      break;
    case PANEL_TV070WSM_ST7703I:
      dsi_.disp_setting = kDisplaySettingTV070WSM_ST7703I;
      break;
    default:
      DISP_ERROR("Unsupported panel detected!\n");
      return ZX_ERR_NOT_SUPPORTED;
  }
  return ZX_OK;
}

zx_status_t Vout::InitHdmi(zx_device_t* parent) {
  type_ = VoutType::kHdmi;

  supports_afbc_ = kHdmiSupportedFeatures.afbc;
  supports_capture_ = kHdmiSupportedFeatures.capture;
  supports_hpd_ = kHdmiSupportedFeatures.hpd;

  ddk::HdmiProtocolClient hdmi(parent, "hdmi");
  if (!hdmi.is_valid()) {
    zxlogf(ERROR, "Could not get hdmi fragment");
    return ZX_ERR_INTERNAL;
  }
  zx::channel client_end, server_end;
  zx_status_t status;
  if ((status = zx::channel::create(0, &client_end, &server_end)) != ZX_OK) {
    zxlogf(ERROR, "Could not create channel %d\n", status);
    return status;
  }
  hdmi.Connect(std::move(server_end));

  fbl::AllocChecker ac;
  hdmi_.hdmi_host =
      fbl::make_unique_checked<amlogic_display::HdmiHost>(&ac, parent, std::move(client_end));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  status = hdmi_.hdmi_host->Init();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Could not initialize HDMI host %d\n", status);
    return status;
  }

  return ZX_OK;
}

zx_status_t Vout::RestartDisplay() {
  DISP_INFO("restarting display");
  zx_status_t status;
  switch (type_) {
    case VoutType::kDsi:

      // Enable all display related clocks
      status = dsi_.clock->Enable(dsi_.disp_setting);
      if (status != ZX_OK) {
        DISP_ERROR("Could not enable display clocks!\n");
        return status;
      }

      dsi_.clock->SetVideoOn(false);
      // Program and Enable DSI Host Interface
      status = dsi_.dsi_host->Enable(dsi_.disp_setting, dsi_.clock->GetBitrate());
      if (status != ZX_OK) {
        DISP_ERROR("DSI Host On failed! %d\n", status);
        return status;
      }
      dsi_.clock->SetVideoOn(true);

      break;
    case VoutType::kHdmi:
      status = hdmi_.hdmi_host->HostOn();
      if (status != ZX_OK) {
        DISP_ERROR("HDMI initialization failed! %d\n", status);
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
      args->pixel_format_count = std::size(kDsiSupportedPixelFormats);
      args->cursor_info_count = 0;
      break;
    case VoutType::kHdmi:
      args->display_id = display_id;
      args->edid_present = true;
      args->panel.i2c_bus_id = 0;
      args->pixel_format_list = kHdmiSupportedPixelFormats;
      args->pixel_format_count = std::size(kHdmiSupportedPixelFormats);
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
    case VoutType::kHdmi:
      for (auto f : kHdmiSupportedPixelFormats) {
        if (f == format) {
          return true;
        }
      }
      return false;
    default:
      return false;
  }
}

void Vout::DisplayConnected() {
  switch (type_) {
    case kHdmi:
      memset(&hdmi_.cur_display_mode_, 0, sizeof(display_mode_t));
      break;
    default:
      break;
  }
}

void Vout::DisplayDisconnected() {
  switch (type_) {
    case kHdmi:
      hdmi_.hdmi_host->HostOff();
      break;
    default:
      break;
  }
}

zx::status<> Vout::PowerOff() {
  if (type_ == kDsi) {
    dsi_.clock->Disable();
    dsi_.dsi_host->Disable(dsi_.disp_setting);
    return zx::ok();
  }
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

zx::status<> Vout::PowerOn() {
  if (type_ == kDsi) {
    dsi_.clock->Enable(dsi_.disp_setting);
    dsi_.dsi_host->Enable(dsi_.disp_setting, dsi_.clock->GetBitrate());
    return zx::ok();
  }
  return zx::error(ZX_ERR_NOT_SUPPORTED);
}

bool Vout::CheckMode(const display_mode_t* mode) {
  switch (type_) {
    case kDsi:
      return false;
    case kHdmi:
      return memcmp(&hdmi_.cur_display_mode_, mode, sizeof(display_mode_t)) &&
             (hdmi_.hdmi_host->GetVic(mode) != ZX_OK);
    default:
      return false;
  }
}

zx_status_t Vout::ApplyConfiguration(const display_mode_t* mode) {
  zx_status_t status;
  switch (type_) {
    case kDsi:
      return ZX_OK;
    case kHdmi:
      if (!memcmp(&hdmi_.cur_display_mode_, mode, sizeof(display_mode_t))) {
        // No new configs
        return ZX_OK;
      }

      display_mode_t modified_mode;
      memcpy(&modified_mode, mode, sizeof(display_mode_t));
      status = hdmi_.hdmi_host->GetVic(&modified_mode);
      if (status != ZX_OK) {
        DISP_ERROR("Apply with bad mode");
        return status;
      }

      memcpy(&hdmi_.cur_display_mode_, mode, sizeof(display_mode_t));
      // FIXME: Need documentation for HDMI PLL initialization
      hdmi_.hdmi_host->ConfigurePll();
      hdmi_.hdmi_host->ModeSet(modified_mode);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vout::OnDisplaysChanged(added_display_info_t& info) {
  switch (type_) {
    case kDsi:
      // Not used anywhere: ZX_PIXEL_FORMAT_RGB_x888;
      return ZX_OK;
    case kHdmi:
      hdmi_.hdmi_host->UpdateOutputColorFormat(
          info.is_standard_srgb_out ? fuchsia_hardware_hdmi::wire::ColorFormat::kCfRgb
                                    : fuchsia_hardware_hdmi::wire::ColorFormat::kCf444);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

zx_status_t Vout::EdidTransfer(uint32_t bus_id, const i2c_impl_op_t* op_list, size_t op_count) {
  switch (type_) {
    case kHdmi:
      hdmi_.hdmi_host->EdidTransfer(bus_id, op_list, op_count);
      return ZX_OK;
    default:
      return ZX_ERR_NOT_SUPPORTED;
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
    case VoutType::kHdmi:
      DISP_INFO("pixel_clock_10khz = 0x%x (%u)\n", hdmi_.cur_display_mode_.pixel_clock_10khz,
                hdmi_.cur_display_mode_.pixel_clock_10khz);
      DISP_INFO("h_addressable = 0x%x (%u)\n", hdmi_.cur_display_mode_.h_addressable,
                hdmi_.cur_display_mode_.h_addressable);
      DISP_INFO("h_front_porch = 0x%x (%u)\n", hdmi_.cur_display_mode_.h_front_porch,
                hdmi_.cur_display_mode_.h_front_porch);
      DISP_INFO("h_sync_pulse = 0x%x (%u)\n", hdmi_.cur_display_mode_.h_sync_pulse,
                hdmi_.cur_display_mode_.h_sync_pulse);
      DISP_INFO("h_blanking = 0x%x (%u)\n", hdmi_.cur_display_mode_.h_blanking,
                hdmi_.cur_display_mode_.h_blanking);
      DISP_INFO("v_addressable = 0x%x (%u)\n", hdmi_.cur_display_mode_.v_addressable,
                hdmi_.cur_display_mode_.v_addressable);
      DISP_INFO("v_front_porch = 0x%x (%u)\n", hdmi_.cur_display_mode_.v_front_porch,
                hdmi_.cur_display_mode_.v_front_porch);
      DISP_INFO("v_sync_pulse = 0x%x (%u)\n", hdmi_.cur_display_mode_.v_sync_pulse,
                hdmi_.cur_display_mode_.v_sync_pulse);
      DISP_INFO("v_blanking = 0x%x (%u)\n", hdmi_.cur_display_mode_.v_blanking,
                hdmi_.cur_display_mode_.v_blanking);
      DISP_INFO("flags = 0x%x (%u)\n", hdmi_.cur_display_mode_.flags,
                hdmi_.cur_display_mode_.flags);
      break;
    default:
      DISP_ERROR("Unrecognized Vout type %u\n", type_);
  }
}

}  // namespace amlogic_display
