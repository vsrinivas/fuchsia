// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_

#include <lib/stdcompat/span.h>

#include <wlan/common/element.h>

namespace wlan {
namespace common {

struct ParsedTim {
  TimHeader header;
  cpp20::span<const uint8_t> bitmap;
};

struct ParsedCountry {
  Country country;
  cpp20::span<const SubbandTriplet> triplets;
};

struct ParsedMpmOpen {
  MpmHeader header;
  const MpmPmk* pmk;  // null if absent
};

struct ParsedMpmConfirm {
  MpmHeader header;
  uint16_t peer_link_id;
  const MpmPmk* pmk;  // null if absent
};

struct ParsedMpmClose {
  MpmHeader header;
  std::optional<uint16_t> peer_link_id;
  uint16_t reason_code;
  const MpmPmk* pmk;  // null if absent
};

struct ParsedPreq {
  const PreqHeader* header;
  const common::MacAddr* originator_external_addr;  // null if absent
  const PreqMiddle* middle;
  cpp20::span<const PreqPerTarget> per_target;
};

struct ParsedPrep {
  const PrepHeader* header;
  const common::MacAddr* target_external_addr;  // null if absent
  const PrepTail* tail;
};

struct ParsedPerr {
  const PerrHeader* header;
  cpp20::span<const uint8_t> destinations;  // can be parsed with PerrDestinationParser
};

std::optional<cpp20::span<const uint8_t>> ParseSsid(cpp20::span<const uint8_t> raw_body);
std::optional<cpp20::span<const SupportedRate>> ParseSupportedRates(
    cpp20::span<const uint8_t> raw_body);
const DsssParamSet* ParseDsssParamSet(cpp20::span<const uint8_t> raw_body);
const CfParamSet* ParseCfParamSet(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedTim> ParseTim(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedCountry> ParseCountry(cpp20::span<const uint8_t> raw_body);
std::optional<cpp20::span<const SupportedRate>> ParseExtendedSupportedRates(
    cpp20::span<const uint8_t> raw_body);
const MeshConfiguration* ParseMeshConfiguration(cpp20::span<const uint8_t> raw_body);
std::optional<cpp20::span<const uint8_t>> ParseMeshId(cpp20::span<const uint8_t> raw_body);
const QosInfo* ParseQosCapability(cpp20::span<const uint8_t> raw_body);
const common::MacAddr* ParseGcrGroupAddress(cpp20::span<const uint8_t> raw_body);
const HtCapabilities* ParseHtCapabilities(cpp20::span<const uint8_t> raw_body);
const HtOperation* ParseHtOperation(cpp20::span<const uint8_t> raw_body);
const VhtCapabilities* ParseVhtCapabilities(cpp20::span<const uint8_t> raw_body);
const VhtOperation* ParseVhtOperation(cpp20::span<const uint8_t> raw_body);

// It is impossible to parse the Mesh Peering Management element without knowing
// the context, i.e. whether it belongs to Open, Confirm or Close action. The
// following three functions parse it for each of the three contexts,
// respectively.
std::optional<ParsedMpmOpen> ParseMpmOpen(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedMpmConfirm> ParseMpmConfirm(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedMpmClose> ParseMpmClose(cpp20::span<const uint8_t> raw_body);

std::optional<ParsedPreq> ParsePreq(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedPrep> ParsePrep(cpp20::span<const uint8_t> raw_body);
std::optional<ParsedPerr> ParsePerr(cpp20::span<const uint8_t> raw_body);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
