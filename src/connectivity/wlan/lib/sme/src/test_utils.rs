// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    futures::channel::mpsc,
    ieee80211::MacAddr,
    wlan_common::{
        ie::{
            rsn::{
                akm::{self, Akm, AKM_PSK},
                cipher::{self, Cipher, CIPHER_CCMP_128, CIPHER_TKIP},
            },
            wpa::WpaIe,
            *,
        },
        organization::Oui,
        test_utils::fake_capabilities::{
            fake_2ghz_band_capability_vht, fake_5ghz_band_capability_ht_cbw,
            fake_5ghz_band_capability_vht,
        },
    },
    wlan_rsn::key::{gtk::Gtk, ptk::Ptk},
};

pub fn make_wpa1_ie() -> WpaIe {
    WpaIe {
        multicast_cipher: CIPHER_TKIP,
        unicast_cipher_list: vec![CIPHER_TKIP],
        akm_list: vec![AKM_PSK],
    }
}

pub fn eapol_key_frame() -> eapol::KeyFrameBuf {
    // Content doesn't matter; we just need a valid EAPOL key frame to test our code path
    let nonce: [u8; 32] = [
        0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79, 0xfe, 0xc3, 0xb9, 0xa9,
        0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25, 0xf8, 0xc7, 0xca, 0x55, 0x86,
        0xbc, 0xda,
    ];
    let iv = [0u8; 16];
    let data: Vec<u8> = vec![0x01, 0x02, 0x03];
    let new_frame = eapol::KeyFrameTx::new(
        eapol::ProtocolVersion::IEEE802DOT1X2001,
        eapol::KeyFrameFields::new(
            eapol::KeyDescriptor::IEEE802DOT11,
            eapol::KeyInformation(0x008a),
            16,
            1,
            nonce,
            iv,
            0,
        ),
        data,
        16,
    )
    .serialize();
    new_frame.finalize_without_mic().expect("failed to construct dummy eapol keyframe")
}

pub fn ptk() -> Ptk {
    let mut ptk_bytes = vec![];
    // Using different values for KCK, KEK,, and TK to detect potential mistakes. This ensures
    // that if our code, for example, mistakenly uses KCK instead of TK, test would fail.
    ptk_bytes.extend(vec![0xAAu8; akm().kck_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xBBu8; akm().kek_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xCCu8; cipher().tk_bytes().unwrap()]);
    Ptk::from_ptk(ptk_bytes, &akm(), cipher()).expect("expect valid ptk")
}

pub fn wpa1_ptk() -> Ptk {
    let mut ptk_bytes = vec![];
    // Using different values for KCK, KEK,, and TK to detect potential mistakes. This ensures
    // that if our code, for example, mistakenly uses KCK instead of TK, test would fail.
    let akm = wpa1_akm();
    let cipher = wpa1_cipher();
    ptk_bytes.extend(vec![0xAAu8; akm.kck_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xBBu8; akm.kek_bytes().unwrap() as usize]);
    ptk_bytes.extend(vec![0xCCu8; cipher.tk_bytes().unwrap()]);
    Ptk::from_ptk(ptk_bytes, &akm, cipher).expect("expect valid ptk")
}

pub fn gtk_bytes() -> Vec<u8> {
    vec![0xDD; 16]
}

pub fn gtk() -> Gtk {
    Gtk::from_gtk(gtk_bytes(), 2, cipher(), 0).expect("failed creating GTK")
}

pub fn wpa1_gtk_bytes() -> Vec<u8> {
    vec![0xDD; 32]
}

pub fn wpa1_gtk() -> Gtk {
    Gtk::from_gtk(wpa1_gtk_bytes(), 2, wpa1_cipher(), 0).expect("failed creating GTK")
}

pub fn akm() -> Akm {
    AKM_PSK
}

pub fn wpa1_akm() -> Akm {
    akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }
}

pub fn cipher() -> Cipher {
    CIPHER_CCMP_128
}

pub fn wpa1_cipher() -> Cipher {
    cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }
}

pub fn fake_device_info(sta_addr: MacAddr) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        sta_addr,
        role: fidl_common::WlanMacRole::Client,
        bands: vec![
            fake_2ghz_band_capability_vht(),
            fake_5ghz_band_capability_vht(ChanWidthSet::TWENTY_FORTY),
        ],
        driver_features: vec![
            fidl_common::DriverFeature::Mfp,
            fidl_common::DriverFeature::SaeSmeAuth,
            fidl_common::DriverFeature::TempSoftmac,
        ],
        softmac_hardware_capability: fidl_common::WlanSoftmacHardwareCapabilityBit::ShortPreamble
            .into_primitive(),
        qos_capable: true,
    }
}

pub fn fake_device_info_ht(chanwidth: ChanWidthSet) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        bands: vec![fake_5ghz_band_capability_ht_cbw(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn fake_device_info_vht(chanwidth: ChanWidthSet) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        bands: vec![fake_5ghz_band_capability_vht(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn create_inspect_persistence_channel() -> (mpsc::Sender<String>, mpsc::Receiver<String>) {
    const DEFAULT_BUFFER_SIZE: usize = 100; // arbitrary value
    mpsc::channel(DEFAULT_BUFFER_SIZE)
}
