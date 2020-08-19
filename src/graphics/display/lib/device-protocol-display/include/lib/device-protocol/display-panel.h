// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <zircon/types.h>

#include <ddk/protocol/dsiimpl.h>

// Supported panel types
#define PANEL_TV070WSM_FT UINT8_C(0x00)
#define PANEL_P070ACB_FT UINT8_C(0x01)
#define PANEL_TV101WXM_FT UINT8_C(0x02)
#define PANEL_G101B158_FT UINT8_C(0x03)
#define PANEL_ILI9881C UINT8_C(0x04)
#define PANEL_ST7701S UINT8_C(0x05)
#define PANEL_TV080WXM_FT UINT8_C(0x06)
#define PANEL_UNKNOWN UINT8_C(0xFF)

// Astro/Sherlock Display Configuration. These configuration comes directly from
// from LCD vendor and hardware team.
const display_setting_t kDisplaySettingTV070WSM_FT = {
    .lane_num = 4,
    .bit_rate_max = 360,
    .clock_factor = 8,
    .lcd_clock = 44250000,
    .h_active = 600,
    .v_active = 1024,
    .h_period = 700,
    .v_period = 1053,
    .hsync_width = 24,
    .hsync_bp = 36,
    .hsync_pol = 0,
    .vsync_width = 2,
    .vsync_bp = 8,
    .vsync_pol = 0,
};
const display_setting_t kDisplaySettingP070ACB_FT = {
    .lane_num = 4,
    .bit_rate_max = 400,
    .clock_factor = 8,
    .lcd_clock = 49434000,
    .h_active = 600,
    .v_active = 1024,
    .h_period = 770,
    .v_period = 1070,
    .hsync_width = 10,
    .hsync_bp = 80,
    .hsync_pol = 0,
    .vsync_width = 6,
    .vsync_bp = 20,
    .vsync_pol = 0,
};
const display_setting_t kDisplaySettingG101B158_FT = {
    .lane_num = 4,
    .bit_rate_max = 566,
    .clock_factor = 8,
    .lcd_clock = 70701600,
    .h_active = 800,
    .v_active = 1280,
    .h_period = 890,
    .v_period = 1324,
    .hsync_width = 24,
    .hsync_bp = 20,
    .hsync_pol = 0,
    .vsync_width = 4,
    .vsync_bp = 20,
    .vsync_pol = 0,
};
const display_setting_t kDisplaySettingTV101WXM_FT = {
    .lane_num = 4,
    .bit_rate_max = 566,
    .clock_factor = 8,
    .lcd_clock = 70701600,
    .h_active = 800,
    .v_active = 1280,
    .h_period = 890,
    .v_period = 1324,
    .hsync_width = 20,
    .hsync_bp = 50,
    .hsync_pol = 0,
    .vsync_width = 4,
    .vsync_bp = 20,
    .vsync_pol = 0,
};
const display_setting_t kDisplaySettingIli9881c = {
    .lane_num = 4,
    .bit_rate_max = 0,  // unused
    .clock_factor = 0,  // unused
    .lcd_clock = 270,
    .h_active = 720,
    .v_active = 1280,
    .h_period = 900,   // Vendor provides front porch of 80. calculate period manually
    .v_period = 1340,  // Vendor provides front porch of 40. calculate period manually
    .hsync_width = 20,
    .hsync_bp = 80,
    .hsync_pol = 0,  // unused
    .vsync_width = 4,
    .vsync_bp = 16,
    .vsync_pol = 0,  // unused
};
const display_setting_t kDisplaySettingSt7701s = {
    .lane_num = 2,
    .bit_rate_max = 0,  // unused
    .clock_factor = 0,  // unused
    .lcd_clock = 228,
    .h_active = 480,
    .v_active = 800,
    .h_period = 740,  // Vendor provides front porch of 100. calculate period manually
    .v_period = 848,  // Vendor provides front porch of 20. calculate period manually
    .hsync_width = 60,
    .hsync_bp = 100,
    .hsync_pol = 0,  // unused
    .vsync_width = 8,
    .vsync_bp = 20,
    .vsync_pol = 0,  // unused
};
const display_setting_t kDisplaySettingTV080WXM_FT = {
    .lane_num = 4,
    .bit_rate_max = 498,
    .clock_factor = 6,
    .lcd_clock = 77967360,
    .h_active = 800,
    .v_active = 1280,
    .h_period = 864,
    .v_period = 1504,
    .hsync_width = 14,
    .hsync_bp = 25,
    .hsync_pol = 0,
    .vsync_width = 8,
    .vsync_bp = 32,
    .vsync_pol = 0,
};

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t panel_type;
} display_panel_t;
