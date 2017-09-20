// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "example_edid.h"
#include "modeset/edid.h"
#include "gtest/gtest.h"

namespace {

TEST(Edid, EdidParsing)
{
    BaseEdid edid = GetExampleEdid();

    EXPECT_TRUE(edid.valid_header());
    EXPECT_TRUE(edid.valid_checksum());

    EdidTimingDesc* timing = &edid.preferred_timing;

    EXPECT_EQ(timing->pixel_clock_10khz, 15400u);

    EXPECT_EQ(timing->horizontal_addressable(), 1920u);
    EXPECT_EQ(timing->horizontal_blanking(), 160u);
    EXPECT_EQ(timing->horizontal_front_porch(), 48u);
    EXPECT_EQ(timing->horizontal_sync_pulse_width(), 32u);

    EXPECT_EQ(timing->vertical_addressable(), 1200u);
    EXPECT_EQ(timing->vertical_blanking(), 35u);
    EXPECT_EQ(timing->vertical_front_porch(), 3u);
    EXPECT_EQ(timing->vertical_sync_pulse_width(), 6u);
}
}
