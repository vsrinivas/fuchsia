// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Error},
    banjo_ddk_hw_wlan_wlaninfo as banjo_ddk_wlaninfo,
    banjo_fuchsia_hardware_wlan_info as banjo_wlan_info,
    banjo_fuchsia_hardware_wlan_mac as banjo_wlan_mac, banjo_fuchsia_wlan_common as banjo_common,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_mlme as fidl_mlme,
    ieee80211::Bssid,
    log::warn,
    wlan_common::{
        ie::{
            parse_ht_capabilities, parse_ht_operation, parse_vht_capabilities, parse_vht_operation,
        },
        mac::Aid,
    },
    zerocopy::AsBytes,
};

pub fn ddk_channel_from_fidl(fc: fidl_common::WlanChannel) -> banjo_common::WlanChannel {
    let cbw = match fc.cbw {
        fidl_common::ChannelBandwidth::Cbw20 => banjo_common::ChannelBandwidth::CBW20,
        fidl_common::ChannelBandwidth::Cbw40 => banjo_common::ChannelBandwidth::CBW40,
        fidl_common::ChannelBandwidth::Cbw40Below => banjo_common::ChannelBandwidth::CBW40BELOW,
        fidl_common::ChannelBandwidth::Cbw80 => banjo_common::ChannelBandwidth::CBW80,
        fidl_common::ChannelBandwidth::Cbw160 => banjo_common::ChannelBandwidth::CBW160,
        fidl_common::ChannelBandwidth::Cbw80P80 => banjo_common::ChannelBandwidth::CBW80P80,
    };
    banjo_common::WlanChannel { primary: fc.primary, cbw, secondary80: fc.secondary80 }
}

pub fn build_ddk_assoc_ctx(
    bssid: Bssid,
    aid: Aid,
    negotiated_capabilities: fidl_mlme::NegotiatedCapabilities,
    ht_op: Option<[u8; fidl_internal::HT_OP_LEN as usize]>,
    vht_op: Option<[u8; fidl_internal::VHT_OP_LEN as usize]>,
) -> banjo_wlan_info::WlanAssocCtx {
    let mut rates = [0; banjo_wlan_info::WLAN_MAC_MAX_RATES as usize];
    rates[..negotiated_capabilities.rates.len()].clone_from_slice(&negotiated_capabilities.rates);
    let has_ht_cap = negotiated_capabilities.ht_cap.is_some();
    let has_vht_cap = negotiated_capabilities.vht_cap.is_some();
    let phy = match (has_ht_cap, has_vht_cap) {
        (true, true) => banjo_wlan_info::WlanPhyType::VHT,
        (true, false) => banjo_wlan_info::WlanPhyType::HT,
        // It is invalid to have VHT without HT and SME would guarantee it does not happen.
        // But default to ERP nonetheless just to be safe.
        _ => banjo_wlan_info::WlanPhyType::ERP,
    };
    let ht_cap_bytes =
        negotiated_capabilities.ht_cap.map_or([0; fidl_internal::HT_CAP_LEN as usize], |h| h.bytes);
    let vht_cap_bytes = negotiated_capabilities
        .vht_cap
        .map_or([0; fidl_internal::VHT_CAP_LEN as usize], |v| v.bytes);
    let ht_op_bytes = ht_op.unwrap_or([0; fidl_internal::HT_OP_LEN as usize]);
    let vht_op_bytes = vht_op.unwrap_or([0; fidl_internal::VHT_OP_LEN as usize]);
    banjo_wlan_info::WlanAssocCtx {
        bssid: bssid.0,
        aid,
        // In the association request we sent out earlier, listen_interval is always set to 0,
        // indicating the client never enters power save mode.
        // TODO(fxbug.dev/42217): ath10k disregard this value and hard code it to 1.
        // It is working now but we may need to revisit.
        listen_interval: 0,
        phy,
        channel: ddk_channel_from_fidl(negotiated_capabilities.channel),
        // TODO(fxbug.dev/29325): QoS works with Aruba/Ubiquiti for BlockAck session but it may need to be
        // dynamically determined for each outgoing data frame.
        // TODO(fxbug.dev/43938): Derive QoS flag and WMM parameters from device info
        qos: has_ht_cap,
        wmm_params: blank_wmm_params(),

        rates_cnt: negotiated_capabilities.rates.len() as u16, // will not overflow as MAX_RATES_LEN is u8
        rates,
        capability_info: negotiated_capabilities.capability_info,
        // All the unwrap are safe because the size of the byte array follow wire format.
        has_ht_cap,
        ht_cap: { *parse_ht_capabilities(&ht_cap_bytes[..]).unwrap() }.into(),
        has_ht_op: ht_op.is_some(),
        ht_op: { *parse_ht_operation(&ht_op_bytes[..]).unwrap() }.into(),
        has_vht_cap,
        vht_cap: { *parse_vht_capabilities(&vht_cap_bytes[..]).unwrap() }.into(),
        has_vht_op: vht_op.is_some(),
        vht_op: { *parse_vht_operation(&vht_op_bytes[..]).unwrap() }.into(),
    }
}

