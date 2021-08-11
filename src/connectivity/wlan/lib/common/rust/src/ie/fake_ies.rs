// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        appendable::BufferTooSmall,
        ie::{
            rsn::{akm, cipher, rsne::Rsne},
            wpa::WpaIe,
            write_wpa1_ie,
            wsc::{ProbeRespWsc, WpsState},
            *,
        },
        organization::Oui,
    },
    std::convert::TryInto,
};

pub fn fake_ht_cap_chanwidth(chanwidth: ChanWidthSet) -> HtCapabilities {
    let mut ht_cap = fake_ht_capabilities();
    ht_cap.ht_cap_info = ht_cap.ht_cap_info.with_chan_width_set(chanwidth);
    ht_cap
}

pub fn fake_ht_op_sec_offset(secondary_offset: SecChanOffset) -> HtOperation {
    let mut ht_op = fake_ht_operation();
    let mut ht_op_info_head = ht_op.ht_op_info_head;
    ht_op_info_head.set_secondary_chan_offset(secondary_offset);
    ht_op.ht_op_info_head = ht_op_info_head;
    ht_op
}

fn fake_supported_mcs_set() -> SupportedMcsSet {
    SupportedMcsSet(0)
        .with_rx_mcs(RxMcsBitmask(0x01000000ff))
        .with_rx_highest_rate(0)
        .with_tx_set_defined(true)
        .with_tx_rx_diff(false)
        .with_tx_max_ss(NumSpatialStreams::from_human(1).unwrap())
        .with_tx_ueqm(false)
}

pub fn fake_ht_capabilities() -> HtCapabilities {
    HtCapabilities {
        ht_cap_info: HtCapabilityInfo(0)
            .with_chan_width_set(ChanWidthSet::TWENTY_FORTY)
            .with_sm_power_save(SmPowerSave::DISABLED)
            .with_greenfield(true)
            .with_short_gi_20(true)
            .with_short_gi_40(true)
            .with_tx_stbc(true)
            .with_rx_stbc(1),
        ampdu_params: AmpduParams(0),
        mcs_set: fake_supported_mcs_set(),
        ht_ext_cap: HtExtCapabilities(0),
        txbf_cap: TxBfCapability(0)
            .with_csi_antennas(NumAntennas::from_human(1).unwrap())
            .with_noncomp_steering_ants(NumAntennas::from_human(1).unwrap())
            .with_comp_steering_ants(NumAntennas::from_human(1).unwrap())
            .with_csi_rows(NumCsiRows::from_human(1).unwrap())
            .with_chan_estimation(NumSpaceTimeStreams::from_human(1).unwrap()),
        asel_cap: AselCapability(0),
    }
}

pub fn fake_ht_operation() -> HtOperation {
    HtOperation {
        primary_channel: 36,
        ht_op_info_head: HtOpInfoHead(0)
            .with_secondary_chan_offset(SecChanOffset::SECONDARY_ABOVE)
            .with_sta_chan_width(StaChanWidth::ANY)
            .with_rifs_mode_permitted(false)
            .with_ht_protection(HtProtection::NONE)
            .with_nongreenfield_present(true)
            .with_obss_non_ht_stas_present(true)
            .with_center_freq_seg2(0)
            .with_dual_beacon(false)
            .with_dual_cts_protection(false),
        ht_op_info_tail: HtOpInfoTail(0)
            .with_stbc_beacon(false)
            .with_lsig_txop_protection(false)
            .with_pco_active(false)
            .with_pco_phase(PcoPhase::TWENTY_MHZ),
        basic_ht_mcs_set: fake_supported_mcs_set(),
    }
}

