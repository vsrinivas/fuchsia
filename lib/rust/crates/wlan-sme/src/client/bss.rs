// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_mlme::BssDescription;
use Ssid;
use std::cmp::Ordering;
use wlan_rsn::{akm, cipher, rsne::{self, Rsne}};

#[derive(Clone, Debug, PartialEq)]
pub struct BssInfo {
    pub bssid: [u8; 6],
    pub ssid: Ssid,
    pub rx_dbm: i8,
    pub channel: u8,
    pub protected: bool,
    pub compatible: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub struct EssInfo {
    pub best_bss: BssInfo,
}

pub fn convert_bss_description(bss: &BssDescription) -> BssInfo {
    BssInfo {
        bssid: bss.bssid.clone(),
        ssid: bss.ssid.bytes().collect(),
        rx_dbm: get_rx_dbm(bss),
        channel: bss.chan.primary,
        protected: bss.rsn.is_some(),
        compatible: is_bss_compatible(bss),
    }
}

pub fn compare_bss(left: &BssDescription, right: &BssDescription) -> Ordering {
    is_bss_compatible(left).cmp(&is_bss_compatible(right))
        .then(get_rx_dbm(left).cmp(&get_rx_dbm(right)))
}

fn get_rx_dbm(bss: &BssDescription) -> i8 {
    if bss.rcpi_dbmh != 0 {
        (bss.rcpi_dbmh / 2) as i8
    } else if bss.rssi_dbm != 0 {
        bss.rssi_dbm
    } else {
        ::std::i8::MIN
    }
}

fn is_bss_compatible(bss: &BssDescription) -> bool {
    match bss.rsn.as_ref() {
        None => true,
        Some(rsn) => match rsne::from_bytes(&rsn[..]).to_full_result() {
            Ok(a_rsne) => is_rsn_compatible(&a_rsne),
            _ => false
        }
    }
}

/// Supported Ciphers and AKMs:
/// Group Data Ciphers: CCMP-128, TKIP
/// Pairwise Cipher: CCMP-128
/// AKM: PSK
pub fn is_rsn_compatible(a_rsne: &Rsne) -> bool {
    let has_supported_group_data_cipher = match a_rsne.group_data_cipher_suite.as_ref() {
        Some(c) if c.has_known_usage() => match c.suite_type {
            // IEEE allows TKIP usage only in GTKSAs for compatibility reasons.
            // TKIP is considered broken and should never be used in a PTKSA or IGTKSA.
            cipher::CCMP_128 | cipher::TKIP => true,
            _ => false,
        },
        _ => false,
    };
    let has_supported_pairwise_cipher = a_rsne.pairwise_cipher_suites.iter()
        .any(|c| c.has_known_usage() && c.suite_type == cipher::CCMP_128);
    let has_supported_akm_suite = a_rsne.akm_suites.iter()
        .any(|a| a.has_known_algorithm() && a.suite_type == akm::PSK);

    has_supported_group_data_cipher && has_supported_pairwise_cipher && has_supported_akm_suite
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_mlme;
    use std::cmp::Ordering;

    #[test]
    fn compare() {
        // Identical BSSes should be ranked equal
        assert_eq!(Ordering::Equal,
                   compare_bss(&bss(-10, -30, true), &bss(-10, -30, true)));
        // Compatibility takes priority over everything else
        assert_bss_cmp(&bss(-10, -10, false), &bss(-50, -50, true));
        // RCPI takes priority over RSSI
        assert_bss_cmp(&bss(-20, -30, true), &bss(-30, -20, true));
        // Compare RSSI if RCPI is absent
        assert_bss_cmp(&bss(-30, 0, true), &bss(-20, 0, true));
        // Having an RCPI measurement is always better than not having any measurement
        assert_bss_cmp(&bss(0, 0, true), &bss(0, -200, true));
        // Having an RSSI measurement is always better than not having any measurement
        assert_bss_cmp(&bss(0, 0, true), &bss(-100, 0, true));
    }

    fn assert_bss_cmp(worse: &fidl_mlme::BssDescription, better: &fidl_mlme::BssDescription) {
        assert_eq!(Ordering::Less, compare_bss(worse, better));
        assert_eq!(Ordering::Greater, compare_bss(better, worse));
    }

    fn bss(_rssi_dbm: i8, _rcpi_dbmh: i16, compatible: bool) -> fidl_mlme::BssDescription {
        let ret = fidl_mlme::BssDescription {
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: String::new(),

            bss_type: fidl_mlme::BssTypes::Infrastructure,
            beacon_period: 100,
            dtim_period: 100,
            timestamp: 0,
            local_time: 0,

            cap: fidl_mlme::CapabilityInfo {
                ess: false,
                ibss: false,
                cf_pollable: false,
                cf_poll_req: false,
                privacy: false,
                short_preamble: false,
                spectrum_mgmt: false,
                qos: false,
                short_slot_time: false,
                apsd: false,
                radio_msmt: false,
                delayed_block_ack: false,
                immediate_block_ack: false,
            },
            country: None,
            rsn: if compatible { None } else { Some(Vec::new()) },

            rcpi_dbmh: _rcpi_dbmh,
            rsni_dbh: 0,

            ht_cap: None,
            ht_op: None,
            vht_cap: None,
            vht_op: None,

            chan: fidl_mlme::WlanChan { primary: 1, secondary80: 0, cbw: fidl_mlme::Cbw::Cbw20 },
            rssi_dbm: _rssi_dbm,
        };
        assert_eq!(compatible, is_bss_compatible(&ret));
        ret
    }

}
