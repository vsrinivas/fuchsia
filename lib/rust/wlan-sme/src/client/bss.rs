// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_mlme::BssDescription;
use std::collections::HashMap;
use std::collections::hash_map::Entry;
use std::cmp::Ordering;
use std::hash::Hash;
use wlan_rsn::rsne;

use crate::Ssid;
use crate::clone_utils::clone_bss_desc;
use crate::client::Standard;
use super::rsn::is_rsn_compatible;

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
        ssid: bss.ssid.clone(),
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

pub fn get_best_bss(bss_list: &[BssDescription]) -> Option<&BssDescription> {
    bss_list.iter().max_by(|x, y| compare_bss(x, y))
}

pub fn group_networks(bss_set: &[BssDescription]) -> Vec<EssInfo> {
    let mut bss_by_ssid: HashMap<Ssid, Vec<BssDescription>> = HashMap::new();

    for bss in bss_set.iter() {
        match bss_by_ssid.entry(bss.ssid.clone()) {
            Entry::Vacant(e) => {
                e.insert(vec![clone_bss_desc(bss)]);
            },
            Entry::Occupied(mut e) => {
                e.get_mut().push(clone_bss_desc(bss));
            }
        };
    }

    bss_by_ssid.values()
        .filter_map(|bss_list| get_best_bss(bss_list))
        .map(|bss| EssInfo { best_bss: convert_bss_description(&bss) })
        .collect()
}

pub fn get_standard_map(bss_list: &Vec<BssDescription>) -> HashMap<Standard, usize> {
    get_info_map(bss_list, get_standard)
}

pub fn get_channel_map(bss_list: &Vec<BssDescription>) -> HashMap<u8, usize> {
    get_info_map(bss_list, |bss| bss.chan.primary)
}

fn get_info_map<F, T>(bss_list: &Vec<BssDescription>, f: F) -> HashMap<T, usize>
where
    T: Eq + Hash,
    F: Fn(&BssDescription) -> T,
{
    let mut info_map: HashMap<T, usize> = HashMap::new();
    for bss in bss_list {
        match info_map.entry(f(&bss)) {
            Entry::Vacant(e) => { e.insert(1); },
            Entry::Occupied(mut e) => {
                *e.get_mut() += 1;
            }
        }
    }
    info_map
}

fn get_standard(bss: &BssDescription) -> Standard {
    if bss.vht_cap.is_some() && bss.vht_op.is_some() {
        Standard::Ac
    } else if bss.ht_cap.is_some() && bss.ht_op.is_some() {
        Standard::N
    } else if bss.chan.primary >= 36 {
        Standard::A
    } else {
        // TODO(NET-1587): Differentiate between 802.11b and 802.11g
        Standard::G
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_mlme as fidl_mlme;
    use std::cmp::Ordering;

    use crate::client::test_utils::fake_bss_with_bssid;

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

    #[test]
    fn get_best_bss_empty_list() {
        assert!(get_best_bss(&vec![]).is_none());
    }

    #[test]
    fn get_best_bss_nonempty_list() {
        let bss1 = bss(-30, -10, false);
        let bss2 = bss(-20, -10, true);
        let bss3 = bss(-80, -80, true);
        let bss_list = vec![bss1, bss2, bss3];
        assert_eq!(get_best_bss(&bss_list), Some(&bss_list[1]));
    }

    #[test]
    fn group_networks_by_ssid() {
        let bss1 = fake_bss_with_bssid(b"foo".to_vec(), [1, 1, 1, 1, 1, 1]);
        let bss2 = fake_bss_with_bssid(b"bar".to_vec(), [2, 2, 2, 2, 2, 2]);
        let bss3 = fake_bss_with_bssid(b"foo".to_vec(), [3, 3, 3, 3, 3, 3]);
        let ess_list = group_networks(&vec![bss1, bss2, bss3]);

        let mut ssid_list = ess_list.into_iter().map(|ess| ess.best_bss.ssid).collect::<Vec<_>>();
        ssid_list.sort();
        assert_eq!(vec![b"bar".to_vec(), b"foo".to_vec()], ssid_list);
    }

    fn assert_bss_cmp(worse: &fidl_mlme::BssDescription, better: &fidl_mlme::BssDescription) {
        assert_eq!(Ordering::Less, compare_bss(worse, better));
        assert_eq!(Ordering::Greater, compare_bss(better, worse));
    }

    fn bss(_rssi_dbm: i8, _rcpi_dbmh: i16, compatible: bool) -> fidl_mlme::BssDescription {
        let ret = fidl_mlme::BssDescription {
            bssid: [0, 0, 0, 0, 0, 0],
            ssid: vec![],

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
            basic_rate_set: vec![],
            op_rate_set: vec![],
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
