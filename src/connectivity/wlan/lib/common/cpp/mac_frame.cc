// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <wlan/common/element_splitter.h>
#include <wlan/common/mac_frame.h>
#include <wlan/protocol/mac.h>

namespace wlan {

CapabilityInfo IntersectCapInfo(const CapabilityInfo& lhs, const CapabilityInfo& rhs) {
  auto cap_info = CapabilityInfo{};

  cap_info.set_ess(lhs.ess() & rhs.ess());
  cap_info.set_ibss(lhs.ibss() & rhs.ibss());
  cap_info.set_cf_pollable(lhs.cf_pollable() & rhs.cf_pollable());
  cap_info.set_cf_poll_req(lhs.cf_poll_req() & rhs.cf_poll_req());
  cap_info.set_privacy(lhs.privacy() & rhs.privacy());
  cap_info.set_short_preamble(lhs.short_preamble() & rhs.short_preamble());
  cap_info.set_spectrum_mgmt(lhs.spectrum_mgmt() & rhs.spectrum_mgmt());
  cap_info.set_qos(lhs.qos() & rhs.qos());
  // TODO(fxbug.dev/29404): Revisit short slot time when necessary.
  // IEEE 802.11-2016 11.1.3.2 and 9.4.1.4
  cap_info.set_short_slot_time(lhs.short_slot_time() & rhs.short_slot_time());
  cap_info.set_apsd(lhs.apsd() & rhs.apsd());
  cap_info.set_radio_msmt(lhs.radio_msmt() & rhs.radio_msmt());
  cap_info.set_delayed_block_ack(lhs.delayed_block_ack() & rhs.delayed_block_ack());
  cap_info.set_immediate_block_ack(lhs.immediate_block_ack() & rhs.immediate_block_ack());

  return cap_info;
}

}  // namespace wlan
