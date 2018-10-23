// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
#define GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_

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

std::optional<Span<const uint8_t>> ParseSsid(Span<const uint8_t> raw_body);
std::optional<Span<const SupportedRate>> ParseSupportedRates(Span<const uint8_t> raw_body);
const DsssParamSet* ParseDsssParamSet(Span<const uint8_t> raw_body);
const CfParamSet* ParseCfParamSet(Span<const uint8_t> raw_body);
std::optional<ParsedTim> ParseTim(Span<const uint8_t> raw_body);
std::optional<ParsedCountry> ParseCountry(Span<const uint8_t> raw_body);
std::optional<Span<const SupportedRate>> ParseExtendedSupportedRates(Span<const uint8_t> raw_body);
const MeshConfiguration* ParseMeshConfiguration(Span<const uint8_t> raw_body);
std::optional<Span<const uint8_t>> ParseMeshId(Span<const uint8_t> raw_body);
const QosInfo* ParseQosCapability(Span<const uint8_t> raw_body);
const common::MacAddr* ParseGcrGroupAddress(Span<const uint8_t> raw_body);
const HtCapabilities* ParseHtCapabilities(Span<const uint8_t> raw_body);
const HtOperation* ParseHtOperation(Span<const uint8_t> raw_body);
const VhtCapabilities* ParseVhtCapabilities(Span<const uint8_t> raw_body);
const VhtOperation* ParseVhtOperation(Span<const uint8_t> raw_body);

} // namespace common
} // namespace wlan

#endif // GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_PARSE_ELEMENT_H_
