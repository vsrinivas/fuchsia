// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <zircon/assert.h>

namespace wlan {
namespace common {

namespace wlan_common = ::fuchsia::wlan::common;

Band GetBand(const wlan_channel_t& chan) {
  return Is2Ghz(chan) ? WLAN_BAND_2GHZ : WLAN_BAND_5GHZ;
}

std::string BandStr(uint8_t band) {
  if (band > WLAN_BAND_COUNT) {
    band = WLAN_BAND_COUNT;
  }
  switch (band) {
    case WLAN_BAND_2GHZ:
      return "2 GHz";
    case WLAN_BAND_5GHZ:
      return "5 GHz";
    default:
      return "BAND_INV";
  }
}

std::string BandStr(Band band) { return BandStr(static_cast<uint8_t>(band)); }

std::string BandStr(const wlan_channel_t& chan) {
  return BandStr(GetBand(chan));
}

wlan_common::Band BandToFidl(uint8_t band) {
  return BandToFidl(static_cast<Band>(band));
}

wlan_common::Band BandToFidl(Band band) {
  switch (band) {
    case WLAN_BAND_2GHZ:
      return wlan_common::Band::WLAN_BAND_2GHZ;
    case WLAN_BAND_5GHZ:
      return wlan_common::Band::WLAN_BAND_5GHZ;
    default:
      return wlan_common::Band::WLAN_BAND_COUNT;
  }
}

Band BandFromFidl(wlan_common::Band band) {
  switch (band) {
    case wlan_common::Band::WLAN_BAND_2GHZ:
      return WLAN_BAND_2GHZ;
    case wlan_common::Band::WLAN_BAND_5GHZ:
      return WLAN_BAND_5GHZ;
    default:
      return WLAN_BAND_COUNT;
  }
}

}  // namespace common
}  // namespace wlan
