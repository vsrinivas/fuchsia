// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie::rsn::{
        akm::AKM_PSK,
        cipher::{CIPHER_CCMP_128, CIPHER_TKIP},
        rsne::Rsne,
    },
    fidl_fuchsia_wlan_common as fidl_common,
};

pub fn fake_wpa2_a_rsne() -> Rsne {
    Rsne {
        group_data_cipher_suite: Some(CIPHER_CCMP_128),
        pairwise_cipher_suites: vec![CIPHER_CCMP_128, CIPHER_TKIP],
        akm_suites: vec![AKM_PSK],
        ..Default::default()
    }
}

static EMPTY_SECURITY_SUPPORT: fidl_common::SecuritySupport = fidl_common::SecuritySupport {
    mfp: fidl_common::MfpFeature { supported: false },
    sae: fidl_common::SaeFeature { driver_handler_supported: false, sme_handler_supported: true },
};

pub fn fake_wpa2_s_rsne() -> Rsne {
    fake_wpa2_a_rsne()
        .derive_wpa2_s_rsne(&EMPTY_SECURITY_SUPPORT)
        .expect("Unable to derive supplicant RSNE")
}

pub fn fake_wpa3_a_rsne() -> Rsne {
    Rsne::wpa3_rsne()
}

pub fn fake_wpa3_s_rsne() -> Rsne {
    let mut security_support = EMPTY_SECURITY_SUPPORT;
    security_support.mfp.supported = true;
    fake_wpa3_a_rsne()
        .derive_wpa3_s_rsne(&security_support)
        .expect("Unable to derive supplicant RSNE")
}
