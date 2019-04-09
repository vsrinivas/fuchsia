// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/parse_mp_action.h>

#include <wlan/common/element_splitter.h>
#include <wlan/common/mac_frame.h>
#include <wlan/common/parse_element.h>

namespace wlan_mlme = ::fuchsia::wlan::mlme;

namespace wlan {

struct RequiredIes {
    bool have_supp_rates = false;
    bool have_mesh_id = false;
    bool have_mesh_config = false;
    bool have_mpm = false;

    bool have_all() const {
        return have_supp_rates && have_mesh_id && have_mesh_config && have_mpm;
    }
};

static void HandleCommonMpElement(element_id::ElementId id, Span<const uint8_t> raw_body,
                                  wlan_mlme::MeshPeeringCommon* out, RequiredIes* required_ies) {
    switch (id) {
    case element_id::kSuppRates:
        if (auto rates = common::ParseSupportedRates(raw_body)) {
            out->rates.insert(out->rates.end(), rates->begin(), rates->end());
            required_ies->have_supp_rates = true;
        }
        break;
    case element_id::kExtSuppRates:
        if (auto rates = common::ParseExtendedSupportedRates(raw_body)) {
            out->rates.insert(out->rates.end(), rates->begin(), rates->end());
        }
        break;
    case element_id::kMeshId:
        if (auto mesh_id = common::ParseMeshId(raw_body)) {
            out->mesh_id.resize(0);
            out->mesh_id.assign(mesh_id->begin(), mesh_id->end());
            required_ies->have_mesh_id = true;
        }
        break;
    case element_id::kMeshConfiguration:
        if (auto mesh_config = common::ParseMeshConfiguration(raw_body)) {
            out->mesh_config = mesh_config->ToFidl();
            required_ies->have_mesh_config = true;
        }
        break;
    case element_id::kHtCapabilities:
        if (auto ht_cap = common::ParseHtCapabilities(raw_body)) {
            out->ht_cap = std::make_unique<wlan_mlme::HtCapabilities>(ht_cap->ToFidl());
        }
        break;
    case element_id::kHtOperation:
        if (auto ht_op = common::ParseHtOperation(raw_body)) {
            out->ht_op = std::make_unique<wlan_mlme::HtOperation>(ht_op->ToFidl());
        }
        break;
    case element_id::kVhtCapabilities:
        if (auto vht_cap = common::ParseVhtCapabilities(raw_body)) {
            out->vht_cap = std::make_unique<wlan_mlme::VhtCapabilities>(vht_cap->ToFidl());
        }
        break;
    case element_id::kVhtOperation:
        if (auto vht_op = common::ParseVhtOperation(raw_body)) {
            out->vht_op = std::make_unique<wlan_mlme::VhtOperation>(vht_op->ToFidl());
        }
        break;
    default:
        break;
    }
}

static void ConvertMpmHeader(const MpmHeader& header, wlan_mlme::MeshPeeringCommon* out) {
    out->protocol_id = header.protocol;
    out->local_link_id = header.local_link_id;
}

// IEEE Std 802.11-2016, 9.6.16.2.2
bool ParseMpOpenAction(BufferReader* r, wlan_mlme::MeshPeeringOpenAction* out) {
    auto cap_info = r->Read<CapabilityInfo>();
    if (cap_info == nullptr) { return false; }

    RequiredIes required_ies;
    for (auto [id, raw_body] : common::ElementSplitter(r->ReadRemaining())) {
        if (id == element_id::kMeshPeeringManagement) {
            // Handle the MPM element separately since there is no way to handle
            // it in a generic fashion
            if (auto mpm_open = common::ParseMpmOpen(raw_body)) {
                ConvertMpmHeader(mpm_open->header, &out->common);
                required_ies.have_mpm = true;
            }
        } else {
            HandleCommonMpElement(id, raw_body, &out->common, &required_ies);
        }
    }
    return required_ies.have_all();
}

// IEEE Std 802.11-2016, 9.6.16.3.2
bool ParseMpConfirmAction(BufferReader* r, wlan_mlme::MeshPeeringConfirmAction* out) {
    auto cap_info = r->Read<CapabilityInfo>();
    if (cap_info == nullptr) { return false; }

    auto aid = r->Read<uint16_t>();
    if (aid == nullptr) { return false; }
    out->aid = *aid;

    RequiredIes required_ies;
    for (auto [id, raw_body] : common::ElementSplitter(r->ReadRemaining())) {
        if (id == element_id::kMeshPeeringManagement) {
            // Handle the MPM element separately since there is no way to handle
            // it in a generic fashion
            if (auto mpm_confirm = common::ParseMpmConfirm(raw_body)) {
                ConvertMpmHeader(mpm_confirm->header, &out->common);
                required_ies.have_mpm = true;
                out->peer_link_id = mpm_confirm->peer_link_id;
            }
        } else {
            HandleCommonMpElement(id, raw_body, &out->common, &required_ies);
        }
    }
    return required_ies.have_all();
}

}  // namespace wlan
