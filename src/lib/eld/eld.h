// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_EDID_EDID_H_2
#define LIB_EDID_EDID_H_2

#include <lib/edid/edid.h>

#include <hwreg/bitfields.h>

namespace hda {

// From HDA Specification 1.0a, section 7.3.3.34.1.
// The ELD (EDID Like Data) buffer is composed of 3 blocks, the header, baseline and vendor.
// 1. The header has a fixed length and is defined below.
// 2. The baseline block can be divided into 3 parts, part 1 is defined below.
//    Part 2 is the monitor_name_string extracted from from 16 byte product description of the
//    Source Product Description Info Frame.
//    Part 3 is an array of ShortAudioDescriptors.
// 3. The vendor block is defined bytes starting from:
//    4 + baseline_eld_len * 4 to the ELD buffer size - 1.
struct EldHeader {
  uint32_t header;
  DEF_SUBFIELD(header, 31, 27, eld_ver);
  DEF_SUBFIELD(header, 15, 8, baseline_eld_len);
};

struct EldBaselinePart1 {
  uint8_t byte1;
  DEF_SUBFIELD(byte1, 7, 5, cea_edid_ver);
  DEF_SUBFIELD(byte1, 4, 0, mnl);
  uint8_t byte2;
  DEF_SUBFIELD(byte2, 7, 4, sad_count);
  DEF_SUBFIELD(byte2, 3, 2, conn_type);
  DEF_SUBBIT(byte2, 1, s_ai);
  DEF_SUBBIT(byte2, 0, hdcp);
  uint8_t aud_synch_delay;
  uint8_t byte4;
  DEF_SUBBIT(byte4, 6, rlrc);
  DEF_SUBBIT(byte4, 5, flrc);
  DEF_SUBBIT(byte4, 4, rc);
  DEF_SUBBIT(byte4, 3, rlr);
  DEF_SUBBIT(byte4, 2, fc);
  DEF_SUBBIT(byte4, 1, lfe);
  DEF_SUBBIT(byte4, 0, lfr);
  uint64_t port_id;
  uint16_t manufacturer_name;
  uint16_t product_code;
} __PACKED;

}  // namespace hda

#endif  // LIB_EDID_EDID_H_2
