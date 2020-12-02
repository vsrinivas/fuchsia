// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_sme as fidl_sme;

pub(crate) fn clone_bss_info(bss: &fidl_sme::BssInfo) -> fidl_sme::BssInfo {
    fidl_sme::BssInfo {
        bssid: bss.bssid.clone(),
        ssid: bss.ssid.clone(),
        rssi_dbm: bss.rssi_dbm,
        snr_db: bss.snr_db,
        channel: bss.channel,
        protection: bss.protection,
        compatible: bss.compatible,
        bss_desc: bss.bss_desc.clone(),
    }
}
