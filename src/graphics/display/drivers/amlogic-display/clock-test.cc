// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/amlogic-display/clock.h"

#include <lib/device-protocol/display-panel.h>

#include <zxtest/zxtest.h>

namespace {

const display_setting_t kDisplaySettingsWithoutClockFactor = {
    .lane_num = 4,
    .bit_rate_max = 400,
    .clock_factor = 0,  // auto
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

static display_setting_t display_types[] = {
    kDisplaySettingTV070WSM_FT,
    kDisplaySettingP070ACB_FT,
    kDisplaySettingG101B158_FT,
    kDisplaySettingTV101WXM_FT,
    /*kDisplaySettingIli9881c,*/ /*kDisplaySettingSt7701s,*/
    kDisplaySettingTV080WXM_FT,
    kDisplaySettingKD070D82_FT,
    kDisplaySettingTV070WSM_ST7703I,
    kDisplaySettingsWithoutClockFactor,
};

}  // namespace

// For now, simply test that timing calculations don't segfault.
TEST(AmlogicDisplayClock, PanelTiming) {
  for (const auto t : display_types) {
    amlogic_display::Clock::CalculateLcdTiming(t);
  }
}

TEST(AmlogicDisplayClock, PllTiming_ValidMode) {
  for (const auto t : display_types) {
    auto pll_r = amlogic_display::Clock::GenerateHPLL(t);
    EXPECT_OK(pll_r);
  }
}