pub fn fake_vht_capabilities() -> VhtCapabilities {
    VhtCapabilities {
        vht_cap_info: VhtCapabilitiesInfo(0)
            .with_max_mpdu_len(MaxMpduLen::OCTECTS_7991)
            .with_supported_cbw_set(0)
            .with_rx_ldpc(true)
            .with_sgi_cbw80(true)
            .with_sgi_cbw160(false)
            .with_tx_stbc(true)
            .with_rx_stbc(2)
            .with_su_bfer(false)
            .with_su_bfee(false)
            .with_bfee_sts(0)
            .with_num_sounding(0)
            .with_mu_bfer(false)
            .with_mu_bfee(false)
            .with_txop_ps(false)
            .with_htc_vht(false)
            .with_max_ampdu_exponent(MaxAmpduExponent(2))
            .with_link_adapt(VhtLinkAdaptation::NO_FEEDBACK)
            .with_rx_ant_pattern(true)
            .with_tx_ant_pattern(true)
            .with_ext_nss_bw(2),
        vht_mcs_nss: VhtMcsNssSet(0)
            .with_rx_max_mcs_raw(0x0001020300010203)
            .with_rx_max_data_rate(867)
            .with_max_nsts(2)
            .with_tx_max_mcs_raw(0x0001020300010203)
            .with_tx_max_data_rate(867)
            .with_ext_nss_bw(false),
    }
}

pub fn fake_vht_op_cbw(vht_cbw: VhtChannelBandwidth) -> VhtOperation {
    VhtOperation { vht_cbw, ..fake_vht_operation() }
}

pub fn fake_vht_operation() -> VhtOperation {
    VhtOperation {
        vht_cbw: VhtChannelBandwidth::CBW_80_160_80P80,
        center_freq_seg0: 42,
        center_freq_seg1: 0,
        basic_mcs_nss: VhtMcsNssMap(0x1b1b),
    }
}

pub fn fake_ht_cap_bytes() -> [u8; std::mem::size_of::<HtCapabilities>()] {
    // Safe to unwrap because the size matches the IE.
    fake_ht_capabilities().as_bytes().try_into().unwrap()
}

pub fn fake_ht_op_bytes() -> [u8; std::mem::size_of::<HtOperation>()] {
    // Safe to unwrap because the size matches the IE.
    fake_ht_operation().as_bytes().try_into().unwrap()
}

pub fn fake_vht_cap_bytes() -> [u8; std::mem::size_of::<VhtCapabilities>()] {
    // Safe to unwrap because the size matches the IE.
    fake_vht_capabilities().as_bytes().try_into().unwrap()
}

pub fn fake_vht_op_bytes() -> [u8; std::mem::size_of::<VhtOperation>()] {
    // Safe to unwrap because the size matches the IE.
    fake_vht_operation().as_bytes().try_into().unwrap()
}

pub fn fake_wpa_ie() -> WpaIe {
    WpaIe {
        unicast_cipher_list: vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP }],
        akm_list: vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }],
        multicast_cipher: cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::TKIP },
    }
}

pub fn get_vendor_ie_bytes_for_wpa_ie(wpa_ie: &WpaIe) -> Result<Vec<u8>, BufferTooSmall> {
    let mut buf = vec![];
    write_wpa1_ie(&mut buf, &wpa_ie).map(|_| buf)
}

pub fn get_rsn_ie_bytes(rsne: &Rsne) -> Vec<u8> {
    let mut buf = Vec::with_capacity(rsne.len());
    rsne.write_into(&mut buf).expect("error writing RSNE into buffer");
    buf
}

// The returned ProbeRespWsc corresponds to the bytes returned by
// fake_probe_resp_wsc_ie_bytes().
pub fn fake_probe_resp_wsc_ie() -> ProbeRespWsc {
    ProbeRespWsc {
        version: 0x10,
        wps_state: WpsState::CONFIGURED,
        ap_setup_locked: true,
        selected_reg: false,
        selected_reg_config_methods: None,
        response_type: 0x03,
        uuid_e: [
            0x3b, 0x3b, 0xe3, 0x66, 0x80, 0x84, 0x4b, 0x03, 0xbb, 0x66, 0x45, 0x2a, 0xf3, 0x00,
            0x59, 0x22,
        ],
        manufacturer: b"ASUSTek Computer Inc.".to_vec(),
        model_name: b"RT-AC58U".to_vec(),
        model_number: b"123".to_vec(),
        serial_number: b"12345".to_vec(),
        primary_device_type: [0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01],
        device_name: b"ASUS Router".to_vec(),
        config_methods: [0x20, 0x0c],
        rf_bands: None,
        vendor_ext: vec![0x00, 0x37, 0x2a, 0x00, 0x01, 0x20],
    }
}

