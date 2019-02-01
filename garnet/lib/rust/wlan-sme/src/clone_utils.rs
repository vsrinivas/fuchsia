// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, BandCapabilities};

pub fn clone_capability_info(c: &fidl_mlme::CapabilityInfo) -> fidl_mlme::CapabilityInfo {
    fidl_mlme::CapabilityInfo { ..*c }
}

pub fn clone_ht_capabilities(c: &fidl_mlme::HtCapabilities) -> fidl_mlme::HtCapabilities {
    fidl_mlme::HtCapabilities {
        ht_cap_info: fidl_mlme::HtCapabilityInfo { ..c.ht_cap_info },
        ampdu_params: fidl_mlme::AmpduParams { ..c.ampdu_params },
        mcs_set: fidl_mlme::SupportedMcsSet { ..c.mcs_set },
        ht_ext_cap: fidl_mlme::HtExtCapabilities { ..c.ht_ext_cap },
        txbf_cap: fidl_mlme::TxBfCapability { ..c.txbf_cap },
        asel_cap: fidl_mlme::AselCapability { ..c.asel_cap },
    }
}

pub fn clone_ht_operation(o: &fidl_mlme::HtOperation) -> fidl_mlme::HtOperation {
    fidl_mlme::HtOperation {
        ht_op_info: fidl_mlme::HtOperationInfo { ..o.ht_op_info },
        basic_mcs_set: fidl_mlme::SupportedMcsSet { ..o.basic_mcs_set },
        ..*o
    }
}

pub fn clone_vht_mcs_nss(m: &fidl_mlme::VhtMcsNss) -> fidl_mlme::VhtMcsNss {
    fidl_mlme::VhtMcsNss {
        rx_max_mcs: m.rx_max_mcs.clone(),
        tx_max_mcs: m.tx_max_mcs.clone(),
        ..*m
    }
}

pub fn clone_basic_vht_mcs_nss(m: &fidl_mlme::BasicVhtMcsNss) -> fidl_mlme::BasicVhtMcsNss {
    fidl_mlme::BasicVhtMcsNss { max_mcs: m.max_mcs.clone(), ..*m }
}

pub fn clone_vht_capabilities_info(
    i: &fidl_mlme::VhtCapabilitiesInfo,
) -> fidl_mlme::VhtCapabilitiesInfo {
    fidl_mlme::VhtCapabilitiesInfo { ..*i }
}

pub fn clone_vht_capabilities(c: &fidl_mlme::VhtCapabilities) -> fidl_mlme::VhtCapabilities {
    fidl_mlme::VhtCapabilities {
        vht_cap_info: clone_vht_capabilities_info(&c.vht_cap_info),
        vht_mcs_nss: clone_vht_mcs_nss(&c.vht_mcs_nss),
    }
}

pub fn clone_vht_operation(o: &fidl_mlme::VhtOperation) -> fidl_mlme::VhtOperation {
    fidl_mlme::VhtOperation { basic_mcs: clone_basic_vht_mcs_nss(&o.basic_mcs), ..*o }
}

pub fn clone_bss_desc(d: &fidl_mlme::BssDescription) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: d.bssid.clone(),
        ssid: d.ssid.clone(),
        bss_type: d.bss_type,
        beacon_period: d.beacon_period,
        dtim_period: d.dtim_period,
        timestamp: d.timestamp,
        local_time: d.local_time,

        cap: clone_capability_info(&d.cap),
        basic_rate_set: d.basic_rate_set.clone(),
        op_rate_set: d.op_rate_set.clone(),
        country: d.country.clone(),

        rsn: d.rsn.clone(),

        rcpi_dbmh: d.rcpi_dbmh,
        rsni_dbh: d.rsni_dbh,

        ht_cap: d.ht_cap.as_ref().map(|v| Box::new(clone_ht_capabilities(v))),
        ht_op: d.ht_op.as_ref().map(|v| Box::new(clone_ht_operation(v))),

        vht_cap: d.vht_cap.as_ref().map(|v| Box::new(clone_vht_capabilities(v))),
        vht_op: d.vht_op.as_ref().map(|v| Box::new(clone_vht_operation(v))),

        chan: fidl_common::WlanChan {
            primary: d.chan.primary,
            cbw: d.chan.cbw,
            secondary80: d.chan.secondary80,
        },
        rssi_dbm: d.rssi_dbm,
    }
}

pub fn clone_band_cap(b: &BandCapabilities) -> BandCapabilities {
    BandCapabilities {
        band_id: b.band_id,
        basic_rates: b.basic_rates.clone(),
        base_frequency: b.base_frequency,
        channels: b.channels.clone(),
        ht_cap: b.ht_cap.as_ref().map(|v| Box::new(clone_ht_capabilities(v))),
        vht_cap: b.vht_cap.as_ref().map(|v| Box::new(clone_vht_capabilities(v))),
        cap: clone_capability_info(&b.cap),
    }
}

pub fn clone_bands(bv: &Vec<BandCapabilities>) -> Vec<BandCapabilities> {
    bv.iter().map(clone_band_cap).collect()
}

pub fn clone_mesh_configuration(c: &fidl_mlme::MeshConfiguration) -> fidl_mlme::MeshConfiguration {
    fidl_mlme::MeshConfiguration { ..*c }
}

pub fn clone_mesh_peering_common(c: &fidl_mlme::MeshPeeringCommon) -> fidl_mlme::MeshPeeringCommon {
    fidl_mlme::MeshPeeringCommon {
        mesh_id: c.mesh_id.clone(),
        mesh_config: clone_mesh_configuration(&c.mesh_config),
        rates: c.rates.clone(),
        ht_cap: c.ht_cap.as_ref().map(|x| Box::new(clone_ht_capabilities(x))),
        ht_op: c.ht_op.as_ref().map(|x| Box::new(clone_ht_operation(x))),
        vht_cap: c.vht_cap.as_ref().map(|x| Box::new(clone_vht_capabilities(x))),
        vht_op: c.vht_op.as_ref().map(|x| Box::new(clone_vht_operation(x))),
        ..*c
    }
}
