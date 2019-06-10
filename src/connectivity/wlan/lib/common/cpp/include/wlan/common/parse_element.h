// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
#define SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_

#include <fbl/span.h>
#include <wlan/common/element.h>

namespace wlan {
namespace common {

struct ParsedTim {
  TimHeader header;
  fbl::Span<const uint8_t> bitmap;
};

struct ParsedCountry {
  Country country;
  fbl::Span<const SubbandTriplet> triplets;
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
  fbl::Span<const PreqPerTarget> per_target;
};

struct ParsedPrep {
  const PrepHeader* header;
  const common::MacAddr* target_external_addr;  // null if absent
  const PrepTail* tail;
};

struct ParsedPerr {
  const PerrHeader* header;
  fbl::Span<const uint8_t>
      destinations;  // can be parsed with PerrDestinationParser
};

std::optional<fbl::Span<const uint8_t>> ParseSsid(
    fbl::Span<const uint8_t> raw_body);
std::optional<fbl::Span<const SupportedRate>> ParseSupportedRates(
    fbl::Span<const uint8_t> raw_body);
const DsssParamSet* ParseDsssParamSet(fbl::Span<const uint8_t> raw_body);
const CfParamSet* ParseCfParamSet(fbl::Span<const uint8_t> raw_body);
std::optional<ParsedTim> ParseTim(fbl::Span<const uint8_t> raw_body);
std::optional<ParsedCountry> ParseCountry(fbl::Span<const uint8_t> raw_body);
std::optional<fbl::Span<const SupportedRate>> ParseExtendedSupportedRates(
    fbl::Span<const uint8_t> raw_body);
const MeshConfiguration* ParseMeshConfiguration(
    fbl::Span<const uint8_t> raw_body);
std::optional<fbl::Span<const uint8_t>> ParseMeshId(
    fbl::Span<const uint8_t> raw_body);
const QosInfo* ParseQosCapability(fbl::Span<const uint8_t> raw_body);
const common::MacAddr* ParseGcrGroupAddress(fbl::Span<const uint8_t> raw_body);
const HtCapabilities* ParseHtCapabilities(fbl::Span<const uint8_t> raw_body);
const HtOperation* ParseHtOperation(fbl::Span<const uint8_t> raw_body);
const VhtCapabilities* ParseVhtCapabilities(fbl::Span<const uint8_t> raw_body);
const VhtOperation* ParseVhtOperation(fbl::Span<const uint8_t> raw_body);

// It is impossible to parse the Mesh Peering Management element without knowing
// the context, i.e. whether it belongs to Open, Confirm or Close action. The
// following three functions parse it for each of the three contexts,
// respectively.
std::optional<ParsedMpmOpen> ParseMpmOpen(fbl::Span<const uint8_t> raw_body);
std::optional<ParsedMpmConfirm> ParseMpmConfirm(
    fbl::Span<const uint8_t> raw_body);
std::optional<ParsedMpmClose> ParseMpmClose(fbl::Span<const uint8_t> raw_body);

std::optional<ParsedPreq> ParsePreq(fbl::Span<const uint8_t> raw_body);
std::optional<ParsedPrep> ParsePrep(fbl::Span<const uint8_t> raw_body);
std::optional<ParsedPerr> ParsePerr(fbl::Span<const uint8_t> raw_body);

}  // namespace common
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_COMMON_CPP_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
