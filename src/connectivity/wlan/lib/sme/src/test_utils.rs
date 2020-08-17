// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::MacAddr,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    std::convert::TryInto,
    wlan_common::{
        channel::{Cbw, Phy},
        ie::{
            rsn::{
                akm::{self, Akm},
                cipher::{self, Cipher},
                rsne::{RsnCapabilities, Rsne},
                OUI,
            },
            wpa::WpaIe,
            *,
        },
        mac::CapabilityInfo,
        organization::Oui,
        RadioConfig,
    },
    wlan_rsn::key::{gtk::Gtk, ptk::Ptk},
    zerocopy::AsBytes,
};

pub fn make_rsne(data: Option<u8>, pairwise: Vec<u8>, akms: Vec<u8>) -> Rsne {
    let a_rsne = Rsne {
        version: 1,
        group_data_cipher_suite: data.map(|t| make_cipher(t)),
        pairwise_cipher_suites: pairwise.into_iter().map(|t| make_cipher(t)).collect(),
        akm_suites: akms.into_iter().map(|t| make_akm(t)).collect(),
        ..Default::default()
    };
    a_rsne
}

pub fn wpa2_psk_ccmp_rsne_with_caps(caps: RsnCapabilities) -> Rsne {
    let a_rsne = Rsne {
        version: 1,
        group_data_cipher_suite: Some(make_cipher(cipher::CCMP_128)),
        pairwise_cipher_suites: vec![make_cipher(cipher::CCMP_128)],
        akm_suites: vec![make_akm(akm::PSK)],
        rsn_capabilities: Some(caps),
        ..Default::default()
    };
    a_rsne
}

pub fn wpa3_mixed_psk_ccmp_rsne() -> Rsne {
    Rsne {
        version: 1,
        group_data_cipher_suite: Some(make_cipher(cipher::CCMP_128)),
        pairwise_cipher_suites: vec![make_cipher(cipher::CCMP_128)],
        akm_suites: vec![make_akm(akm::SAE), make_akm(akm::PSK)],
        rsn_capabilities: Some(RsnCapabilities(0).with_mgmt_frame_protection_cap(true)),
        ..Default::default()
    }
}

pub fn wpa3_psk_ccmp_rsne() -> Rsne {
    Rsne {
        version: 1,
        group_data_cipher_suite: Some(make_cipher(cipher::CCMP_128)),
        pairwise_cipher_suites: vec![make_cipher(cipher::CCMP_128)],
        akm_suites: vec![make_akm(akm::SAE)],
        rsn_capabilities: Some(
            RsnCapabilities(0)
                .with_mgmt_frame_protection_cap(true)
                .with_mgmt_frame_protection_req(true),
        ),
        ..Default::default()
    }
}

pub fn make_wpa1_ie() -> WpaIe {
    WpaIe {
        multicast_cipher: make_cipher(cipher::TKIP),
        unicast_cipher_list: vec![make_cipher(cipher::TKIP)],
        akm_list: vec![make_akm(akm::PSK)],
    }
}

pub fn rsne_as_bytes(s_rsne: Rsne) -> Vec<u8> {
    let mut buf = Vec::with_capacity(s_rsne.len());
    s_rsne.write_into(&mut buf).expect("error writing RSNE into buffer");
    buf
}

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: OUI, suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: OUI, suite_type }
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
    make_akm(akm::PSK)
}

pub fn wpa1_akm() -> Akm {
    akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }
}

pub fn cipher() -> Cipher {
    make_cipher(cipher::CCMP_128)
}

pub fn wpa1_cipher() -> Cipher {
    cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }
}

pub fn fake_device_info(mac_addr: MacAddr) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        mac_addr,
        role: fidl_mlme::MacRole::Client,
        bands: vec![
            fake_2ghz_band_capabilities_vht(),
            fake_band_capabilities_5ghz_vht(ChanWidthSet::TWENTY_FORTY),
        ],
        driver_features: vec![fidl_common::DriverFeature::TempSoftmac],
        qos_capable: true,
    }
}

pub fn fake_device_info_ht(chanwidth: ChanWidthSet) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        bands: vec![fake_5ghz_band_capabilities_ht_cbw(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn fake_device_info_vht(chanwidth: ChanWidthSet) -> fidl_mlme::DeviceInfo {
    fidl_mlme::DeviceInfo {
        bands: vec![fake_band_capabilities_5ghz_vht(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn fake_5ghz_band_capabilities_ht_cbw(chanwidth: ChanWidthSet) -> fidl_mlme::BandCapabilities {
    let bc = fake_5ghz_band_capabilities();
    fidl_mlme::BandCapabilities {
        ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(chanwidth).as_bytes().try_into().unwrap(),
        })),
        ..bc
    }
}

pub fn fake_band_capabilities_5ghz_vht(chanwidth: ChanWidthSet) -> fidl_mlme::BandCapabilities {
    let bc = fake_5ghz_band_capabilities();
    fidl_mlme::BandCapabilities {
        ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(chanwidth).as_bytes().try_into().unwrap(),
        })),
        vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
            bytes: fake_vht_capabilities().as_bytes().try_into().unwrap(),
        })),
        ..bc
    }
}

pub fn fake_overrider(phy: fidl_common::Phy, cbw: fidl_common::Cbw) -> RadioConfig {
    RadioConfig {
        phy: Some(Phy::from_fidl(phy)),
        cbw: Some(Cbw::from_fidl(cbw, 0)),
        primary_chan: None,
    }
}

pub fn fake_ht_capabilities_cbw(chanwidth: ChanWidthSet) -> HtCapabilities {
    let mut ht_cap = fake_ht_capabilities();
    ht_cap.ht_cap_info = ht_cap.ht_cap_info.with_chan_width_set(chanwidth);
    ht_cap
}

pub fn fake_capability_info() -> CapabilityInfo {
    CapabilityInfo(0)
        .with_ess(false)
        .with_ibss(false)
        .with_cf_pollable(false)
        .with_cf_poll_req(false)
        .with_privacy(false)
        .with_short_preamble(true)
        .with_spectrum_mgmt(false)
        .with_qos(false)
        .with_short_slot_time(false)
        .with_apsd(false)
        .with_radio_measurement(false)
        .with_delayed_block_ack(false)
        .with_immediate_block_ack(false)
}

pub fn fake_5ghz_band_capabilities() -> fidl_mlme::BandCapabilities {
    fidl_mlme::BandCapabilities {
        band_id: fidl_common::Band::WlanBand5Ghz,
        rates: vec![0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        base_frequency: 5000,
        channels: vec![],
        cap: fake_capability_info().0,
        ht_cap: None,
        vht_cap: None,
    }
}

pub fn fake_2ghz_band_capabilities_vht() -> fidl_mlme::BandCapabilities {
    fidl_mlme::BandCapabilities {
        ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
            bytes: fake_ht_capabilities_cbw(ChanWidthSet::TWENTY_FORTY)
                .as_bytes()
                .try_into()
                .unwrap(),
        })),
        vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
            bytes: fake_vht_capabilities().as_bytes().try_into().unwrap(),
        })),
        ..fake_2ghz_band_capabilities()
    }
}

pub fn fake_2ghz_band_capabilities() -> fidl_mlme::BandCapabilities {
    fidl_mlme::BandCapabilities {
        band_id: fidl_common::Band::WlanBand2Ghz,
        rates: vec![0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c],
        base_frequency: 2407,
        channels: vec![],
        cap: fake_capability_info().0,
        ht_cap: None,
        vht_cap: None,
    }
}
