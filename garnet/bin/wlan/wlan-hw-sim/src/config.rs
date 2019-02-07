// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO(porce): Rename the aliases as fidl_*
    fidl_fuchsia_wlan_common as wlan_common,
    fidl_fuchsia_wlan_device as wlan_device,
    fidl_fuchsia_wlan_mlme as wlan_mlme,
    fidl_fuchsia_wlan_tap as wlantap,
};

pub fn create_wlantap_config(
    hw_mac_address: [u8; 6],
    mac_role: wlan_device::MacRole,
) -> wlantap::WlantapPhyConfig {
    use fidl_fuchsia_wlan_common::DriverFeature;
    use fidl_fuchsia_wlan_device::SupportedPhy;
    wlantap::WlantapPhyConfig {
        phy_info: wlan_device::PhyInfo {
            id: 0,
            dev_path: None,
            hw_mac_address,
            supported_phys: vec![
                SupportedPhy::Dsss,
                SupportedPhy::Cck,
                SupportedPhy::Ofdm,
                SupportedPhy::Ht,
            ],
            driver_features: vec![DriverFeature::Synth, DriverFeature::TxStatusReport],
            mac_roles: vec![mac_role],
            caps: vec![],
            bands: vec![create_2_4_ghz_band_info()],
        },
        name: String::from("wlantap0"),
        quiet: false,
    }
}

fn create_2_4_ghz_band_info() -> wlan_device::BandInfo {
    wlan_device::BandInfo {
        band_id: wlan_common::Band::WlanBand2Ghz,
        ht_caps: Some(Box::new(wlan_mlme::HtCapabilities {
            ht_cap_info: wlan_mlme::HtCapabilityInfo {
                ldpc_coding_cap: false,
                chan_width_set: wlan_mlme::ChanWidthSet::TwentyForty as u8,
                sm_power_save: wlan_mlme::SmPowerSave::Disabled as u8,
                greenfield: true,
                short_gi_20: true,
                short_gi_40: true,
                tx_stbc: true,
                rx_stbc: 1,
                delayed_block_ack: false,
                max_amsdu_len: wlan_mlme::MaxAmsduLen::Octets3839 as u8,
                dsss_in_40: false,
                intolerant_40: false,
                lsig_txop_protect: false,
            },
            ampdu_params: wlan_mlme::AmpduParams {
                exponent: 0,
                min_start_spacing: wlan_mlme::MinMpduStartSpacing::NoRestrict as u8,
            },
            mcs_set: wlan_mlme::SupportedMcsSet {
                rx_mcs_set: 0x01000000ff,
                rx_highest_rate: 0,
                tx_mcs_set_defined: true,
                tx_rx_diff: false,
                tx_max_ss: 1,
                tx_ueqm: false,
            },
            ht_ext_cap: wlan_mlme::HtExtCapabilities {
                pco: false,
                pco_transition: wlan_mlme::PcoTransitionTime::PcoReserved as u8,
                mcs_feedback: wlan_mlme::McsFeedback::McsNofeedback as u8,
                htc_ht_support: false,
                rd_responder: false,
            },
            txbf_cap: wlan_mlme::TxBfCapability {
                implicit_rx: false,
                rx_stag_sounding: false,
                tx_stag_sounding: false,
                rx_ndp: false,
                tx_ndp: false,
                implicit: false,
                calibration: wlan_mlme::Calibration::CalibrationNone as u8,
                csi: false,
                noncomp_steering: false,
                comp_steering: false,
                csi_feedback: wlan_mlme::Feedback::FeedbackNone as u8,
                noncomp_feedback: wlan_mlme::Feedback::FeedbackNone as u8,
                comp_feedback: wlan_mlme::Feedback::FeedbackNone as u8,
                min_grouping: wlan_mlme::MinGroup::MinGroupOne as u8,
                csi_antennas: 1,
                noncomp_steering_ants: 1,
                comp_steering_ants: 1,
                csi_rows: 1,
                chan_estimation: 1,
            },
            asel_cap: wlan_mlme::AselCapability {
                asel: false,
                csi_feedback_tx_asel: false,
                ant_idx_feedback_tx_asel: false,
                explicit_csi_feedback: false,
                antenna_idx_feedback: false,
                rx_asel: false,
                tx_sounding_ppdu: false,
            },
        })),
        vht_caps: None,
        basic_rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
        supported_channels: wlan_device::ChannelList {
            base_freq: 2407,
            channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
        },
    }
}
