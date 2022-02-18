// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_

#include <fuchsia/hardware/wlan/associnfo/c/banjo.h>
#include <fuchsia/hardware/wlan/phyinfo/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>

#include <cstdint>
#include <string>

bool operator==(const wlan_channel_t& lhs, const wlan_channel_t& rhs);
bool operator!=(const wlan_channel_t& lhs, const wlan_channel_t& rhs);

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
bool Is5Ghz(const wlan_channel_t& channel);
bool Is2Ghz(const wlan_channel_t& channel);

bool IsValidChan2Ghz(const wlan_channel_t& channel);
bool IsValidChan5Ghz(const wlan_channel_t& channel);
bool IsValidChan(const wlan_channel_t& channel);

Mhz GetCenterFreq(const wlan_channel_t& channel);
uint8_t GetCenterChanIdx(const wlan_channel_t& channel);

std::string ChanStr(const wlan_channel_t& channel);
std::string ChanStrLong(const wlan_channel_t& channel);

struct Channel {
  wlan_channel_t channel;
  // TODO(porce): Validation
  // TODO(porce): Notation string.
  // TODO(porce): Center frequencies.
  // Define the rule to translsate center frequency to/from channel numbering.
  // See IEEE Std 802.11-2016 19.3.15
};

wlan_channel_t FromFidl(const ::fuchsia::wlan::common::WlanChannel& fidl_channel);
::fuchsia::wlan::common::WlanChannel ToFidl(const wlan_channel_t& channel);

const char* CbwSuffix(channel_bandwidth_t cbw);
const char* CbwStr(channel_bandwidth_t cbw);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_CHANNEL_H_
