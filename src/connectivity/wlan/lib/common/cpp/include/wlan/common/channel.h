// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_

#include <fuchsia/wlan/common/cpp/fidl.h>

#include <cstdint>
#include <string>

#include "fidl/fuchsia.wlan.common/cpp/wire_types.h"

bool operator==(const fuchsia_wlan_common::wire::WlanChannel& lhs,
                const fuchsia_wlan_common::wire::WlanChannel& rhs);
bool operator!=(const fuchsia_wlan_common::wire::WlanChannel& lhs,
                const fuchsia_wlan_common::wire::WlanChannel& rhs);

namespace wlan {
namespace common {

typedef uint16_t Mhz;

// IEEE Std 802.11-2016, Annex E
// Note the distinction of index for primary20 and index for center frequency.
// Fuchsia OS minimizes the use of the notion of center frequency,
// with following exceptions:
// - CBW80P80's secondary frequency segment
// - Frequency conversion at device drivers
constexpr Mhz kBaseFreq2Ghz = 2407;
constexpr Mhz kBaseFreq5Ghz = 5000;

// TODO(porce): Replace all channel > 14 test throughout the codes
bool Is5Ghz(uint8_t channel_number);
bool Is2Ghz(uint8_t channel_number);
bool Is5Ghz(const fuchsia_wlan_common::wire::WlanChannel& channel);
bool Is2Ghz(const fuchsia_wlan_common::wire::WlanChannel& channel);

bool IsValidChan2Ghz(const fuchsia_wlan_common::wire::WlanChannel& channel);
bool IsValidChan5Ghz(const fuchsia_wlan_common::wire::WlanChannel& channel);
bool IsValidChan(const fuchsia_wlan_common::wire::WlanChannel& channel);

Mhz GetCenterFreq(const fuchsia_wlan_common::wire::WlanChannel& channel);
uint8_t GetCenterChanIdx(const fuchsia_wlan_common::wire::WlanChannel& channel);

std::string ChanStr(const fuchsia_wlan_common::wire::WlanChannel& channel);
std::string ChanStrLong(const fuchsia_wlan_common::wire::WlanChannel& channel);

struct Channel {
  fuchsia_wlan_common::wire::WlanChannel channel;
  // TODO(porce): Validation
  // TODO(porce): Notation string.
  // TODO(porce): Center frequencies.
  // Define the rule to translsate center frequency to/from channel numbering.
  // See IEEE Std 802.11-2016 19.3.15
};

const char* CbwSuffix(fuchsia_wlan_common::wire::ChannelBandwidth cbw);
const char* CbwStr(fuchsia_wlan_common::wire::ChannelBandwidth cbw);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_
