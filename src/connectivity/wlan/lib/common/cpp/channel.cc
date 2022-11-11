// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <wlan/common/channel.h>
#include <wlan/common/logging.h>

#include "fidl/fuchsia.wlan.common/cpp/wire_types.h"

namespace wlan_common_wire = ::fuchsia_wlan_common::wire;

bool operator==(const wlan_common_wire::WlanChannel& lhs,
                const wlan_common_wire::WlanChannel& rhs) {
  // TODO(porce): Support 802.11ac Wave2 by lhs.secondary80 == rhs.secondary80
  return (lhs.primary == rhs.primary && lhs.cbw == rhs.cbw);
}

bool operator!=(const wlan_common_wire::WlanChannel& lhs,
                const wlan_common_wire::WlanChannel& rhs) {
  return !(lhs == rhs);
}

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

bool Is5Ghz(const wlan_common_wire::WlanChannel& channel) { return Is5Ghz(channel.primary); }

bool Is2Ghz(const wlan_common_wire::WlanChannel& channel) { return !Is5Ghz(channel.primary); }

bool IsValidChan2Ghz(const wlan_common_wire::WlanChannel& channel) {
  uint8_t p = channel.primary;

  if (p < 1 || p > 14) {
    return false;
  }

  switch (channel.cbw) {
    case wlan_common_wire::ChannelBandwidth::kCbw20:
      return true;
    case wlan_common_wire::ChannelBandwidth::kCbw40:
      return (p <= 7);
    case wlan_common_wire::ChannelBandwidth::kCbw40Below:
      return (p >= 5);
    default:
      return false;
  }
}

bool IsValidChan5Ghz(const wlan_common_wire::WlanChannel& channel) {
  uint8_t p = channel.primary;
  uint8_t s = channel.secondary80;

  // See IEEE Std 802.11-2016, Table 9-252, 9-253
  // TODO(porce): Augment wlan_common_wire::WlanChannel to carry
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

  switch (channel.cbw) {
    case wlan_common_wire::ChannelBandwidth::kCbw20:
      break;
    case wlan_common_wire::ChannelBandwidth::kCbw40:
      if (p <= 144 && (p % 8 != 4)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 5)) {
        return false;
      }
      break;
    case wlan_common_wire::ChannelBandwidth::kCbw40Below:
      if (p <= 144 && (p % 8 != 0)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 1)) {
        return false;
      }
      break;
    case wlan_common_wire::ChannelBandwidth::kCbw80:
      if (p == 165) {
        return false;
      }
      break;
    case wlan_common_wire::ChannelBandwidth::kCbw80P80: {
      if (!(s == 42 || s == 58 || s == 106 || s == 122 || s == 138 || s == 155)) {
        return false;
      }

      uint8_t ccfs0 = GetCenterChanIdx(channel);
      uint8_t ccfs1 = s;
      uint8_t gap = (ccfs0 >= ccfs1) ? (ccfs0 - ccfs1) : (ccfs1 - ccfs0);
      if (gap <= 16) {
        return false;
      }
      break;
    }
    case wlan_common_wire::ChannelBandwidth::kCbw160: {
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

bool IsValidChan(const wlan_common_wire::WlanChannel& channel) {
  return Is2Ghz(channel) ? IsValidChan2Ghz(channel) : IsValidChan5Ghz(channel);
}

Mhz GetCenterFreq(const wlan_common_wire::WlanChannel& channel) {
  ZX_DEBUG_ASSERT(IsValidChan(channel));

  Mhz spacing = 5;
  Mhz channel_starting_frequency;
  if (Is2Ghz(channel)) {
    channel_starting_frequency = kBaseFreq2Ghz;
  } else {
    // 5 GHz
    channel_starting_frequency = kBaseFreq5Ghz;
  }

  // IEEE Std 802.11-2016, 21.3.14
  return channel_starting_frequency + spacing * GetCenterChanIdx(channel);
}

// Returns the channel index corresponding to the first frequency segment's
// center frequency
uint8_t GetCenterChanIdx(const wlan_common_wire::WlanChannel& channel) {
  uint8_t p = channel.primary;
  switch (channel.cbw) {
    case wlan_common_wire::ChannelBandwidth::kCbw20:
      return p;
    case wlan_common_wire::ChannelBandwidth::kCbw40:
      return p + 2;
    case wlan_common_wire::ChannelBandwidth::kCbw40Below:
      return p - 2;
    case wlan_common_wire::ChannelBandwidth::kCbw80:
    case wlan_common_wire::ChannelBandwidth::kCbw80P80:
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
    case wlan_common_wire::ChannelBandwidth::kCbw160:
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
      return channel.primary;
  }
}

const char* CbwSuffix(wlan_common_wire::ChannelBandwidth cbw) {
  switch (cbw) {
    case wlan_common_wire::ChannelBandwidth::kCbw20:
      return "";  // Vanilla plain 20 MHz bandwidth
    case wlan_common_wire::ChannelBandwidth::kCbw40:
      return "+";  // SCA, often denoted by "+1"
    case wlan_common_wire::ChannelBandwidth::kCbw40Below:
      return "-";  // SCB, often denoted by "-1"
    case wlan_common_wire::ChannelBandwidth::kCbw80:
      return "V";  // VHT 80 MHz
    case wlan_common_wire::ChannelBandwidth::kCbw160:
      return "W";  // VHT Wave2 160 MHz
    case wlan_common_wire::ChannelBandwidth::kCbw80P80:
      return "P";  // VHT Wave2 80Plus80 (not often obvious, but P is the first
                   // alphabet)
    default:
      return "?";  // Invalid
  }
}

const char* CbwStr(wlan_common_wire::ChannelBandwidth cbw) {
  switch (cbw) {
    case wlan_common_wire::ChannelBandwidth::kCbw20:
      return "CBW20";
    case wlan_common_wire::ChannelBandwidth::kCbw40:
      return "CBW40";
    case wlan_common_wire::ChannelBandwidth::kCbw40Below:
      return "CBW40B";
    case wlan_common_wire::ChannelBandwidth::kCbw80:
      return "CBW80";
    case wlan_common_wire::ChannelBandwidth::kCbw160:
      return "CBW160";
    case wlan_common_wire::ChannelBandwidth::kCbw80P80:
      return "CBW80P80";
    default:
      return "Invalid";
  }
}

std::string ChanStr(const wlan_common_wire::WlanChannel& channel) {
  char buf[8 + 1];
  if (channel.cbw != wlan_common_wire::ChannelBandwidth::kCbw80P80) {
    std::snprintf(buf, sizeof(buf), "%u%s", channel.primary, CbwSuffix(channel.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u%s", channel.primary, channel.secondary80,
                  CbwSuffix(channel.cbw));
  }
  return std::string(buf);
}

std::string ChanStrLong(const wlan_common_wire::WlanChannel& channel) {
  char buf[16 + 1];
  if (channel.cbw != wlan_common_wire::ChannelBandwidth::kCbw80P80) {
    std::snprintf(buf, sizeof(buf), "%u %s", channel.primary, CbwStr(channel.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u %s", channel.primary, channel.secondary80,
                  CbwStr(channel.cbw));
  }
  return std::string(buf);
}

}  // namespace common
}  // namespace wlan
