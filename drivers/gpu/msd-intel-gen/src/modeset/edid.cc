// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modeset/edid.h"

bool BaseEdid::valid_header()
{
    static const uint8_t kEdidHeader[8] = {0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0};
    return memcmp(header, kEdidHeader, sizeof(kEdidHeader)) == 0;
}

bool BaseEdid::valid_checksum()
{
    // The last byte of the 128-byte EDID data is a checksum byte which
    // should make the 128 bytes sum to zero.
    uint8_t* edid_bytes = reinterpret_cast<uint8_t*>(this);
    uint8_t sum = 0;
    for (uint32_t i = 0; i < sizeof(*this); ++i)
        sum += edid_bytes[i];
    return sum == 0;
}
