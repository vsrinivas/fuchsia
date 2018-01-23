// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/common/bitfield.h>
#include <zircon/compiler.h>

#include <stdint.h>

// TODO(hahnr): Rather than making each vendor define frame headers, we should extract Fuchsia's
// definitions from wlan/mac_frame.h into a shared library under common/.
namespace ralink {

using wlan::common::BitField;

// IEEE Std 802.11-2016, 9.2.4.1.1
class FrameControl : public BitField<uint16_t> {
 public:
  constexpr FrameControl() = default;

  WLAN_BIT_FIELD(protocol_version, 0, 2);
  WLAN_BIT_FIELD(type, 2, 2);
  WLAN_BIT_FIELD(subtype, 4, 4);
  WLAN_BIT_FIELD(to_ds, 8, 1);
  WLAN_BIT_FIELD(from_ds, 9, 1);
  WLAN_BIT_FIELD(more_frag, 10, 1);
  WLAN_BIT_FIELD(retry, 11, 1);
  WLAN_BIT_FIELD(pwr_mgmt, 12, 1);
  WLAN_BIT_FIELD(more_data, 13, 1);
  WLAN_BIT_FIELD(protected_frame, 14, 1);
  WLAN_BIT_FIELD(htc_order, 15, 1);

  bool IsMgmt() const { return type() == 0x00; }
  bool IsCtrl() const { return type() == 0x01; }
  bool IsData() const { return type() == 0x02; }
};

// IEEE Std 802.11-2016, 9.2.3
struct FrameHeader {
  // Compatible with management and data frames.
  // Incompatible with control frames.
  FrameControl fc;
  uint16_t duration;
  uint8_t addr1[6];
  uint8_t addr2[6];
  uint8_t addr3[6];
  uint16_t sc;
} __PACKED;

}  // namespace ralink
