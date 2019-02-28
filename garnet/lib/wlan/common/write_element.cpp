// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/write_element.h>

namespace wlan {
namespace common {

namespace {

template <typename T> struct PartTraits {
    static constexpr size_t size_bytes(const T& t) { return sizeof(T); }

    static void write(BufferWriter* w, const T& t) { w->Write(as_bytes(Span<const T>{&t, 1u})); }
};

template <typename T> struct PartTraits<Span<T>> {
    static constexpr size_t size_bytes(const Span<T>& t) { return t.size_bytes(); }

    static void write(BufferWriter* w, const Span<T>& t) { w->Write(as_bytes(t)); }
};

template <size_t PadTo, typename... T>
void WriteWithPadding(BufferWriter* w, element_id::ElementId elem_id, const T&... parts) {
    w->WriteByte(elem_id);

    size_t total_size = (PartTraits<T>::size_bytes(parts) + ...);
    size_t unpadded = total_size % PadTo;
    size_t padding = 0;
    if (unpadded > 0) { padding = PadTo - unpadded; }

    w->WriteByte(static_cast<uint8_t>(total_size + padding));

    (PartTraits<T>::write(w, parts), ...);

    for (size_t i = 0; i < padding; ++i) {
        w->WriteByte(0);
    }
}

template <typename... T>
void Write(BufferWriter* w, element_id::ElementId elem_id, const T&... parts) {
    WriteWithPadding<1>(w, elem_id, parts...);
}
}  // namespace

void WriteSsid(BufferWriter* w, Span<const uint8_t> ssid) {
    Write(w, element_id::kSsid, ssid);
}

void WriteSupportedRates(BufferWriter* w, Span<const SupportedRate> supported_rates) {
    Write(w, element_id::kSuppRates, supported_rates);
}

void WriteDsssParamSet(BufferWriter* w, uint8_t current_chan) {
    Write(w, element_id::kDsssParamSet, current_chan);
}

void WriteCfParamSet(BufferWriter* w, CfParamSet param_set) {
    Write(w, element_id::kCfParamSet, param_set);
}

void WriteTim(BufferWriter* w, TimHeader header, Span<const uint8_t> bitmap) {
    Write(w, element_id::kTim, header, bitmap);
}

void WriteCountry(BufferWriter* w, Country country, Span<SubbandTriplet> triplets) {
    WriteWithPadding<2>(w, element_id::kCountry, country, triplets);
}

void WriteExtendedSupportedRates(BufferWriter* w, Span<const SupportedRate> ext_supported_rates) {
    Write(w, element_id::kExtSuppRates, ext_supported_rates);
}

void WriteMeshConfiguration(BufferWriter* w, MeshConfiguration mesh_config) {
    Write(w, element_id::kMeshConfiguration, mesh_config);
}

void WriteMeshId(BufferWriter* w, Span<const uint8_t> mesh_id) {
    Write(w, element_id::kMeshId, mesh_id);
}

void WriteQosCapability(BufferWriter* w, QosInfo qos_info) {
    Write(w, element_id::kQosCapability, qos_info);
}

void WriteGcrGroupAddress(BufferWriter* w, common::MacAddr gcr_group_addr) {
    Write(w, element_id::kGcrGroupAddress, gcr_group_addr);
}

void WriteHtCapabilities(BufferWriter* w, const HtCapabilities& ht_caps) {
    Write(w, element_id::kHtCapabilities, ht_caps);
}

void WriteHtOperation(BufferWriter* w, const HtOperation& ht_op) {
    Write(w, element_id::kHtOperation, ht_op);
}

void WriteVhtCapabilities(BufferWriter* w, const VhtCapabilities& vht_caps) {
    Write(w, element_id::kVhtCapabilities, vht_caps);
}

void WriteVhtOperation(BufferWriter* w, const VhtOperation& vht_op) {
    Write(w, element_id::kVhtOperation, vht_op);
}

void WriteMpmOpen(BufferWriter* w, MpmHeader mpm_header, const MpmPmk* pmk) {
    auto pmk_bytes = pmk == nullptr ? Span<const uint8_t>{} : Span<const uint8_t>{pmk->data};
    Write(w, element_id::kMeshPeeringManagement, mpm_header, pmk_bytes);
}

void WriteMpmConfirm(BufferWriter* w, MpmHeader mpm_header, uint16_t peer_link_id,
                     const MpmPmk* pmk) {
    auto pmk_bytes = pmk == nullptr ? Span<const uint8_t>{} : Span<const uint8_t>{pmk->data};
    Write(w, element_id::kMeshPeeringManagement, mpm_header, peer_link_id, pmk_bytes);
}

void WritePreq(BufferWriter* w, const PreqHeader& header,
               const common::MacAddr* originator_external_addr, const PreqMiddle& middle,
               Span<const PreqPerTarget> per_target) {
    auto ext_bytes = originator_external_addr == nullptr
                         ? Span<const uint8_t>{}
                         : Span<const uint8_t>{originator_external_addr->byte};
    Write(w, element_id::kPreq, header, ext_bytes, middle, per_target);
}

void WritePrep(BufferWriter* w, const PrepHeader& header,
               const common::MacAddr* target_external_addr, const PrepTail& tail) {
    auto ext_bytes = target_external_addr == nullptr
                         ? Span<const uint8_t>{}
                         : Span<const uint8_t>{target_external_addr->byte};
    Write(w, element_id::kPrep, header, ext_bytes, tail);
}

void WritePerr(BufferWriter* w, const PerrHeader& header, Span<const uint8_t> destinations) {
    Write(w, element_id::kPerr, header, destinations);
}

}  // namespace common
}  // namespace wlan
