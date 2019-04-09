// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{DeviceInfo, MacAddr};
use bytes::Bytes;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use wlan_common::ie::rsn::{
    akm::{self, Akm},
    cipher::{self, Cipher},
    rsne::{RsnCapabilities, Rsne},
    OUI,
};
use wlan_common::{
    channel::{Cbw, Phy},
    RadioConfig,
};
use wlan_rsn::key::{gtk::Gtk, ptk::Ptk};

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

pub fn rsne_as_bytes(s_rsne: Rsne) -> Vec<u8> {
    let mut buf = Vec::with_capacity(s_rsne.len());
    s_rsne.as_bytes(&mut buf);
    buf
}

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: Bytes::from(&OUI[..]), suite_type }
}

pub fn eapol_key_frame_bytes() -> Vec<u8> {
    // Content doesn't matter; we just need a valid EAPOL key frame to test our code path
    vec![
        0x01, 0x03, 0x00, 0x5f, 0x02, 0x00, 0x8a, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x39, 0x5c, 0xc7, 0x6e, 0x1a, 0xe9, 0x9f, 0xa0, 0xb1, 0x22, 0x79, 0xfe, 0xc3,
        0xb9, 0xa9, 0x9e, 0x1d, 0x9a, 0x21, 0xb8, 0x47, 0x51, 0x38, 0x98, 0x25, 0xf8, 0xc7, 0xca,
        0x55, 0x86, 0xbc, 0xda, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x01, 0x02, 0x03,
    ]
}

