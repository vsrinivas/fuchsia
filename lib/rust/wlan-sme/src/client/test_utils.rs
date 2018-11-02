// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bytes::Bytes;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use wlan_rsn::{akm, cipher, rsne::{RsnCapabilities, Rsne}, suite_selector::OUI};

use crate::{InfoEvent, InfoStream, Ssid};

fn fake_bss_description(ssid: Ssid, rsn: Option<Vec<u8>>) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid: [0, 0, 0, 0, 0, 0],
        ssid,
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
        rsn,

        rcpi_dbmh: 0,
        rsni_dbh: 0,

        ht_cap: None,
        ht_op: None,
        vht_cap: None,
        vht_op: None,
        chan: fidl_mlme::WlanChan {
            primary: 1,
            secondary80: 0,
            cbw: fidl_mlme::Cbw::Cbw20,
        },
        rssi_dbm: 0,
    }
}

pub fn fake_bss_with_bssid(ssid: Ssid, bssid: [u8; 6]) -> fidl_mlme::BssDescription {
    fidl_mlme::BssDescription {
        bssid,
        .. fake_unprotected_bss_description(ssid)
    }
}

pub fn fake_unprotected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    fake_bss_description(ssid, None)
}

pub fn fake_protected_bss_description(ssid: Ssid) -> fidl_mlme::BssDescription {
    let a_rsne = make_rsne(Some(cipher::CCMP_128), vec![cipher::CCMP_128], vec![akm::PSK]);
    fake_bss_description(ssid, Some(rsne_as_bytes(a_rsne)))
}

pub fn fake_vht_bss_description() -> fidl_mlme::BssDescription {
    let bss = fake_bss_description(vec![], None);
    fidl_mlme::BssDescription {
        chan: fake_chan(36),
        ht_cap: Some(Box::new(fake_ht_capabilities())),
        ht_op: Some(Box::new(fake_ht_operation())),
        vht_cap: Some(Box::new(fake_vht_capabilities())),
        vht_op: Some(Box::new(fake_vht_operation())),
        .. bss
    }
}

pub fn fake_chan(primary :u8) -> fidl_mlme::WlanChan {
    fidl_mlme::WlanChan {
        primary,
        cbw: fidl_mlme::Cbw::Cbw20,
        secondary80: 0,
    }
}

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

pub fn expect_info_event(info_stream: &mut InfoStream, expected_event: InfoEvent) {
    if let Ok(Some(e)) = info_stream.try_next() {
        assert_eq!(e, expected_event);
    } else {
        panic!("expect event to InfoSink");
    }
}

fn make_cipher(suite_type: u8) -> cipher::Cipher {
    cipher::Cipher { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
}

fn make_akm(suite_type: u8) -> akm::Akm {
    akm::Akm { oui: Bytes::from(&OUI[..]), suite_type: suite_type }
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


fn fake_ht_cap_info() -> fidl_mlme::HtCapabilityInfo {
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

fn fake_ampdu_params() -> fidl_mlme::AmpduParams {
    fidl_mlme::AmpduParams {
        exponent: 0,
        min_start_spacing: fidl_mlme::MinMpduStartSpacing::NoRestrict as u8,
    }
}

fn fake_supported_mcs_set() -> fidl_mlme::SupportedMcsSet {
    fidl_mlme::SupportedMcsSet {
        rx_mcs_set: 0x01000000ff,
        rx_highest_rate: 0,
        tx_mcs_set_defined: true,
        tx_rx_diff: false,
        tx_max_ss: 1,
        tx_ueqm: false,
    }
}

fn fake_ht_ext_capabilities() -> fidl_mlme::HtExtCapabilities {
    fidl_mlme::HtExtCapabilities {
        pco: false,
        pco_transition: fidl_mlme::PcoTransitionTime::PcoReserved as u8,
        mcs_feedback: fidl_mlme::McsFeedback::McsNofeedback as u8,
        htc_ht_support: false,
        rd_responder: false,
    }
}

fn fake_txbf_capabilities() -> fidl_mlme::TxBfCapability {
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

fn fake_asel_capability() -> fidl_mlme::AselCapability {
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


fn fake_ht_op_info() -> fidl_mlme::HtOperationInfo {
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

fn fake_vht_capabilities_info() -> fidl_mlme::VhtCapabilitiesInfo {
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
        ext_nss_bw:2,
    }
}

fn fake_vht_mcs_nss() -> fidl_mlme::VhtMcsNss {
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

fn fake_basic_vht_mcs_nss() -> fidl_mlme::BasicVhtMcsNss {
    fidl_mlme::BasicVhtMcsNss {
        max_mcs: [fidl_mlme::VhtMcs::Set0To9 as u8; 8],
    }
}

pub fn fake_5ghz_band_capabilities() -> fidl_mlme::BandCapabilities {
    fidl_mlme::BandCapabilities {
        band_id: fidl_mlme::Band::WlanBand5Ghz,
        basic_rates: vec![],
        base_frequency: 5000,
        channels: vec![],
        cap: fake_capability_info(),
        ht_cap: None,
        vht_cap: None,
    }
}
