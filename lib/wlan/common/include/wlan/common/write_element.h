// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_ELEMENT_WRITER_H_
#define GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_ELEMENT_WRITER_H_

#include <wlan/common/buffer_writer.h>
#include <wlan/common/element.h>

namespace wlan {
namespace common {

void WriteSsid(BufferWriter* w, Span<const uint8_t> ssid);
void WriteSupportedRates(BufferWriter* w, Span<const SupportedRate> supported_rates);
void WriteDsssParamSet(BufferWriter* w, uint8_t current_chan);
void WriteCfParamSet(BufferWriter* w, CfParamSet param_set);
void WriteTim(BufferWriter* w, TimHeader header, Span<const uint8_t> bitmap);
void WriteCountry(BufferWriter* w, Country country, Span<SubbandTriplet> triplets);
void WriteExtendedSupportedRates(BufferWriter* w, Span<const SupportedRate> ext_supported_rates);
void WriteMeshConfiguration(BufferWriter* w, MeshConfiguration mesh_config);
void WriteMeshId(BufferWriter* w, Span<const uint8_t> mesh_id);
void WriteQosCapability(BufferWriter* w, QosInfo qos_info);
void WriteGcrGroupAddress(BufferWriter* w, common::MacAddr gcr_group_addr);
void WriteHtCapabilities(BufferWriter* w, const HtCapabilities& ht_caps);
void WriteHtOperation(BufferWriter* w, const HtOperation& ht_op);
void WriteVhtCapabilities(BufferWriter* w, const VhtCapabilities& vht_caps);
void WriteVhtOperation(BufferWriter* w, const VhtOperation& vht_op);

} // namespace common
} // namespace wlan

#endif  // GARNET_LIB_WLAN_COMMON_INCLUDE_WLAN_COMMON_ELEMENT_WRITER_H_