pub fn eapol_key_frame() -> eapol::KeyFrame {
    eapol::key_frame_from_bytes(&eapol_key_frame_bytes(), 16)
        .to_full_result()
        .expect("expect valid eapol key frame")
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

pub fn gtk_bytes() -> Vec<u8> {
    vec![0xDD; 16]
}

pub fn gtk() -> Gtk {
    Gtk::from_gtk(gtk_bytes(), 2, cipher()).expect("failed creating GTK")
}

pub fn akm() -> Akm {
    Akm { oui: Bytes::from(&OUI[..]), suite_type: akm::PSK }
}

pub fn cipher() -> Cipher {
    Cipher { oui: Bytes::from(&OUI[..]), suite_type: cipher::CCMP_128 }
}

pub fn fake_ht_cap_chanwidth(chanwidth: fidl_mlme::ChanWidthSet) -> fidl_mlme::HtCapabilities {
    let mut ht_cap = fake_ht_capabilities();
    ht_cap.ht_cap_info.chan_width_set = chanwidth as u8;
    ht_cap
}

pub fn fake_ht_op_sec_offset(secondary_offset: fidl_mlme::SecChanOffset) -> fidl_mlme::HtOperation {
    let mut ht_op = fake_ht_operation();
    ht_op.ht_op_info.secondary_chan_offset = secondary_offset as u8;
    ht_op
}

pub fn fake_vht_op_cbw(cbw: fidl_mlme::VhtCbw) -> fidl_mlme::VhtOperation {
    fidl_mlme::VhtOperation { vht_cbw: cbw as u8, ..fake_vht_operation() }
}

pub fn fake_device_info(addr: MacAddr) -> DeviceInfo {
    DeviceInfo { addr, bands: vec![], driver_features: vec![] }
}

pub fn fake_device_info_ht(chanwidth: fidl_mlme::ChanWidthSet) -> DeviceInfo {
    DeviceInfo {
        bands: vec![fake_5ghz_band_capabilities_ht_cbw(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn fake_device_info_vht(chanwidth: fidl_mlme::ChanWidthSet) -> DeviceInfo {
    DeviceInfo {
        bands: vec![fake_band_capabilities_5ghz_vht(chanwidth)],
        ..fake_device_info([0; 6])
    }
}

pub fn fake_5ghz_band_capabilities_ht_cbw(
    chanwidth: fidl_mlme::ChanWidthSet,
) -> fidl_mlme::BandCapabilities {
    let bc = fake_5ghz_band_capabilities();
    fidl_mlme::BandCapabilities {
        ht_cap: Some(Box::new(fake_ht_capabilities_cbw(chanwidth))),
        ..bc
    }
}

pub fn fake_band_capabilities_5ghz_vht(
    chanwidth: fidl_mlme::ChanWidthSet,
) -> fidl_mlme::BandCapabilities {
    let bc = fake_5ghz_band_capabilities();
    fidl_mlme::BandCapabilities {
        ht_cap: Some(Box::new(fake_ht_capabilities_cbw(chanwidth))),
        vht_cap: Some(Box::new(fake_vht_capabilities())),
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

pub fn fake_ht_capabilities_cbw(chanwidth: fidl_mlme::ChanWidthSet) -> fidl_mlme::HtCapabilities {
    let mut ht_cap = fake_ht_capabilities();
    ht_cap.ht_cap_info.chan_width_set = chanwidth as u8;
    ht_cap
}

pub fn fake_capability_info() -> fidl_mlme::CapabilityInfo {
    fidl_mlme::CapabilityInfo {
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
    }
}

pub fn fake_ht_capabilities() -> fidl_mlme::HtCapabilities {
    fidl_mlme::HtCapabilities {
        ht_cap_info: fake_ht_cap_info(),
        ampdu_params: fake_ampdu_params(),
        mcs_set: fake_supported_mcs_set(),
        ht_ext_cap: fake_ht_ext_capabilities(),
        txbf_cap: fake_txbf_capabilities(),
        asel_cap: fake_asel_capability(),
    }
}

pub fn fake_ht_cap_info() -> fidl_mlme::HtCapabilityInfo {
    fidl_mlme::HtCapabilityInfo {
        ldpc_coding_cap: false,
        chan_width_set: fidl_mlme::ChanWidthSet::TwentyForty as u8,
        sm_power_save: fidl_mlme::SmPowerSave::Disabled as u8,
        greenfield: true,
        short_gi_20: true,
        short_gi_40: true,
        tx_stbc: true,
        rx_stbc: 1,
        delayed_block_ack: false,
        max_amsdu_len: fidl_mlme::MaxAmsduLen::Octets3839 as u8,
        dsss_in_40: false,
        intolerant_40: false,
        lsig_txop_protect: false,
    }
}

pub fn fake_ampdu_params() -> fidl_mlme::AmpduParams {
    fidl_mlme::AmpduParams {
        exponent: 0,
        min_start_spacing: fidl_mlme::MinMpduStartSpacing::NoRestrict as u8,
    }
}

pub fn fake_supported_mcs_set() -> fidl_mlme::SupportedMcsSet {
    fidl_mlme::SupportedMcsSet {
        rx_mcs_set: 0x01000000ff,
        rx_highest_rate: 0,
        tx_mcs_set_defined: true,
        tx_rx_diff: false,
        tx_max_ss: 1,
        tx_ueqm: false,
    }
}

pub fn fake_ht_ext_capabilities() -> fidl_mlme::HtExtCapabilities {
    fidl_mlme::HtExtCapabilities {
        pco: false,
        pco_transition: fidl_mlme::PcoTransitionTime::PcoReserved as u8,
        mcs_feedback: fidl_mlme::McsFeedback::McsNofeedback as u8,
        htc_ht_support: false,
        rd_responder: false,
    }
}

pub fn fake_txbf_capabilities() -> fidl_mlme::TxBfCapability {
    fidl_mlme::TxBfCapability {
        implicit_rx: false,
        rx_stag_sounding: false,
        tx_stag_sounding: false,
        rx_ndp: false,
        tx_ndp: false,
        implicit: false,
        calibration: fidl_mlme::Calibration::CalibrationNone as u8,
        csi: false,
        noncomp_steering: false,
        comp_steering: false,
        csi_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
        noncomp_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
        comp_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
        min_grouping: fidl_mlme::MinGroup::MinGroupOne as u8,
        csi_antennas: 1,
        noncomp_steering_ants: 1,
        comp_steering_ants: 1,
        csi_rows: 1,
        chan_estimation: 1,
    }
}

pub fn fake_asel_capability() -> fidl_mlme::AselCapability {
    fidl_mlme::AselCapability {
        asel: false,
        csi_feedback_tx_asel: false,
        ant_idx_feedback_tx_asel: false,
        explicit_csi_feedback: false,
        antenna_idx_feedback: false,
        rx_asel: false,
        tx_sounding_ppdu: false,
    }
}

pub fn fake_ht_operation() -> fidl_mlme::HtOperation {
    fidl_mlme::HtOperation {
        primary_chan: 36,
        ht_op_info: fake_ht_op_info(),
        basic_mcs_set: fake_supported_mcs_set(),
    }
}

pub fn fake_ht_op_info() -> fidl_mlme::HtOperationInfo {
    fidl_mlme::HtOperationInfo {
        secondary_chan_offset: fidl_mlme::SecChanOffset::SecondaryAbove as u8,
        sta_chan_width: fidl_mlme::StaChanWidth::Any as u8,
        rifs_mode: false,
        ht_protect: fidl_mlme::HtProtect::None as u8,
        nongreenfield_present: true,
        obss_non_ht: true,
        center_freq_seg2: 0,
        dual_beacon: false,
        dual_cts_protect: false,
        stbc_beacon: false,
        lsig_txop_protect: false,
        pco_active: false,
        pco_phase: false,
    }
}

pub fn fake_vht_capabilities() -> fidl_mlme::VhtCapabilities {
    fidl_mlme::VhtCapabilities {
        vht_cap_info: fake_vht_capabilities_info(),
        vht_mcs_nss: fake_vht_mcs_nss(),
    }
}

pub fn fake_vht_capabilities_info() -> fidl_mlme::VhtCapabilitiesInfo {
    fidl_mlme::VhtCapabilitiesInfo {
        max_mpdu_len: fidl_mlme::MaxMpduLen::Octets7991 as u8,
        supported_cbw_set: 0,
        rx_ldpc: true,
        sgi_cbw80: true,
        sgi_cbw160: false,
        tx_stbc: true,
        rx_stbc: 2,
        su_bfer: false,
        su_bfee: false,
        bfee_sts: 0,
        num_sounding: 0,
        mu_bfer: false,
        mu_bfee: false,
        txop_ps: false,
        htc_vht: false,
        max_ampdu_exp: 2,
        link_adapt: fidl_mlme::VhtLinkAdaptation::NoFeedback as u8,
        rx_ant_pattern: true,
        tx_ant_pattern: true,
        ext_nss_bw: 2,
    }
}

pub fn fake_vht_mcs_nss() -> fidl_mlme::VhtMcsNss {
    fidl_mlme::VhtMcsNss {
        rx_max_mcs: [fidl_mlme::VhtMcs::Set0To9 as u8; 8],
        rx_max_data_rate: 867,
        max_nsts: 2,
        tx_max_mcs: [fidl_mlme::VhtMcs::Set0To9 as u8; 8],
        tx_max_data_rate: 867,
        ext_nss_bw: false,
    }
}

pub fn fake_vht_operation() -> fidl_mlme::VhtOperation {
    fidl_mlme::VhtOperation {
        vht_cbw: fidl_mlme::VhtCbw::Cbw8016080P80 as u8,
        center_freq_seg0: 42,
        center_freq_seg1: 0,
        basic_mcs: fake_basic_vht_mcs_nss(),
    }
}

pub fn fake_basic_vht_mcs_nss() -> fidl_mlme::BasicVhtMcsNss {
    fidl_mlme::BasicVhtMcsNss { max_mcs: [fidl_mlme::VhtMcs::Set0To9 as u8; 8] }
}

pub fn fake_5ghz_band_capabilities() -> fidl_mlme::BandCapabilities {
    fidl_mlme::BandCapabilities {
        band_id: fidl_common::Band::WlanBand5Ghz,
        basic_rates: vec![],
        base_frequency: 5000,
        channels: vec![],
        cap: fake_capability_info(),
        ht_cap: None,
        vht_cap: None,
    }
}
