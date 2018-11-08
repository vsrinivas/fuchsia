// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>

#include <zircon/assert.h>

bool operator==(const wlan_channel_t& lhs, const wlan_channel_t& rhs) {
    // TODO(porce): Support 802.11ac Wave2 by lhs.secondary80 == rhs.secondary80
    return (lhs.primary == rhs.primary && lhs.cbw == rhs.cbw);
}

bool operator!=(const wlan_channel_t& lhs, const wlan_channel_t& rhs) {
    return !(lhs == rhs);
}

// TODO(porce): Look up constants from the operating class table.
// No need to use constexpr in this prototype.
namespace wlan {
namespace common {

namespace wlan_mlme = ::fuchsia::wlan::mlme;

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

bool IsValidChan2Ghz(const wlan_channel_t& chan) {
    uint8_t p = chan.primary;

    if (p < 1 || p > 14) { return false; }

    switch (chan.cbw) {
    case CBW20:
        return true;
    case CBW40ABOVE:
        return (p <= 7);
    case CBW40BELOW:
        return (p >= 5);
    default:
        return false;
    }
}

bool IsValidChan5Ghz(const wlan_channel_t& chan) {
    uint8_t p = chan.primary;

    if (p < 36 || p > 173) { return false; }
    if (p > 64 && p < 100) { return false; }
    if (p > 144 && p < 149) { return false; }
    if (p <= 144 && (p % 4 != 0)) { return false; }
    if (p >= 149 && (p % 4 != 1)) { return false; }

    switch (chan.cbw) {
    case CBW20:
        break;
    case CBW40ABOVE:
        if (p <= 144 && (p % 8 != 4)) { return false; }
        if (p >= 149 && (p % 8 != 5)) { return false; }
        break;
    case CBW40BELOW:
        if (p <= 144 && (p % 8 != 0)) { return false; }
        if (p >= 149 && (p % 8 != 1)) { return false; }
        break;
    default:
        return false;
    }

    return true;
}

bool IsValidChan(const wlan_channel_t& chan) {
    auto result = Is2Ghz(chan) ? IsValidChan2Ghz(chan) : IsValidChan5Ghz(chan);

    // TODO(porce): Revisit if wlan library may have active logging
    // Prefer logging in the caller only
    if (!result) { errorf("invalid channel value: %s\n", ChanStr(chan).c_str()); }
    return result;
}

Mhz GetCenterFreq(const wlan_channel_t& chan) {
    ZX_DEBUG_ASSERT(IsValidChan(chan));

    Mhz spacing = 5;
    Mhz channel_starting_frequency;
    if (Is2Ghz(chan)) {
        channel_starting_frequency = kBaseFreq2Ghz;
    } else {
        // 5Ghz
        channel_starting_frequency = kBaseFreq5Ghz;
    }

    // IEEE Std 802.11-2016, 21.3.14
    return channel_starting_frequency + spacing * chan.primary;
}

uint8_t GetCenterChanIdx(const wlan_channel_t& chan) {
    ZX_DEBUG_ASSERT(IsValidChan(chan));

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

wlan_channel_t FromFidl(const wlan_mlme::WlanChan& fidl_chan) {
    // Translate wlan::WlanChan class defined in wlan-mlme.fidl
    // to wlan_channel_t struct defined in wlan.h
    return wlan_channel_t{
        .primary = fidl_chan.primary,
        .cbw = static_cast<uint8_t>(fidl_chan.cbw),
        .secondary80 = fidl_chan.secondary80,
    };
}

wlan_mlme::WlanChan ToFidl(const wlan_channel_t& chan) {
    return wlan_mlme::WlanChan{
        .primary = chan.primary,
        .cbw = static_cast<wlan_mlme::CBW>(chan.cbw),
        .secondary80 = chan.secondary80,
    };
}

// Sanitizes the user-providing channel value, and always returns a valid one,
// with respect to the primary channel.
// TODO(NET-449): Move this logic to policy engine
uint8_t GetValidCbw(const wlan_channel_t& chan) {
    if (IsValidChan(chan)) { return chan.cbw; }

    wlan_channel_t attempt = {
        .primary = chan.primary,
        .cbw = CBW20,  // To be tested
        //.secondary80 = 0,
    };

    // Search a valid combination in decreasing order of bandwidth.
    // No precedence among CBW40*
    attempt.cbw = CBW40ABOVE;
    if (IsValidChan(attempt)) { return attempt.cbw; }

    attempt.cbw = CBW40BELOW;
    if (IsValidChan(attempt)) { return attempt.cbw; }

    attempt.cbw = CBW20;
    if (IsValidChan(attempt)) { return attempt.cbw; }

    return CBW20;  // Fallback to the minimum bandwidth
}

std::string GetPhyStr(PHY phy) {
    switch (phy) {
    case WLAN_PHY_DSSS:
        return "802.11 DSSS";
    case WLAN_PHY_CCK:
        return "802.11b CCK/DSSS";
    case WLAN_PHY_OFDM:  // and WLAN_PHY_ERP
        return "802.11a/g OFDM";
    case WLAN_PHY_HT:
        return "802.11n HT";
    case WLAN_PHY_VHT:
        return "802.11ac VHT";
    default:
        return "UNKNOWN_PHY";
    }
}

PHY FromFidl(::fuchsia::wlan::mlme::PHY phy) {
    // TODO(NET-1845): Streamline the enum values
    switch (phy) {
    case wlan_mlme::PHY::HR:
        return WLAN_PHY_CCK;
    case wlan_mlme::PHY::ERP:
        return WLAN_PHY_OFDM;
    case wlan_mlme::PHY::HT:
        return WLAN_PHY_HT;
    case wlan_mlme::PHY::VHT:
        return WLAN_PHY_VHT;
    case wlan_mlme::PHY::HEW:
        return WLAN_PHY_HEW;
    default:
        errorf("Unknown phy value: %d\n", phy);
        ZX_DEBUG_ASSERT(false);
        return WLAN_PHY_HEW;
    }
}

::fuchsia::wlan::mlme::PHY ToFidl(PHY phy) {
    // TODO(NET-1845): Streamline the enum values
    switch (phy) {
    case WLAN_PHY_CCK:
        return wlan_mlme::PHY::HR;
    case WLAN_PHY_OFDM:
        return wlan_mlme::PHY::ERP;
    case WLAN_PHY_HT:
        return wlan_mlme::PHY::HT;
    case WLAN_PHY_VHT:
        return wlan_mlme::PHY::VHT;
    case WLAN_PHY_HEW:
        return wlan_mlme::PHY::HEW;
    default:
        errorf("Unknown phy value: %d\n", phy);
        ZX_DEBUG_ASSERT(false);
        return wlan_mlme::PHY::HEW;
    }
}

}  // namespace common
}  // namespace wlan
