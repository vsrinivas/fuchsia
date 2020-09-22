// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>

bool operator==(const wlan_channel_t& lhs, const wlan_channel_t& rhs) {
  // TODO(porce): Support 802.11ac Wave2 by lhs.secondary80 == rhs.secondary80
  return (lhs.primary == rhs.primary && lhs.cbw == rhs.cbw);
}

bool operator!=(const wlan_channel_t& lhs, const wlan_channel_t& rhs) { return !(lhs == rhs); }

// TODO(porce): Look up constants from the operating class table.
// No need to use constexpr in this prototype.
namespace wlan {
namespace common {

namespace wlan_common = ::fuchsia::wlan::common;

bool Is5Ghz(uint8_t channel_number) {
  // TODO(porce): Improve this humble function
  return (channel_number > 14);
}

bool Is2Ghz(uint8_t channel_number) { return !Is5Ghz(channel_number); }

bool Is5Ghz(const wlan_channel_t& chan) { return Is5Ghz(chan.primary); }

bool Is2Ghz(const wlan_channel_t& chan) { return !Is5Ghz(chan.primary); }

bool IsValidChan2Ghz(const wlan_channel_t& chan) {
  uint8_t p = chan.primary;

  if (p < 1 || p > 14) {
    return false;
  }

  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      return true;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      return (p <= 7);
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      return (p >= 5);
    default:
      return false;
  }
}

bool IsValidChan5Ghz(const wlan_channel_t& chan) {
  uint8_t p = chan.primary;
  uint8_t s = chan.secondary80;

  // See IEEE Std 802.11-2016, Table 9-252, 9-253
  // TODO(porce): Augment wlan_channel_t to carry
  // "channel width" subfield of VHT Operation Info of VHT Operation IE.
  // Test the validity of CCFS1, and the relation to the CCFS0.

  if (p < 36 || p > 173) {
    return false;
  }
  if (p > 64 && p < 100) {
    return false;
  }
  if (p > 144 && p < 149) {
    return false;
  }
  if (p <= 144 && (p % 4 != 0)) {
    return false;
  }
  if (p >= 149 && (p % 4 != 1)) {
    return false;
  }

  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      break;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      if (p <= 144 && (p % 8 != 4)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 5)) {
        return false;
      }
      break;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      if (p <= 144 && (p % 8 != 0)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 1)) {
        return false;
      }
      break;
    case WLAN_CHANNEL_BANDWIDTH__80:
      if (p == 165) {
        return false;
      }
      break;
    case WLAN_CHANNEL_BANDWIDTH__80P80: {
      if (!(s == 42 || s == 58 || s == 106 || s == 122 || s == 138 || s == 155)) {
        return false;
      }

      uint8_t ccfs0 = GetCenterChanIdx(chan);
      uint8_t ccfs1 = s;
      uint8_t gap = (ccfs0 >= ccfs1) ? (ccfs0 - ccfs1) : (ccfs1 - ccfs0);
      if (gap <= 16) {
        return false;
      }
      break;
    }
    case WLAN_CHANNEL_BANDWIDTH__160: {
      if (p >= 132) {
        return false;
      }
      break;
    }
    default:
      return false;
  }

  return true;
}

bool IsValidChan(const wlan_channel_t& chan) {
  auto result = Is2Ghz(chan) ? IsValidChan2Ghz(chan) : IsValidChan5Ghz(chan);

  // TODO(porce): Revisit if wlan library may have active logging
  // Prefer logging in the caller only
  if (!result) {
    errorf("invalid channel value: %s\n", ChanStr(chan).c_str());
  }
  return result;
}

Mhz GetCenterFreq(const wlan_channel_t& chan) {
  ZX_DEBUG_ASSERT(IsValidChan(chan));

  Mhz spacing = 5;
  Mhz channel_starting_frequency;
  if (Is2Ghz(chan)) {
    channel_starting_frequency = kBaseFreq2Ghz;
  } else {
    // 5 GHz
    channel_starting_frequency = kBaseFreq5Ghz;
  }

  // IEEE Std 802.11-2016, 21.3.14
  return channel_starting_frequency + spacing * GetCenterChanIdx(chan);
}

// Returns the channel index corresponding to the first frequency segment's
// center frequency
uint8_t GetCenterChanIdx(const wlan_channel_t& chan) {
  uint8_t p = chan.primary;
  switch (chan.cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      return p;
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      return p + 2;
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      return p - 2;
    case WLAN_CHANNEL_BANDWIDTH__80:
    case WLAN_CHANNEL_BANDWIDTH__80P80:
      if (p <= 48) {
        return 42;
      } else if (p <= 64) {
        return 58;
      } else if (p <= 112) {
        return 106;
      } else if (p <= 128) {
        return 122;
      } else if (p <= 144) {
        return 138;
      } else if (p <= 161) {
        return 155;
      } else {
        // Not reachable
        return p;
      }
    case WLAN_CHANNEL_BANDWIDTH__160:
      // See IEEE Std 802.11-2016 Table 9-252 and 9-253.
      // Note CBW160 has only one frequency segment, regardless of
      // encodings on CCFS0 and CCFS1 in VHT Operation Information IE.
      if (p <= 64) {
        return 50;
      } else if (p <= 128) {
        return 114;
      } else {
        // Not reachable
        return p;
      }
    default:
      return chan.primary;
  }
}

