// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    banjo_ddk_protocol_wlan_info as banjo_wlan_info, banjo_ddk_protocol_wlan_mac as banjo_wlan_mac,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_mlme as fidl_mlme,
    wlan_common::{
        ie::{
            parse_ht_capabilities, parse_ht_operation, parse_vht_capabilities, parse_vht_operation,
        },
        mac::{Aid, Bssid},
    },
};

pub fn ddk_channel_from_fidl(fc: fidl_common::WlanChan) -> banjo_wlan_info::WlanChannel {
    let cbw = match fc.cbw {
        fidl_common::Cbw::Cbw20 => banjo_wlan_info::WlanChannelBandwidth::_20,
        fidl_common::Cbw::Cbw40 => banjo_wlan_info::WlanChannelBandwidth::_40,
        fidl_common::Cbw::Cbw40Below => banjo_wlan_info::WlanChannelBandwidth::_40BELOW,
        fidl_common::Cbw::Cbw80 => banjo_wlan_info::WlanChannelBandwidth::_80,
        fidl_common::Cbw::Cbw160 => banjo_wlan_info::WlanChannelBandwidth::_160,
        fidl_common::Cbw::Cbw80P80 => banjo_wlan_info::WlanChannelBandwidth::_80P80,
    };
    banjo_wlan_info::WlanChannel { primary: fc.primary, cbw, secondary80: fc.secondary80 }
}

pub fn build_ddk_assoc_ctx(
    bssid: Bssid,
    aid: Aid,
    cap: fidl_mlme::NegotiatedCapabilities,
    ht_op: Option<[u8; fidl_mlme::HT_OP_LEN as usize]>,
    vht_op: Option<[u8; fidl_mlme::VHT_OP_LEN as usize]>,
) -> banjo_wlan_info::WlanAssocCtx {
    let mut rates = [0; banjo_wlan_info::WLAN_MAC_MAX_RATES as usize];
    rates[..cap.rates.len()].clone_from_slice(&cap.rates);
    let has_ht_cap = cap.ht_cap.is_some();
    let has_vht_cap = cap.vht_cap.is_some();
    let phy = match (has_ht_cap, has_vht_cap) {
        (true, true) => banjo_wlan_info::WlanPhyType::VHT,
        (true, false) => banjo_wlan_info::WlanPhyType::HT,
        // It is invalid to have VHT without HT and SME would guarantee it does not happen.
        // But default to ERP nonetheless just to be safe.
        _ => banjo_wlan_info::WlanPhyType::ERP,
    };
    let ht_cap_bytes = cap.ht_cap.map_or([0; fidl_mlme::HT_CAP_LEN as usize], |h| h.bytes);
    let vht_cap_bytes = cap.vht_cap.map_or([0; fidl_mlme::VHT_CAP_LEN as usize], |v| v.bytes);
    let ht_op_bytes = ht_op.unwrap_or([0; fidl_mlme::HT_OP_LEN as usize]);
    let vht_op_bytes = vht_op.unwrap_or([0; fidl_mlme::VHT_OP_LEN as usize]);
    banjo_wlan_info::WlanAssocCtx {
        bssid: bssid.0,
        aid,
        // In the association request we sent out earlier, listen_interval is always set to 0,
        // indicating the client never enters power save mode.
        // TODO(fxbug.dev/42217): ath10k disregard this value and hard code it to 1.
        // It is working now but we may need to revisit.
        listen_interval: 0,
        phy,
        chan: ddk_channel_from_fidl(cap.channel),
        // TODO(fxbug.dev/29325): QoS works with Aruba/Ubiquiti for BlockAck session but it may need to be
        // dynamically determined for each outgoing data frame.
        // TODO(fxbug.dev/43938): Derive QoS flag and WMM parameters from device info
        qos: has_ht_cap,
        ac_be_params: blank_wmm_params(),
        ac_bk_params: blank_wmm_params(),
        ac_vi_params: blank_wmm_params(),
        ac_vo_params: blank_wmm_params(),

        rates_cnt: cap.rates.len() as u16, // will not overflow as MAX_RATES_LEN is u8
        rates,
        cap_info: cap.cap_info,
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
    banjo_wlan_info::WlanWmmParams { ecw_min: 0, ecw_max: 0, aifsn: 0, txop_limit: 0, acm: false }
}

#[cfg(test)]
mod tests {
    use {
        super::*, banjo_ddk_hw_wlan_ieee80211 as banjo_80211, std::convert::TryInto,
        wlan_common::ie, zerocopy::AsBytes,
    };

    #[test]
    fn assoc_ctx_construction_successful() {
        let ddk = build_ddk_assoc_ctx(
            Bssid([1, 2, 3, 4, 5, 6]),
            42,
            fidl_mlme::NegotiatedCapabilities {
                channel: fidl_common::WlanChan {
                    primary: 149,
                    cbw: fidl_common::Cbw::Cbw40,
                    secondary80: 42,
                },
                cap_info: 0x1234,
                rates: vec![111, 112, 113, 114, 115, 116, 117, 118, 119, 120],
                wmm_param: None,
                ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                    bytes: ie::fake_ht_capabilities().as_bytes().try_into().unwrap(),
                })),
                vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
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
            banjo_wlan_info::WlanChannel {
                primary: 149,
                cbw: banjo_wlan_info::WlanChannelBandwidth::_40,
                secondary80: 42
            },
            ddk.chan
        );
        assert_eq!(true, ddk.qos);

        assert_eq!(10, ddk.rates_cnt);
        assert_eq!([111, 112, 113, 114, 115, 116, 117, 118, 119, 120], ddk.rates[0..10]);
        assert_eq!(&[0; 253][..], &ddk.rates[10..]);

        assert_eq!(0x1234, ddk.cap_info);

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
            chan: banjo_wlan_info::WlanChannel {
                primary: 0,
                cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
                secondary80: 0,
            },
            mcs: 0,
            rssi_dbm: 0,
            rcpi_dbmh: 0,
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
}
