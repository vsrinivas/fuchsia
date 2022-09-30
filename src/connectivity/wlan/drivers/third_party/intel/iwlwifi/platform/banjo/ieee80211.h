// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains copies of banjo definitions that were auto generated from
// fuchsia.wlan.ieee80211. Since banjo is being deprecated, we are making a local copy of defines
// that the driver relies upon. fxbug.dev/104598 is the tracking bug to remove the usage pf
// platforms/banjo/*.h files.

// WARNING: DO NOT ADD MORE DEFINITIONS TO THIS FILE

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_IEEE80211_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_IEEE80211_H_

#include <zircon/types.h>

// IEEE Std 802.11-2016, 9.2.4.5
#define fuchsia_wlan_ieee80211_TIDS_MAX UINT32_C(16)
// The limit on the number of channels in a list of unique channel numbers is 256
// since a channel number in IEEE 802.11-2016 cannot exceed one octet. See
// IEEE 802.11-2016 9.4.2.18 Supported Channels element for an example element
// that assumes a channel number does not exceed one octet.
#define fuchsia_wlan_ieee80211_MAX_UNIQUE_CHANNEL_NUMBERS UINT16_C(256)
// IEEE Std 802.11-2016, 9.4.2.2
// The maximum length of an SSID is 32 bytes, even when the SSID should be
// interpreted using UTF-8 encoding (see Table 9-135). While every length in
// the 802.11 standard is byte oriented, the word BYTE is included in the
// name of this constant to emphasize the importance that it not be applied
// to the length of a UTF-8 encoded string.
#define fuchsia_wlan_ieee80211_MAX_SSID_BYTE_LEN UINT8_C(32)
#define fuchsia_wlan_ieee80211_MAC_ADDR_LEN UINT8_C(6)
typedef struct ht_capabilities ht_capabilities_t;
typedef uint32_t cipher_suite_type_t;
#define CIPHER_SUITE_TYPE_USE_GROUP UINT32_C(0)
#define CIPHER_SUITE_TYPE_WEP_40 UINT32_C(1)
#define CIPHER_SUITE_TYPE_TKIP UINT32_C(2)
#define CIPHER_SUITE_TYPE_RESERVED_3 UINT32_C(3)
#define CIPHER_SUITE_TYPE_CCMP_128 UINT32_C(4)
#define CIPHER_SUITE_TYPE_WEP_104 UINT32_C(5)
#define CIPHER_SUITE_TYPE_BIP_CMAC_128 UINT32_C(6)
#define CIPHER_SUITE_TYPE_GROUP_ADDRESSED_NOT_ALLOWED UINT32_C(7)
#define CIPHER_SUITE_TYPE_GCMP_128 UINT32_C(8)
#define CIPHER_SUITE_TYPE_GCMP_256 UINT32_C(9)
#define CIPHER_SUITE_TYPE_CCMP_256 UINT32_C(10)
#define CIPHER_SUITE_TYPE_BIP_GMAC_128 UINT32_C(11)
#define CIPHER_SUITE_TYPE_BIP_GMAC_256 UINT32_C(12)
#define CIPHER_SUITE_TYPE_BIP_CMAC_256 UINT32_C(13)
#define CIPHER_SUITE_TYPE_RESERVED_14_TO_255 UINT32_C(14)
typedef struct cssid cssid_t;
#define fuchsia_wlan_ieee80211_CCMP_PN_LEN UINT32_C(6)
// IEEE Std 802.11-2016 12.5.3.2
#define fuchsia_wlan_ieee80211_CCMP_HDR_LEN UINT32_C(8)

// Declarations
struct ht_capabilities {
  uint8_t bytes[26];
};
struct cssid {
  uint8_t len;
  uint8_t data[32];
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_BANJO_IEEE80211_H_