pub fn get_rssi_dbm(rx_info: banjo_wlan_mac::WlanRxInfo) -> Option<i8> {
    match rx_info.valid_fields & banjo_wlan_info::WlanRxInfoValid::RSSI.0 != 0
        && rx_info.rssi_dbm != 0
    {
        true => Some(rx_info.rssi_dbm),
        false => None,
    }
}

pub fn blank_wmm_params() -> banjo_wlan_info::WlanWmmParams {
    banjo_wlan_info::WlanWmmParams {
        apsd: false,
        ac_be_params: blank_wmm_ac_params(),
        ac_bk_params: blank_wmm_ac_params(),
        ac_vi_params: blank_wmm_ac_params(),
        ac_vo_params: blank_wmm_ac_params(),
    }
}

fn blank_wmm_ac_params() -> banjo_wlan_info::WlanWmmAcParams {
    banjo_wlan_info::WlanWmmAcParams { ecw_min: 0, ecw_max: 0, aifsn: 0, txop_limit: 0, acm: false }
}

pub fn device_info_from_wlanmac_info(
    info: banjo_wlan_mac::WlanmacInfo,
) -> Result<fidl_mlme::DeviceInfo, Error> {
    let mac_addr = info.mac_addr;
    let role = match info.mac_role {
        banjo_ddk_wlaninfo::WlanInfoMacRole::CLIENT => fidl_mlme::MacRole::Client,
        banjo_ddk_wlaninfo::WlanInfoMacRole::AP => fidl_mlme::MacRole::Ap,
        banjo_ddk_wlaninfo::WlanInfoMacRole::MESH => fidl_mlme::MacRole::Mesh,
        other => bail!("Unknown WLAN MAC role: {:?}", other),
    };
    let cap = wlan_common::mac::CapabilityInfo::from(info.caps).0;
    let bands = info.bands[0..info.bands_count as usize]
        .to_vec()
        .into_iter()
        .map(|band_info| convert_ddk_band_info(band_info, cap))
        .collect();
    let driver_features = convert_driver_features(&info.driver_features);
    Ok(fidl_mlme::DeviceInfo { mac_addr, role, bands, driver_features, qos_capable: false })
}

fn convert_ddk_band_info(
    band_info: banjo_ddk_wlaninfo::WlanInfoBandInfo,
    capability_info: u16,
) -> fidl_mlme::BandCapabilities {
    let band_id = match band_info.band {
        banjo_ddk_wlaninfo::WlanInfoBand::TWO_GHZ => fidl_common::Band::WlanBand2Ghz,
        banjo_ddk_wlaninfo::WlanInfoBand::FIVE_GHZ => fidl_common::Band::WlanBand5Ghz,
        banjo_ddk_wlaninfo::WlanInfoBand::COUNT => fidl_common::Band::WlanBandCount,
        unknown => {
            warn!("Unexpected WLAN band: {:?}. Defaulting to WlanBandCount", unknown);
            fidl_common::Band::WlanBandCount
        }
    };
    let rates = band_info.rates.to_vec().into_iter().filter(|rate| *rate != 0).collect();
    let base_frequency = band_info.supported_channels.base_freq;
    let channels = band_info
        .supported_channels
        .channels
        .to_vec()
        .into_iter()
        .filter(|channel| *channel != 0)
        .collect();

    let ht_cap = if band_info.ht_supported {
        let caps = wlan_common::ie::HtCapabilities::from(band_info.ht_caps);
        let mut bytes = [0u8; 26];
        bytes.copy_from_slice(caps.as_bytes());
        Some(Box::new(fidl_internal::HtCapabilities { bytes }))
    } else {
        None
    };
    let vht_cap = if band_info.vht_supported {
        let caps = wlan_common::ie::VhtCapabilities::from(band_info.vht_caps);
        let mut bytes = [0u8; 12];
        bytes.copy_from_slice(caps.as_bytes());
        Some(Box::new(fidl_internal::VhtCapabilities { bytes }))
    } else {
        None
    };
    fidl_mlme::BandCapabilities {
        band_id,
        rates,
        base_frequency,
        channels,
        capability_info,
        ht_cap,
        vht_cap,
    }
}

