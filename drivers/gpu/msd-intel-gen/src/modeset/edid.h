// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MODESET_EDID_H
#define MODESET_EDID_H

#include "register_bitfields.h"

// Definitions for parsing EDID data.

// EDID 18-byte detailed timing descriptor.
//
// Many of the parameters in the timing descriptor are split across
// multiple fields, so we define various accessors for reading them.
//
// See "Table 3.21 - Detailed Timing Definition - Part 1" (in Release
// A, Revision 2 of the EDID spec, 2006).
struct EdidTimingDesc {
    uint32_t horizontal_addressable()
    {
        return horizontal_addressable_low | (horizontal_addressable_high().get() << 8);
    }
    uint32_t horizontal_blanking()
    {
        return horizontal_blanking_low | (horizontal_blanking_high().get() << 8);
    }
    uint32_t vertical_addressable()
    {
        return vertical_addressable_low | (vertical_addressable_high().get() << 8);
    }
    uint32_t vertical_blanking()
    {
        return vertical_blanking_low | (vertical_blanking_high().get() << 8);
    }

    uint32_t horizontal_front_porch()
    {
        return horizontal_front_porch_low | (horizontal_front_porch_high().get() << 8);
    }
    uint32_t horizontal_sync_pulse_width()
    {
        return horizontal_sync_pulse_width_low | (horizontal_sync_pulse_width_high().get() << 8);
    }
    uint32_t vertical_front_porch()
    {
        return vertical_front_porch_low().get() | (vertical_front_porch_high().get() << 4);
    }
    uint32_t vertical_sync_pulse_width()
    {
        return vertical_sync_pulse_width_low().get() |
               (vertical_sync_pulse_width_high().get() << 4);
    }

    // Offset 0
    uint16_t pixel_clock_10khz;
    // Offset 2
    uint8_t horizontal_addressable_low;
    uint8_t horizontal_blanking_low;
    uint8_t horizontal_fields1;
    DEF_SUBFIELD(horizontal_fields1, 7, 4, horizontal_addressable_high);
    DEF_SUBFIELD(horizontal_fields1, 3, 0, horizontal_blanking_high);
    // Offset 5
    uint8_t vertical_addressable_low;
    uint8_t vertical_blanking_low;
    uint8_t vertical_fields1;
    DEF_SUBFIELD(vertical_fields1, 7, 4, vertical_addressable_high);
    DEF_SUBFIELD(vertical_fields1, 3, 0, vertical_blanking_high);
    // Offset 8
    uint8_t horizontal_front_porch_low;
    uint8_t horizontal_sync_pulse_width_low;
    // Offset 10
    uint8_t vertical_fields2;
    DEF_SUBFIELD(vertical_fields2, 7, 4, vertical_front_porch_low);
    DEF_SUBFIELD(vertical_fields2, 3, 0, vertical_sync_pulse_width_low);
    // Offset 11
    uint8_t combined;
    DEF_SUBFIELD(combined, 7, 6, horizontal_front_porch_high);
    DEF_SUBFIELD(combined, 5, 4, horizontal_sync_pulse_width_high);
    DEF_SUBFIELD(combined, 3, 2, vertical_front_porch_high);
    DEF_SUBFIELD(combined, 1, 0, vertical_sync_pulse_width_high);
    uint8_t rest[6]; // Fields that we don't need to read yet.
};

static_assert(sizeof(EdidTimingDesc) == 18, "Size check for EdidTimingDesc");

// This covers the "base" EDID data -- the first 128 bytes (block 0).  In
// many cases, that is all the display provides, but there may be more data
// in extension blocks.
//
// See "Table 3.1 - EDID Structure Version 1, Revision 4" (in Release
// A, Revision 2 of the EDID spec, 2006).
struct BaseEdid {
    bool valid_header();
    bool valid_checksum();

    // Offset 0
    uint8_t header[8];
    uint8_t various[46]; // Fields that we don't need to read yet.
    // Offset 0x36
    EdidTimingDesc preferred_timing;
    uint8_t rest[128 - 0x36 - 18 - 1]; // Fields that we don't need to read yet.
    uint8_t checksum_byte;
};

static_assert(sizeof(BaseEdid) == 128, "Size check for Edid struct");
static_assert(offsetof(BaseEdid, preferred_timing) == 0x36, "Layout check");

#endif // MODESET_EDID_H
