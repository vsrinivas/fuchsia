// Copyright (c) 2020 The Fuchsia Authors
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

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_MCS_RATE_LOOKUP_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_MCS_RATE_LOOKUP_H_

// Lookup the data rate for a given set of PHY data rate parameters.
// See IEEE 802.11-2016 19.5 and IEEE 802.11-2016 21.5 for details on data rate parameters.

#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <zircon/status.h>
#include <zircon/types.h>

namespace wlan::common {

// Lookup the data rate for the given HT PHY parameters.
zx_status_t HtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                             const ::fuchsia::wlan::common::GuardInterval& gi, uint32_t* out_kbps);

// Lookup the data rate for the given VHT PHY parameters.
zx_status_t VhtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                              const ::fuchsia::wlan::common::GuardInterval& gi, uint8_t num_sts,
                              uint8_t stbc, uint32_t* out_kbps);

// Lookup the data rate for the given VHT PHY parameters. This is a convenience method for callers
// that have the nss value, but not the num_sts or stbc values.
zx_status_t VhtDataRateLookup(const ::fuchsia::wlan::common::ChannelBandwidth& cbw, uint8_t mcs,
                              const ::fuchsia::wlan::common::GuardInterval& gi, uint8_t nss,
                              uint32_t* out_kbps);

}  // namespace wlan::common

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_MCS_RATE_LOOKUP_H_
