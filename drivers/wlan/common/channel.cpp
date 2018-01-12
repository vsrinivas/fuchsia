// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel.h"

#include <garnet/drivers/wlan/wlan/logging.h>

// TODO(porce): Look up constants from the operating class table.
// No need to use constexpr in this prototype.
namespace wlan {
namespace common {

const char* kCbwStr[] = {"CBW20", "CBW40", "CBW40B", "CBW80", "CBW160", "CBW80P80", "CBW_INV"};

const char* kCbwSuffix[] = {
    // Fuchsia's short CBW notation. Not IEEE standard.
    "",   // Vanilla plain 20 MHz bandwidth
    "+",  // SCA, often denoted by "+1"
    "-",  // SCB, often denoted by "-1"
    "V",  // VHT 80 MHz
    "W",  // VHT Wave2 160 MHz
    "P",  // VHT Wave2 80Plus80 (not often obvious, but P is the first alphabet)
    "!",  // Invalid
};

bool Is5Ghz(uint8_t channel_number) {
    // TODO(porce): Improve this humble function
    return (channel_number > 14);
}

bool Is2Ghz(uint8_t channel_number) {
    return !Is5Ghz(channel_number);
}

bool Is5Ghz(const wlan_channel_t& chan) {
    return Is5Ghz(chan.primary);
}

bool Is2Ghz(const wlan_channel_t& chan) {
    return !Is5Ghz(chan.primary);
}

// TODO(porce): Implement
// bool IsChanValid(const wlan_channel_t& chan) { }

Mhz GetCenterFreq(const wlan_channel_t& chan) {
    // ZX_DEBUG_ASSERT(IsChanValid(chan));

    Mhz spacing = 5;
    Mhz channel_starting_frequency;
    if (Is2Ghz(chan)) {
        channel_starting_frequency = 2407;
    } else {
        // 5Ghz
        channel_starting_frequency = 5000;
    }

    // IEEE Std 802.11-2016, 21.3.14
    return channel_starting_frequency + spacing * chan.primary;
}

uint8_t GetCenterChanIdx(const wlan_channel_t& chan) {
    // ZX_DEBUG_ASSERT(IsChanValid(chan));

    switch (chan.cbw) {
    case CBW20:
        return chan.primary;
    case CBW40ABOVE:
        return chan.primary + 2;
    case CBW40BELOW:
        return chan.primary - 2;
    default:
        // TODO(porce): Support CBW80 and beyond
        return chan.primary;
    }
}

std::string ChanStr(const wlan_channel_t& chan) {
    char buf[7 + 1];

    uint8_t cbw = chan.cbw;
    if (cbw >= CBW_COUNT) {
        cbw = CBW_COUNT;  // To be used to indicate invalid value.
    }

    int offset = std::snprintf(buf, sizeof(buf), "%u%s", chan.primary, kCbwSuffix[cbw]);
    if (cbw == CBW80P80) {
        std::snprintf(buf + offset, sizeof(buf) - offset, "%u", chan.secondary80);
    }
    return std::string(buf);
}

std::string ChanStrLong(const wlan_channel_t& chan) {
    char buf[16 + 1];

    uint8_t cbw = chan.cbw;
    if (cbw >= CBW_COUNT) {
        cbw = CBW_COUNT;  // To be used to indicate invalid value;
    }

    int offset = std::snprintf(buf, sizeof(buf), "%u %s", chan.primary, kCbwStr[cbw]);
    if (cbw == CBW80P80) {
        std::snprintf(buf + offset, sizeof(buf) - offset, " %u", chan.secondary80);
    }
    return std::string(buf);
}

}  // namespace common
}  // namespace wlan
