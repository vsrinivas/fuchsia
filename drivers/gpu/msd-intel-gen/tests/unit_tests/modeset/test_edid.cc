// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modeset/edid.h"
#include "gtest/gtest.h"

namespace {

BaseEdid GetExampleEdid()
{
    // This data covers the EDID fields that we test below.  Other fields
    // are left as zero.
    static const uint8_t kExampleEdid[] =
        "\x00\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x28\x3c\x80\xa0\x70\xb0\x23\x40\x30\x20"
        "\x36\x00\x07\x44\x21\x00\x00\x1a\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf3";

    BaseEdid edid;
    memcpy(&edid, kExampleEdid, sizeof(edid));
    return edid;
}

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
