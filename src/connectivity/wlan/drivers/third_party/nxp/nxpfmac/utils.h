// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_UTILS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_UTILS_H_

#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <stdint.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"

namespace wlan::nxpfmac {

constexpr uint8_t band_from_channel(uint32_t channel) {
  return channel > 14 ? BAND_5GHZ : BAND_2GHZ;
}

constexpr bool is_dfs_channel(uint32_t channel) {
  // TODO(https://fxbug.dev/110320): Take regulatory domain into consideration.
  return channel >= 52 && channel <= 144;
}

// Returns true if `cipher_suite` indicates the use of WPA1, WPA2, or WPA3.
constexpr bool is_wpa_cipher_suite(uint8_t cipher_suite) {
  return cipher_suite == CIPHER_SUITE_TYPE_TKIP || cipher_suite == CIPHER_SUITE_TYPE_CCMP_128 ||
         cipher_suite == CIPHER_SUITE_TYPE_CCMP_256 || cipher_suite == CIPHER_SUITE_TYPE_GCMP_128 ||
         cipher_suite == CIPHER_SUITE_TYPE_GCMP_256;
}

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_UTILS_H_
