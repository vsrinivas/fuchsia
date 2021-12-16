// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_H_

#include <netinet/if_ether.h>
#include <stddef.h>
#include <stdint.h>

namespace wlan::drivers::components {

// Differentiated Services Field Codepoints (DSCP). Taken from:
// https://www.iana.org/assignments/dscp-registry/dscp-registry.xhtml
static constexpr uint8_t kDscpLowLatencyDataCs1 = 8;
static constexpr uint8_t kDscpLowLatencyDataAf21 = 18;
static constexpr uint8_t kDscpLowLatencyDataAf22 = 20;
static constexpr uint8_t kDscpLowLatencyDataAf23 = 22;
static constexpr uint8_t kDscpBroadcastVideoCs3 = 24;
static constexpr uint8_t kDscpMultimediaStreamingAf31 = 26;
static constexpr uint8_t kDscpMultimediaStreamingAf32 = 28;
static constexpr uint8_t kDscpMultimediaStreamingAf33 = 30;
static constexpr uint8_t kDscpRealTimeInteractiveCs4 = 32;
static constexpr uint8_t kDscpMultimediaConferencingAf41 = 34;
static constexpr uint8_t kDscpMultimediaConferencingAf42 = 36;
static constexpr uint8_t kDscpMultimediaConferencingAf43 = 38;
static constexpr uint8_t kDscpSignalingCs5 = 40;
static constexpr uint8_t kDscpVoiceAdmit = 44;
static constexpr uint8_t kDscpTelephony = 46;
static constexpr uint8_t kDscpNetworkControlCs6 = 48;
static constexpr uint8_t kDscpNetworkControlCs7 = 56;

// Get the user priority from the DSCP field in an IP header based on RFC 8325. The incoming data
// points to an ethernet header and if it contains an IPv4 or IPv6 payload the DSCP field is
// inspected and the priority code point is retrieved from it. Payloads other than IP or unknown
// code points in the DSCP field result in the lowest possible priority. Payloads that do not meet
// the minimum required size also receive the lowest possible priority. It is therefore safe to use
// this function on any data as long as the the size parameter correctly represents the amount of
// available data. This translation is based on RFC-8325 Section 4.3
// https://datatracker.ietf.org/doc/html/rfc8325#section-4.3
inline uint8_t Get80211UserPriority(const uint8_t* data, size_t size) {
  constexpr size_t kDsFieldLength = 2;
  if (size < sizeof(ethhdr) + kDsFieldLength) {
    return 0;
  }

  auto* eh = reinterpret_cast<const struct ethhdr*>(data);
  uint8_t dscp = 0;
  if (eh->h_proto == htobe16(ETH_P_IP)) {
    const uint8_t* ipv4_hdr = data + sizeof(ethhdr);
    dscp = ipv4_hdr[1] >> 2;
  } else if (eh->h_proto == htobe16(ETH_P_IPV6)) {
    const uint16_t* ipv6_hdr = reinterpret_cast<const uint16_t*>(data + sizeof(ethhdr));
    dscp = (be16toh(*ipv6_hdr) >> 6) & 0x3F;
  }

  // Map DSCP to user priority based on RFC 8325
  switch (dscp) {
    case kDscpNetworkControlCs7:
    case kDscpNetworkControlCs6:
      return 7;
    case kDscpTelephony:
    case kDscpVoiceAdmit:
      return 6;
    case kDscpSignalingCs5:
      return 5;
    case kDscpMultimediaConferencingAf43:
    case kDscpMultimediaConferencingAf42:
    case kDscpMultimediaConferencingAf41:
    case kDscpRealTimeInteractiveCs4:
    case kDscpMultimediaStreamingAf33:
    case kDscpMultimediaStreamingAf32:
    case kDscpMultimediaStreamingAf31:
    case kDscpBroadcastVideoCs3:
      return 4;
    case kDscpLowLatencyDataAf23:
    case kDscpLowLatencyDataAf22:
    case kDscpLowLatencyDataAf21:
      return 3;
    case kDscpLowLatencyDataCs1:
      return 1;
  }
  // Remaining and unknown code points default to lowest priority
  return 0;
}

// Convert a user priority (as retrieved with the function above for example) to an ordered
// precedence index. A higher value indicates a higher priority. This ensures that even though the
// user priority of background traffic is higher than best effort the precedence of best effort is
// higher than background traffic. Based on IEEE 802.11-2016 Section 10.2.4.2.
inline uint32_t UserPriorityToPrecedence(uint8_t priority) {
  static constexpr uint32_t kLookup[] = {1, 2, 0, 3, 4, 5, 6, 7};
  return kLookup[priority];
}

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_PRIORITY_H_
