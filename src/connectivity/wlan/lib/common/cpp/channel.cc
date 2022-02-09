// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/wlan/common/c/banjo.h>
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

bool Is5Ghz(const wlan_channel_t& channel) { return Is5Ghz(channel.primary); }

bool Is2Ghz(const wlan_channel_t& channel) { return !Is5Ghz(channel.primary); }

bool IsValidChan2Ghz(const wlan_channel_t& channel) {
  uint8_t p = channel.primary;

  if (p < 1 || p > 14) {
    return false;
  }

  switch (channel.cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      return true;
    case CHANNEL_BANDWIDTH_CBW40:
      return (p <= 7);
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      return (p >= 5);
    default:
      return false;
  }
}

bool IsValidChan5Ghz(const wlan_channel_t& channel) {
  uint8_t p = channel.primary;
  uint8_t s = channel.secondary80;

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

  switch (channel.cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      break;
    case CHANNEL_BANDWIDTH_CBW40:
      if (p <= 144 && (p % 8 != 4)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 5)) {
        return false;
      }
      break;
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      if (p <= 144 && (p % 8 != 0)) {
        return false;
      }
      if (p >= 149 && (p % 8 != 1)) {
        return false;
      }
      break;
    case CHANNEL_BANDWIDTH_CBW80:
      if (p == 165) {
        return false;
      }
      break;
    case CHANNEL_BANDWIDTH_CBW80P80: {
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
    case CHANNEL_BANDWIDTH_CBW160: {
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

bool IsValidChan(const wlan_channel_t& channel) {
  auto result = Is2Ghz(channel) ? IsValidChan2Ghz(channel) : IsValidChan5Ghz(channel);

  // TODO(porce): Revisit if wlan library may have active logging
  // Prefer logging in the caller only
  if (!result) {
    errorf("invalid channel value: %s\n", ChanStr(channel).c_str());
  }
  return result;
}

Mhz GetCenterFreq(const wlan_channel_t& channel) {
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
uint8_t GetCenterChanIdx(const wlan_channel_t& channel) {
  uint8_t p = channel.primary;
  switch (channel.cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      return p;
    case CHANNEL_BANDWIDTH_CBW40:
      return p + 2;
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      return p - 2;
    case CHANNEL_BANDWIDTH_CBW80:
    case CHANNEL_BANDWIDTH_CBW80P80:
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
    case CHANNEL_BANDWIDTH_CBW160:
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

const char* CbwSuffix(channel_bandwidth_t cbw) {
  switch (cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      return "";  // Vanilla plain 20 MHz bandwidth
    case CHANNEL_BANDWIDTH_CBW40:
      return "+";  // SCA, often denoted by "+1"
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      return "-";  // SCB, often denoted by "-1"
    case CHANNEL_BANDWIDTH_CBW80:
      return "V";  // VHT 80 MHz
    case CHANNEL_BANDWIDTH_CBW160:
      return "W";  // VHT Wave2 160 MHz
    case CHANNEL_BANDWIDTH_CBW80P80:
      return "P";  // VHT Wave2 80Plus80 (not often obvious, but P is the first
                   // alphabet)
    default:
      return "?";  // Invalid
  }
}

const char* CbwStr(channel_bandwidth_t cbw) {
  switch (cbw) {
    case CHANNEL_BANDWIDTH_CBW20:
      return "CBW20";
    case CHANNEL_BANDWIDTH_CBW40:
      return "CBW40";
    case CHANNEL_BANDWIDTH_CBW40BELOW:
      return "CBW40B";
    case CHANNEL_BANDWIDTH_CBW80:
      return "CBW80";
    case CHANNEL_BANDWIDTH_CBW160:
      return "CBW160";
    case CHANNEL_BANDWIDTH_CBW80P80:
      return "CBW80P80";
    default:
      return "Invalid";
  }
}

std::string ChanStr(const wlan_channel_t& channel) {
  char buf[8 + 1];
  if (channel.cbw != CHANNEL_BANDWIDTH_CBW80P80) {
    std::snprintf(buf, sizeof(buf), "%u%s", channel.primary, CbwSuffix(channel.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u%s", channel.primary, channel.secondary80,
                  CbwSuffix(channel.cbw));
  }
  return std::string(buf);
}

std::string ChanStrLong(const wlan_channel_t& channel) {
  char buf[16 + 1];
  if (channel.cbw != CHANNEL_BANDWIDTH_CBW80P80) {
    std::snprintf(buf, sizeof(buf), "%u %s", channel.primary, CbwStr(channel.cbw));
  } else {
    std::snprintf(buf, sizeof(buf), "%u+%u %s", channel.primary, channel.secondary80,
                  CbwStr(channel.cbw));
  }
  return std::string(buf);
}

wlan_channel_t FromFidl(const wlan_common::WlanChannel& fidl_channel) {
  // Translate wlan::WlanChan class defined in wlan-mlme.fidl
  // to wlan_channel_t struct defined in wlan.h
  return wlan_channel_t{
      .primary = fidl_channel.primary,
      .cbw = static_cast<uint8_t>(fidl_channel.cbw),
      .secondary80 = fidl_channel.secondary80,
  };
}

wlan_common::WlanChannel ToFidl(const wlan_channel_t& channel) {
  return wlan_common::WlanChannel{
      .primary = channel.primary,
      .cbw = static_cast<wlan_common::ChannelBandwidth>(channel.cbw),
      .secondary80 = channel.secondary80,
  };
}

}  // namespace common
}  // namespace wlan