fn convert_driver_features(
    features: &banjo_ddk_wlaninfo::WlanInfoDriverFeature,
) -> Vec<fidl_common::DriverFeature> {
    // Add features supported at the MLME level.
    let mut out = vec![
        fidl_common::DriverFeature::SaeSmeAuth,
        // TODO(fxbug.dev/41640): Remove this flag once FullMAC drivers no longer needs SME.
        // This flag tells SME that SoftMAC drivers need SME to derive association capabilities.
        fidl_common::DriverFeature::TempSoftmac,
    ];
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD).0 != 0 {
        out.push(fidl_common::DriverFeature::ScanOffload);
    }
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::RATE_SELECTION).0 != 0 {
        out.push(fidl_common::DriverFeature::RateSelection);
    }
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::SYNTH).0 != 0 {
        out.push(fidl_common::DriverFeature::Synth);
    }
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::TX_STATUS_REPORT).0 != 0 {
        out.push(fidl_common::DriverFeature::TxStatusReport);
    }
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::DFS).0 != 0 {
        out.push(fidl_common::DriverFeature::Dfs);
    }
    if (*features & banjo_ddk_wlaninfo::WlanInfoDriverFeature::PROBE_RESP_OFFLOAD).0 != 0 {
        out.push(fidl_common::DriverFeature::ProbeRespOffload);
    }
    out
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::device::fake_wlanmac_info, banjo_ddk_hw_wlan_ieee80211 as banjo_80211,
        std::convert::TryInto, wlan_common::ie, zerocopy::AsBytes,
    };

    #[test]
    fn assoc_ctx_construction_successful() {
        let ddk = build_ddk_assoc_ctx(
            Bssid([1, 2, 3, 4, 5, 6]),
            42,
            fidl_mlme::NegotiatedCapabilities {
                channel: fidl_common::WlanChannel {
                    primary: 149,
                    cbw: fidl_common::ChannelBandwidth::Cbw40,
                    secondary80: 42,
                },
                capability_info: 0x1234,
                rates: vec![111, 112, 113, 114, 115, 116, 117, 118, 119, 120],
                wmm_param: None,
                ht_cap: Some(Box::new(fidl_internal::HtCapabilities {
                    bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap(),
                })),
                vht_cap: Some(Box::new(fidl_internal::VhtCapabilities {
                    bytes: ie::fake_vht_capabilities().as_bytes().try_into().unwrap(),
                })),
            },
            Some(ie::fake_ht_operation().as_bytes().try_into().unwrap()),
            Some(ie::fake_vht_operation().as_bytes().try_into().unwrap()),
        );
        assert_eq!([1, 2, 3, 4, 5, 6], ddk.bssid);
        assert_eq!(42, ddk.aid);
        assert_eq!(0, ddk.listen_interval);
        assert_eq!(banjo_wlan_info::WlanPhyType::VHT, ddk.phy);
        assert_eq!(
            banjo_common::WlanChannel {
                primary: 149,
                cbw: banjo_common::ChannelBandwidth::CBW40,
                secondary80: 42
            },
            ddk.channel
        );
        assert_eq!(true, ddk.qos);

        assert_eq!(10, ddk.rates_cnt);
        assert_eq!([111, 112, 113, 114, 115, 116, 117, 118, 119, 120], ddk.rates[0..10]);
        assert_eq!(&[0; 253][..], &ddk.rates[10..]);

        assert_eq!(0x1234, ddk.capability_info);

        assert_eq!(true, ddk.has_ht_cap);
        let expected_ht_cap: banjo_80211::Ieee80211HtCapabilities =
            ie::fake_ht_capabilities().into();

        // PartialEq not derived because supported_mcs_set is a union. Compare fields individually.
        assert_eq!({ expected_ht_cap.ht_capability_info }, { ddk.ht_cap.ht_capability_info });
        assert_eq!({ expected_ht_cap.ampdu_params }, { ddk.ht_cap.ampdu_params });

        // A union. Compare both variants.
        unsafe {
            assert_eq!(expected_ht_cap.supported_mcs_set.bytes, ddk.ht_cap.supported_mcs_set.bytes);
            assert_eq!(
                expected_ht_cap.supported_mcs_set.fields,
                ddk.ht_cap.supported_mcs_set.fields
            );
        }

        assert_eq!({ expected_ht_cap.tx_beamforming_capabilities }, {
            ddk.ht_cap.tx_beamforming_capabilities
        });
        assert_eq!(expected_ht_cap.asel_capabilities, ddk.ht_cap.asel_capabilities);

        assert_eq!(true, ddk.has_ht_op);
        let expected_ht_op: banjo_wlan_info::WlanHtOp = ie::fake_ht_operation().into();
        assert_eq!(expected_ht_op, ddk.ht_op);

        assert_eq!(true, ddk.has_vht_cap);
        let expected_vht_cap: banjo_80211::Ieee80211VhtCapabilities =
            ie::fake_vht_capabilities().into();
        assert_eq!(expected_vht_cap, ddk.vht_cap);

        assert_eq!(true, ddk.has_vht_op);
        let expected_vht_op: banjo_wlan_info::WlanVhtOp = ie::fake_vht_operation().into();
        assert_eq!(expected_vht_op, ddk.vht_op);
    }

    fn empty_rx_info() -> banjo_wlan_mac::WlanRxInfo {
        banjo_wlan_mac::WlanRxInfo {
            rx_flags: 0,
            valid_fields: 0,
            phy: 0,
            data_rate: 0,
            channel: banjo_common::WlanChannel {
                primary: 0,
                cbw: banjo_common::ChannelBandwidth::CBW20,
                secondary80: 0,
            },
            mcs: 0,
            rssi_dbm: 0,
            snr_dbh: 0,
        }
    }

    #[test]
    fn test_get_rssi_dbm_field_not_valid() {
        let rx_info =
            banjo_wlan_mac::WlanRxInfo { valid_fields: 0, rssi_dbm: 20, ..empty_rx_info() };
        assert_eq!(get_rssi_dbm(rx_info), None);
    }

    #[test]
    fn test_get_rssi_dbm_zero_dbm() {
        let rx_info = banjo_wlan_mac::WlanRxInfo {
            valid_fields: banjo_wlan_info::WlanRxInfoValid::RSSI.0,
            rssi_dbm: 0,
            ..empty_rx_info()
        };
        assert_eq!(get_rssi_dbm(rx_info), None);
    }

    #[test]
    fn test_get_rssi_dbm_all_good() {
        let rx_info = banjo_wlan_mac::WlanRxInfo {
            valid_fields: banjo_wlan_info::WlanRxInfoValid::RSSI.0,
            rssi_dbm: 20,
            ..empty_rx_info()
        };
        assert_eq!(get_rssi_dbm(rx_info), Some(20));
    }

    #[test]
    fn test_convert_band_info() {
        let wlanmac_info = fake_wlanmac_info();
        let band0 = convert_ddk_band_info(wlanmac_info.bands[0], 10);
        assert_eq!(band0.band_id, fidl_common::Band::WlanBand2Ghz);
        assert_eq!(band0.rates, vec![12, 24, 48, 54, 96, 108]);
        assert_eq!(band0.base_frequency, 2407);
        assert_eq!(band0.channels, vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]);
        assert_eq!(band0.capability_info, 10);
        assert!(band0.ht_cap.is_some());
        assert!(band0.vht_cap.is_none());
    }

    #[test]
    fn test_convert_device_info() {
        let wlanmac_info = fake_wlanmac_info();
        let device_info =
            device_info_from_wlanmac_info(wlanmac_info).expect("Failed to conver wlanmac info");
        assert_eq!(device_info.mac_addr, wlanmac_info.mac_addr);
        assert_eq!(device_info.role, fidl_mlme::MacRole::Client);
        assert_eq!(device_info.bands.len(), 2);
        assert_eq!(
            device_info.driver_features,
            vec![fidl_common::DriverFeature::SaeSmeAuth, fidl_common::DriverFeature::TempSoftmac]
        );
    }

    #[test]
    fn test_convert_device_info_unknown_role() {
        let mut wlanmac_info = fake_wlanmac_info();
        wlanmac_info.mac_role.0 = 10;
        device_info_from_wlanmac_info(wlanmac_info)
            .expect_err("Shouldn't convert invalid mac role");
    }

    #[test]
    fn test_convert_driver_features() {
        let features = banjo_ddk_wlaninfo::WlanInfoDriverFeature::SCAN_OFFLOAD
            | banjo_ddk_wlaninfo::WlanInfoDriverFeature::RATE_SELECTION
            | banjo_ddk_wlaninfo::WlanInfoDriverFeature::SYNTH
            | banjo_ddk_wlaninfo::WlanInfoDriverFeature::TX_STATUS_REPORT
            | banjo_ddk_wlaninfo::WlanInfoDriverFeature::DFS
            | banjo_ddk_wlaninfo::WlanInfoDriverFeature::PROBE_RESP_OFFLOAD;
        let converted_features = convert_driver_features(&features);
        assert_eq!(
            converted_features,
            vec![
                fidl_common::DriverFeature::SaeSmeAuth,
                fidl_common::DriverFeature::TempSoftmac,
                fidl_common::DriverFeature::ScanOffload,
                fidl_common::DriverFeature::RateSelection,
                fidl_common::DriverFeature::Synth,
                fidl_common::DriverFeature::TxStatusReport,
                fidl_common::DriverFeature::Dfs,
                fidl_common::DriverFeature::ProbeRespOffload,
            ]
        )
    }
}