const char* CbwSuffix(wlan_channel_bandwidth_t cbw) {
  switch (cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      return "";  // Vanilla plain 20 MHz bandwidth
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      return "+";  // SCA, often denoted by "+1"
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      return "-";  // SCB, often denoted by "-1"
    case WLAN_CHANNEL_BANDWIDTH__80:
      return "V";  // VHT 80 MHz
    case WLAN_CHANNEL_BANDWIDTH__160:
      return "W";  // VHT Wave2 160 MHz
    case WLAN_CHANNEL_BANDWIDTH__80P80:
      return "P";  // VHT Wave2 80Plus80 (not often obvious, but P is the first
                   // alphabet)
    default:
      return "?";  // Invalid
  }
}

const char* CbwStr(wlan_channel_bandwidth_t cbw) {
  switch (cbw) {
    case WLAN_CHANNEL_BANDWIDTH__20:
      return "CBW20";
    case WLAN_CHANNEL_BANDWIDTH__40ABOVE:
      return "CBW40";
    case WLAN_CHANNEL_BANDWIDTH__40BELOW:
      return "CBW40B";
    case WLAN_CHANNEL_BANDWIDTH__80:
      return "CBW80";
    case WLAN_CHANNEL_BANDWIDTH__160:
      return "CBW160";
    case WLAN_CHANNEL_BANDWIDTH__80P80:
      return "CBW80P80";
    default:
      return "Invalid";
  }
}

std::string ChanStr(const wlan_channel_t& chan) {
  char buf[8 + 1];
  if (chan.cbw != WLAN_CHANNEL_BANDWIDTH__80P80) {
    std::snprintf(buf, sizeof(buf), "%u%s", chan.primary, CbwSuffix(chan.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u%s", chan.primary, chan.secondary80, CbwSuffix(chan.cbw));
  }
  return std::string(buf);
}

std::string ChanStrLong(const wlan_channel_t& chan) {
  char buf[16 + 1];
  if (chan.cbw != WLAN_CHANNEL_BANDWIDTH__80P80) {
    std::snprintf(buf, sizeof(buf), "%u %s", chan.primary, CbwStr(chan.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u %s", chan.primary, chan.secondary80, CbwStr(chan.cbw));
  }
  return std::string(buf);
}

wlan_channel_t FromFidl(const wlan_common::WlanChan& fidl_chan) {
  // Translate wlan::WlanChan class defined in wlan-mlme.fidl
  // to wlan_channel_t struct defined in wlan.h
  return wlan_channel_t{
      .primary = fidl_chan.primary,
      .cbw = static_cast<uint8_t>(fidl_chan.cbw),
      .secondary80 = fidl_chan.secondary80,
  };
}

wlan_common::WlanChan ToFidl(const wlan_channel_t& chan) {
  return wlan_common::WlanChan{
      .primary = chan.primary,
      .cbw = static_cast<wlan_common::CBW>(chan.cbw),
      .secondary80 = chan.secondary80,
  };
}

std::string GetPhyStr(wlan_info_phy_type_t phy) {
  switch (phy) {
    case WLAN_INFO_PHY_TYPE_DSSS:
      return "802.11 DSSS";
    case WLAN_INFO_PHY_TYPE_CCK:
      return "802.11b CCK/DSSS";
    case WLAN_INFO_PHY_TYPE_OFDM:  // and WLAN_INFO_PHY_TYPE_ERP
      return "802.11a/g OFDM";
    case WLAN_INFO_PHY_TYPE_HT:
      return "802.11n HT";
    case WLAN_INFO_PHY_TYPE_VHT:
      return "802.11ac VHT";
    default:
      return "UNKNOWN_PHY";
  }
}

wlan_info_phy_type_t FromFidl(::fuchsia::wlan::common::PHY phy) {
  // TODO(fxbug.dev/29293): Streamline the enum values
  switch (phy) {
    case wlan_common::PHY::HR:
      return WLAN_INFO_PHY_TYPE_CCK;
    case wlan_common::PHY::ERP:
      return WLAN_INFO_PHY_TYPE_OFDM;
    case wlan_common::PHY::HT:
      return WLAN_INFO_PHY_TYPE_HT;
    case wlan_common::PHY::VHT:
      return WLAN_INFO_PHY_TYPE_VHT;
    case wlan_common::PHY::HEW:
      return WLAN_INFO_PHY_TYPE_HEW;
    default:
      errorf("Unknown phy value: %d\n", phy);
      ZX_DEBUG_ASSERT(false);
      return WLAN_INFO_PHY_TYPE_HEW;
  }
}

::fuchsia::wlan::common::PHY ToFidl(wlan_info_phy_type_t phy) {
  // TODO(fxbug.dev/29293): Streamline the enum values
  switch (phy) {
    case WLAN_INFO_PHY_TYPE_CCK:
      return wlan_common::PHY::HR;
    case WLAN_INFO_PHY_TYPE_OFDM:
      return wlan_common::PHY::ERP;
    case WLAN_INFO_PHY_TYPE_HT:
      return wlan_common::PHY::HT;
    case WLAN_INFO_PHY_TYPE_VHT:
      return wlan_common::PHY::VHT;
    case WLAN_INFO_PHY_TYPE_HEW:
      return wlan_common::PHY::HEW;
    default:
      errorf("Unknown phy value: %d\n", phy);
      ZX_DEBUG_ASSERT(false);
      return wlan_common::PHY::HEW;
  }
}

}  // namespace common
}  // namespace wlan
