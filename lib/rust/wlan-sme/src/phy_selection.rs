// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::{self as fidl_mlme};
use crate::client::{ConnectPhyParams};

pub fn derive_cbw_ht(bss: &fidl_mlme::BssDescription, params: &ConnectPhyParams)
    -> fidl_mlme::Cbw {
    // TODO(NET-1575): Get client_cbw from true capabilities
    let client_cbw = fidl_mlme::Cbw::Cbw40Below;

    // Derive CBW from AP's HT IEs
    let sec_chan = bss.ht_op.as_ref().expect("bss.ht_op should be present")
                      .ht_op_info.secondary_chan_offset;

    let ap_cbw;
    if sec_chan == fidl_mlme::SecChanOffset::SecondaryAbove as u8 {
        ap_cbw = fidl_mlme::Cbw::Cbw40;
    } else if sec_chan == fidl_mlme::SecChanOffset::SecondaryBelow as u8 {
        ap_cbw = fidl_mlme::Cbw::Cbw40Below;
    } else {
        ap_cbw = fidl_mlme::Cbw::Cbw20;
    }

    let best_cbw = std::cmp::min(client_cbw, ap_cbw);
    // Conditionally override
    match params.cbw {
        None => best_cbw,
        Some(params_cbw) => std::cmp::min(params_cbw, best_cbw),
    }
}

pub fn derive_cbw_vht(bss: &fidl_mlme::BssDescription, params: &ConnectPhyParams)
    -> fidl_mlme::Cbw {
    // TODO(NET-1575): Get client_cbw from true capabilities
    let client_cbw = fidl_mlme::Cbw::Cbw40Below;

    // Derive CBW from AP's VHT IEs
    let ap_cbw;
    let vht_op = bss.vht_op.as_ref().expect("bss.vht_op should be present");
    if vht_op.vht_cbw == fidl_mlme::VhtCbw::Cbw8016080P80 as u8 {
        // See IEEE Std 802.11-2016, Table 9-253
        let seg0 = vht_op.center_freq_seg0;
        let seg1 = vht_op.center_freq_seg1;
        let gap = if seg0 >= seg1 { seg0 - seg1 } else { seg1 - seg0 };

        ap_cbw = if seg1 == 0 {
            fidl_mlme::Cbw::Cbw80
        } else if gap == 8 {
            fidl_mlme::Cbw::Cbw160
        } else if gap > 16 {
            fidl_mlme::Cbw::Cbw80P80
        } else {
            fidl_mlme::Cbw::Cbw80
        };
    } else {
        ap_cbw = derive_cbw_ht(bss, params);
    }

    let best_cbw = std::cmp::min(client_cbw, ap_cbw);
    // Conditionally override
    match params.cbw {
        None => best_cbw,
        Some(param_cbw) => std::cmp::min(param_cbw, best_cbw),
    }
}


pub fn derive_phy_cbw(bss: &fidl_mlme::BssDescription, params: &ConnectPhyParams)
    -> (fidl_mlme::Phy, fidl_mlme::Cbw) {

    // TODO(NET-1575): Get client_phy from true capabilities
    let client_phy = fidl_mlme::Phy::Ht;
    let ap_phy =
        if bss.ht_cap.is_none() || bss.ht_op.is_none() { fidl_mlme::Phy::Erp }
        else if bss.vht_cap.is_none() || bss.vht_op.is_none() { fidl_mlme::Phy::Ht }
        else { fidl_mlme::Phy::Vht };

    // best_phy is the largest intersection of client's and ap's.
    let best_phy = std::cmp::min(client_phy, ap_phy);

    // Conditionally override
    let phy_to_use = match params.phy {
        None => best_phy,
        Some(param_phy) => std::cmp::min(param_phy, best_phy),
    };

    let cbw_to_use = match phy_to_use {
        fidl_mlme::Phy::Hr => fidl_mlme::Cbw::Cbw20,
        fidl_mlme::Phy::Erp => fidl_mlme::Cbw::Cbw20,
        fidl_mlme::Phy::Ht => derive_cbw_ht(bss, params),
        fidl_mlme::Phy::Vht => derive_cbw_vht(bss, params),
        fidl_mlme::Phy::Hew => derive_cbw_vht(bss, params), // Not implemented
    };

    (phy_to_use, cbw_to_use)
}

