// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/ht.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/ps_cfg.h>

namespace wlan {

enum class BssType {
    kInfrastructure = 0,
    kIndependent,
    kMesh
};

struct BeaconConfig {
    common::MacAddr bssid;
    BssType bss_type;
    const uint8_t* ssid;
    size_t ssid_len;
    const uint8_t* rsne;
    size_t rsne_len;
    uint16_t beacon_period;
    wlan_channel_t channel;
    const PsCfg* ps_cfg;
    uint64_t timestamp;
    HtConfig ht;
    MeshConfiguration* mesh_config;
    const uint8_t* mesh_id;
    size_t mesh_id_len;
    const SupportedRate* rates; // covers both Supported Rates and Ext Sup Rates elements
    size_t rates_len;
};

zx_status_t BuildBeacon(const BeaconConfig& config, MgmtFrame<Beacon>* buffer,
                        size_t* tim_ele_offset);

zx_status_t BuildProbeResponse(const BeaconConfig& config,
                               common::MacAddr addr1,
                               MgmtFrame<ProbeResponse>* buffer);

} // namespace wlan
