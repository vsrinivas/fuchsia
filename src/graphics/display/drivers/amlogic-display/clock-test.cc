// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "clock.h"

#include <lib/device-protocol/display-panel.h>

#include <zxtest/zxtest.h>

static display_setting_t display_types[] = {kDisplaySettingTV070WSM_FT, kDisplaySettingP070ACB_FT,
                                            kDisplaySettingTV101WXM_FT};

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
