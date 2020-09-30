// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        ie::{
            rsn::{
                akm::{Akm, PSK},
                cipher::{Cipher, TKIP},
                rsne::Rsne,
            },
            wpa::WpaIe,
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
        primary_chan: 36,
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
    let mut wpa = WpaIe::default();
    wpa.unicast_cipher_list.push(Cipher { oui: Oui::MSFT, suite_type: TKIP });
    wpa.akm_list.push(Akm { oui: Oui::MSFT, suite_type: PSK });
    wpa
}

pub fn get_rsn_ie_bytes(rsne: &Rsne) -> Vec<u8> {
    let mut buf = Vec::with_capacity(rsne.len());
    rsne.write_into(&mut buf).expect("error writing RSNE into buffer");
    buf
}
