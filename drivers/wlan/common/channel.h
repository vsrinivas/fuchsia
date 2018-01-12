// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

#include <ddk/protocol/wlan.h>

namespace wlan {
namespace common {

typedef uint16_t Mhz;

// TODO(porce): Replace all channel > 14 test throughout the codes
bool Is5Ghz(uint8_t channel_number);
bool Is2Ghz(uint8_t channel_number);
bool Is5Ghz(const wlan_channel_t& chan);
bool Is2Ghz(const wlan_channel_t& chan);

// TODO(porce): Implement
// bool IsChanValid(const wlan_channel_t& chan);
Mhz GetCenterFreq(const wlan_channel_t& chan);
uint8_t GetCenterChanIdx(const wlan_channel_t& chan);

std::string ChanStr(const wlan_channel_t& chan);
std::string ChanStrLong(const wlan_channel_t& chan);

struct Channel {
    wlan_channel_t chan;
    // TODO(porce): Validation
    // TODO(porce): Notation string.
    // TODO(porce): Center frequencies.
    // Define the rule to translsate center frequency to/from channel numbering.
    // See IEEE Std 802.11-2016 19.3.15
};

}  // namespace common
}  // namespace wlan
