// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_

#include <wlan/common/element.h>
#include <wlan/common/span.h>

namespace wlan {
namespace common {

struct ParsedTim {
  TimHeader header;
  Span<const uint8_t> bitmap;
};

struct ParsedCountry {
  Country country;
  Span<const SubbandTriplet> triplets;
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
  Span<const PreqPerTarget> per_target;
};

struct ParsedPrep {
  const PrepHeader* header;
  const common::MacAddr* target_external_addr;  // null if absent
  const PrepTail* tail;
};

struct ParsedPerr {
  const PerrHeader* header;
  Span<const uint8_t> destinations;  // can be parsed with PerrDestinationParser
};

std::optional<Span<const uint8_t>> ParseSsid(Span<const uint8_t> raw_body);
std::optional<Span<const SupportedRate>> ParseSupportedRates(
    Span<const uint8_t> raw_body);
const DsssParamSet* ParseDsssParamSet(Span<const uint8_t> raw_body);
const CfParamSet* ParseCfParamSet(Span<const uint8_t> raw_body);
std::optional<ParsedTim> ParseTim(Span<const uint8_t> raw_body);
std::optional<ParsedCountry> ParseCountry(Span<const uint8_t> raw_body);
std::optional<Span<const SupportedRate>> ParseExtendedSupportedRates(
    Span<const uint8_t> raw_body);
const MeshConfiguration* ParseMeshConfiguration(Span<const uint8_t> raw_body);
std::optional<Span<const uint8_t>> ParseMeshId(Span<const uint8_t> raw_body);
const QosInfo* ParseQosCapability(Span<const uint8_t> raw_body);
const common::MacAddr* ParseGcrGroupAddress(Span<const uint8_t> raw_body);
const HtCapabilities* ParseHtCapabilities(Span<const uint8_t> raw_body);
const HtOperation* ParseHtOperation(Span<const uint8_t> raw_body);
const VhtCapabilities* ParseVhtCapabilities(Span<const uint8_t> raw_body);
const VhtOperation* ParseVhtOperation(Span<const uint8_t> raw_body);

// It is impossible to parse the Mesh Peering Management element without knowing
// the context, i.e. whether it belongs to Open, Confirm or Close action. The
// following three functions parse it for each of the three contexts,
// respectively.
std::optional<ParsedMpmOpen> ParseMpmOpen(Span<const uint8_t> raw_body);
std::optional<ParsedMpmConfirm> ParseMpmConfirm(Span<const uint8_t> raw_body);
std::optional<ParsedMpmClose> ParseMpmClose(Span<const uint8_t> raw_body);

std::optional<ParsedPreq> ParsePreq(Span<const uint8_t> raw_body);
std::optional<ParsedPrep> ParsePrep(Span<const uint8_t> raw_body);
std::optional<ParsedPerr> ParsePerr(Span<const uint8_t> raw_body);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
