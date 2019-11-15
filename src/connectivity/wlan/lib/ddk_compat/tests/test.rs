// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We link against the test data structs defined in test_data.cc, which will construct the DDK
// structs, in C++. We then check if the resulting values are expected, in Rust.

#![cfg(test)]

use wlan_ddk_compat::{ddk_hw_wlan_ieee80211, ddk_protocol_wlan_info};

// This links against the shared library built from test_data.cc.
#[link(name = "test_data")]
extern "C" {
    pub static test_wlan_channel: ddk_protocol_wlan_info::WlanChannel;
    pub static test_wlan_bss_config: ddk_protocol_wlan_info::WlanBssConfig;
    pub static test_ht_caps: ddk_hw_wlan_ieee80211::Ieee80211HtCapabilities;
    pub static test_vht_caps: ddk_hw_wlan_ieee80211::Ieee80211VhtCapabilities;
}

#[test]
fn wlan_channel() {
    assert_eq!(
        unsafe { test_wlan_channel },
        ddk_protocol_wlan_info::WlanChannel {
            primary: 1,
            cbw: ddk_protocol_wlan_info::WlanChannelBandwidth::_80P80,
            secondary80: 3,
        }
    )
}

#[test]
fn wlan_bss_config() {
    assert_eq!(
        unsafe { test_wlan_bss_config },
        ddk_protocol_wlan_info::WlanBssConfig {
            bssid: [1, 2, 3, 4, 5, 6],
            bss_type: ddk_protocol_wlan_info::WlanBssType::Personal,
            remote: true,
        }
    )
}

/// Subset of `assert_eq` that does not borrow values. Defined this because we need
/// to test packed structs where borrowing fields is UB.
macro_rules! assert_eq_simple {
    ($left:expr, $right:expr) => {{
        let left = $left;
        let right = $right;
        if left != right {
            panic!(
                r#"assertion failed: `(left == right)`
  left: `{:?}`,
 right: `{:?}`"#,
                left, right
            );
        }
    }};
}

#[test]
fn ht_caps() {
    let ht_caps = unsafe { test_ht_caps };
    assert_eq_simple!(ht_caps.ht_capability_info, 1);
    assert_eq_simple!(ht_caps.ampdu_params, 2);
    let mcs_set_fields = unsafe { ht_caps.supported_mcs_set.fields };
    assert_eq_simple!(mcs_set_fields.rx_mcs_head, 3);
    assert_eq_simple!(mcs_set_fields.rx_mcs_tail, 4);
    assert_eq_simple!(mcs_set_fields.tx_mcs, 5);
    assert_eq_simple!(ht_caps.ext_capabilities, 6);
    assert_eq_simple!(ht_caps.tx_beamforming_capabilities, 7);
    assert_eq_simple!(ht_caps.asel_capabilities, 255);
}

#[test]
fn vht_caps() {
    let vht_caps = unsafe { test_vht_caps };
    assert_eq_simple!(vht_caps.vht_capability_info, 1);
    assert_eq_simple!(vht_caps.supported_vht_mcs_and_nss_set, 1 << 63);
}