// The bytes returned correspond to the ProbeRespWsc returned by
// fake_probe_resp_wsc_ie().
pub fn fake_probe_resp_wsc_ie_bytes() -> Vec<u8> {
    #[rustfmt::skip]
    let bytes = vec![
        0x10, 0x4a, 0x00, 0x01, 0x10, // Version
        0x10, 0x44, 0x00, 0x01, 0x02, // WiFi Protected Setup State
        0x10, 0x57, 0x00, 0x01, 0x01, // AP Setup Locked
        0x10, 0x3b, 0x00, 0x01, 0x03, // Response Type
        // UUID-E
        0x10, 0x47, 0x00, 0x10,
        0x3b, 0x3b, 0xe3, 0x66, 0x80, 0x84, 0x4b, 0x03,
        0xbb, 0x66, 0x45, 0x2a, 0xf3, 0x00, 0x59, 0x22,
        // Manufacturer
        0x10, 0x21, 0x00, 0x15,
        0x41, 0x53, 0x55, 0x53, 0x54, 0x65, 0x6b, 0x20, 0x43, 0x6f, 0x6d, 0x70,
        0x75, 0x74, 0x65, 0x72, 0x20, 0x49, 0x6e, 0x63, 0x2e,
        // Model name
        0x10, 0x23, 0x00, 0x08, 0x52, 0x54, 0x2d, 0x41, 0x43, 0x35, 0x38, 0x55,
        // Model number
        0x10, 0x24, 0x00, 0x03, 0x31, 0x32, 0x33,
        // Serial number
        0x10, 0x42, 0x00, 0x05, 0x31, 0x32, 0x33, 0x34, 0x35,
        // Primary device type
        0x10, 0x54, 0x00, 0x08, 0x00, 0x06, 0x00, 0x50, 0xf2, 0x04, 0x00, 0x01,
        // Device name
        0x10, 0x11, 0x00, 0x0b,
        0x41, 0x53, 0x55, 0x53, 0x20, 0x52, 0x6f, 0x75, 0x74, 0x65, 0x72,
        // Config methods
        0x10, 0x08, 0x00, 0x02, 0x20, 0x0c,
        // Vendor extension
        0x10, 0x49, 0x00, 0x06, 0x00, 0x37, 0x2a, 0x00, 0x01, 0x20,
    ];
    bytes
}

pub fn get_vendor_ie_bytes_for_wsc_ie(wsc_ie_bytes: &[u8]) -> Result<Vec<u8>, BufferTooSmall> {
    let mut buf = vec![];
    write_wsc_ie(&mut buf, &wsc_ie_bytes).map(|_| buf)
}

pub fn fake_wmm_param() -> WmmParam {
    WmmParam {
        wmm_info: WmmInfo(0).with_ap_wmm_info(ApWmmInfo(0).with_uapsd(true)),
        _reserved: 0,
        ac_be_params: WmmAcParams {
            aci_aifsn: WmmAciAifsn(0).with_aifsn(3).with_aci(0),
            ecw_min_max: EcwMinMax(0).with_ecw_min(4).with_ecw_max(10),
            txop_limit: 0,
        },
        ac_bk_params: WmmAcParams {
            aci_aifsn: WmmAciAifsn(0).with_aifsn(7).with_aci(1),
            ecw_min_max: EcwMinMax(0).with_ecw_min(4).with_ecw_max(10),
            txop_limit: 0,
        },
        ac_vi_params: WmmAcParams {
            aci_aifsn: WmmAciAifsn(0).with_aifsn(2).with_aci(2),
            ecw_min_max: EcwMinMax(0).with_ecw_min(3).with_ecw_max(4),
            txop_limit: 94,
        },
        ac_vo_params: WmmAcParams {
            aci_aifsn: WmmAciAifsn(0).with_aifsn(2).with_aci(3),
            ecw_min_max: EcwMinMax(0).with_ecw_min(2).with_ecw_max(3),
            txop_limit: 47,
        },
    }
}
