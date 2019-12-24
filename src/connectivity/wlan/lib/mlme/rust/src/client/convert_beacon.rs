// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    banjo_ddk_protocol_wlan_mac as banjo_wlan_mac, fidl_fuchsia_wlan_mlme as fidl_mlme,
    std::convert::TryInto,
    wlan_common::{
        channel::derive_channel,
        ie,
        mac::{Bssid, CapabilityInfo},
        TimeUnit,
    },
};

/// Given information from beacon or probe response, convert to BssDescription.
pub fn construct_bss_description(
    bssid: Bssid,
    timestamp: u64,
    beacon_interval: TimeUnit,
    capability_info: CapabilityInfo,
    ies: &[u8],
    rx_info: banjo_wlan_mac::WlanRxInfo,
) -> Result<fidl_mlme::BssDescription, Error> {
    let mut ssid = None;
    let mut rates = None;
    let mut dsss_chan = None;
    let mut dtim_period = 0;
    let mut country = None;
    let mut rsne = None;
    let mut vendor_ies = None;
    let mut ht_cap = None;
    let mut ht_op = None;
    let mut vht_cap = None;
    let mut vht_op = None;

    let mut parsed_ht_op = None;
    let mut parsed_vht_op = None;

    for (id, body) in ie::Reader::new(ies) {
        match id {
            ie::Id::SSID => ssid = Some(ie::parse_ssid(body)?.to_vec()),
            ie::Id::SUPPORTED_RATES | ie::Id::EXT_SUPPORTED_RATES => {
                rates.get_or_insert_with(|| vec![]).extend_from_slice(body);
            }
            ie::Id::DSSS_PARAM_SET => {
                dsss_chan = Some(ie::parse_dsss_param_set(body)?.current_chan)
            }
            ie::Id::TIM => dtim_period = ie::parse_tim(body)?.header.dtim_period,
            ie::Id::COUNTRY => country = Some(body.to_vec()),
            ie::Id::RSNE => {
                let mut rsne_bytes = vec![id.0, body.len() as u8];
                rsne_bytes.extend_from_slice(body);
                rsne = Some(rsne_bytes);
            }
            ie::Id::VENDOR_SPECIFIC => {
                let vendor_ies = vendor_ies.get_or_insert_with(|| vec![]);
                vendor_ies.push(id.0);
                vendor_ies.push(body.len() as u8);
                vendor_ies.extend_from_slice(body);
            }
            ie::Id::HT_CAPABILITIES => {
                if body.len() != fidl_mlme::HT_CAP_LEN as usize {
                    return Err(format_err!("Invalid HT capabilities IE"));
                }
                // Safe to unwrap because length was already checked above
                let bytes: [u8; fidl_mlme::HT_CAP_LEN as usize] = body.try_into().unwrap();
                ht_cap = Some(Box::new(fidl_mlme::HtCapabilities { bytes }))
            }
            ie::Id::HT_OPERATION => {
                parsed_ht_op = Some(*ie::parse_ht_operation(body)?);
                if body.len() != fidl_mlme::HT_OP_LEN as usize {
                    return Err(format_err!("Invalid HT operation IE"));
                }
                // Safe to unwrap because length was already checked above
                let bytes: [u8; fidl_mlme::HT_OP_LEN as usize] = body.try_into().unwrap();
                ht_op = Some(Box::new(fidl_mlme::HtOperation { bytes }));
            }
            ie::Id::VHT_CAPABILITIES => {
                if body.len() != fidl_mlme::VHT_CAP_LEN as usize {
                    return Err(format_err!("Invalid VHT capabilities IE"));
                }
                // Safe to unwrap because length was already checked above
                let bytes: [u8; fidl_mlme::VHT_CAP_LEN as usize] = body.try_into().unwrap();
                vht_cap = Some(Box::new(fidl_mlme::VhtCapabilities { bytes }));
            }
            ie::Id::VHT_OPERATION => {
                parsed_vht_op = Some(*ie::parse_vht_operation(body)?);
                if body.len() != fidl_mlme::VHT_OP_LEN as usize {
                    return Err(format_err!("Invalid VHT operation IE"));
                }
                // Safe to unwrap because length was already checked above
                let bytes: [u8; fidl_mlme::VHT_OP_LEN as usize] = body.try_into().unwrap();
                vht_op = Some(Box::new(fidl_mlme::VhtOperation { bytes }));
            }
            _ => (),
        }
    }

    let ssid = ssid.ok_or(format_err!("Missing SSID IE"))?;
    let rates = rates.ok_or(format_err!("Missing rates IE"))?;
    let bss_type = get_bss_type(capability_info);
    let chan = derive_channel(rx_info.chan.primary, dsss_chan, parsed_ht_op, parsed_vht_op);

    Ok(fidl_mlme::BssDescription {
        bssid: bssid.0,
        ssid,
        bss_type,
        beacon_period: beacon_interval.into(),
        dtim_period,
        timestamp,
        local_time: 0,
        cap: capability_info.raw(),
        rates,
        country,
        rsne,
        vendor_ies,
        ht_cap,
        ht_op,
        vht_cap,
        vht_op,

        chan,
        rssi_dbm: rx_info.rssi_dbm,
        rcpi_dbmh: rx_info.rcpi_dbmh,
        rsni_dbh: rx_info.snr_dbh,
    })
}

/// Note: This is in Beacon / Probe Response frames context.
/// IEEE Std 802.11-2016, 9.4.1.4
fn get_bss_type(capability_info: CapabilityInfo) -> fidl_mlme::BssTypes {
    match (capability_info.ess(), capability_info.ibss()) {
        (true, false) => fidl_mlme::BssTypes::Infrastructure,
        (false, true) => fidl_mlme::BssTypes::Independent,
        (false, false) => fidl_mlme::BssTypes::Mesh,
        _ => fidl_mlme::BssTypes::AnyBss,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, banjo_ddk_protocol_wlan_info as banjo_wlan_info,
        fidl_fuchsia_wlan_common as fidl_common,
    };

    const BSSID: Bssid = Bssid([0x33; 6]);
    const TIMESTAMP: u64 = 364983910445;
    const BEACON_INTERVAL: u16 = 100;
    // Capability information: ESS, privacy, spectrum mgmt, radio msmt
    const CAPABILITY_INFO: CapabilityInfo = CapabilityInfo(0x1111);
    const RX_INFO: banjo_wlan_mac::WlanRxInfo = banjo_wlan_mac::WlanRxInfo {
        chan: banjo_wlan_info::WlanChannel {
            primary: 11,
            cbw: banjo_wlan_info::WlanChannelBandwidth::_20,
            secondary80: 0,
        },
        rssi_dbm: -40,
        rcpi_dbmh: 30,
        snr_dbh: 35,

        // Unused fields
        rx_flags: 0,
        valid_fields: 0,
        phy: 0,
        data_rate: 0,
        mcs: 0,
    };

    fn beacon_frame_ies() -> Vec<u8> {
        #[rustfmt::skip]
        let ies = vec![
            // SSID: "foo-ssid"
            0x00, 0x08, 0x66, 0x6f, 0x6f, 0x2d, 0x73, 0x73, 0x69, 0x64,
            // Supported rates: 24(B), 36, 48, 54
            0x01, 0x04, 0xb0, 0x48, 0x60, 0x6c,
            // DS parameter set: channel 140
            0x03, 0x01, 0x8c,
            // TIM - DTIM count: 0, DTIM period: 1, PVB: 2
            0x05, 0x04, 0x00, 0x01, 0x00, 0x02,
            // Country info
            0x07, 0x10,
            0x55, 0x53, 0x20, // US, Any environment
            0x24, 0x04, 0x24, // 1st channel: 36, # channels: 4, maximum tx power: 36 dBm
            0x34, 0x04, 0x1e, // 1st channel: 52, # channels: 4, maximum tx power: 30 dBm
            0x64, 0x0c, 0x1e, // 1st channel: 100, # channels: 12, maximum tx power: 30 dBm
            0x95, 0x05, 0x24, // 1st channel: 149, # channels: 5, maximum tx power: 36 dBm
            0x00, // padding
            // Power constraint: 0
            0x20, 0x01, 0x00,
            // TPC Report Transmit Power: 9, Link Margin: 0
            0x23, 0x02, 0x09, 0x00,
            // RSN Information
            0x30, 0x14, 0x01, 0x00,
            0x00, 0x0f, 0xac, 0x04, // Group cipher: AES (CCM)
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise cipher: AES (CCM)
            0x01, 0x00, 0x00, 0x0f, 0xac, 0x01, // AKM: EAP
            0x28, 0x00, // RSN capabilities
            // HT Capabilities
            0x2d, 0x1a,
            0xef, 0x09, // HT capabilities info
            0x17, // A-MPDU parameters
            0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // MCS set
            0x00, 0x00, // HT extended capabilities
            0x00, 0x00, 0x00, 0x00, // Transmit beamforming
            0x00, // Antenna selection capabilities
            // HT Operation
            0x3d, 0x16,
            0x8c, // Primary channel: 140
            0x0d, // HT info subset - secondary channel above, any channel width, RIFS permitted
            0x16, 0x00, 0x00, 0x00, // HT info subsets
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Basic MCS set
            // Extended Capabilities: extended channel switching, BSS transition, operating mode notification
            0x7f, 0x08, 0x04, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x40,
            // VHT Capabilities
            0xbf, 0x0c,
            0x91, 0x59, 0x82, 0x0f, // VHT capabilities info
            0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00, // VHT supported MCS set
            // VHT Operation
            0xc0, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00,
            // VHT Tx Power Envelope
            0xc3, 0x03, 0x01, 0x24, 0x24,
            // Aruba, Hewlett Packard vendor-specific IE
            0xdd, 0x07, 0x00, 0x0b, 0x86, 0x01, 0x04, 0x08, 0x09,
            // WMM parameters
            0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
            0x80, // U-APSD enabled
            0x00, // reserved
            0x03, 0xa4, 0x00, 0x00, // AC_BE parameters
            0x27, 0xa4, 0x00, 0x00, // AC_BK parameters
            0x42, 0x43, 0x5e, 0x00, // AC_VI parameters
            0x62, 0x32, 0x2f, 0x00, // AC_VO parameters
        ];
        ies
    }

    #[test]
    fn test_construct_bss_description() {
        let ies = beacon_frame_ies();
        let bss_desc = construct_bss_description(
            BSSID,
            TIMESTAMP,
            TimeUnit(BEACON_INTERVAL),
            CAPABILITY_INFO,
            &ies[..],
            RX_INFO,
        )
        .expect("expect convert_beacon to succeed");

        assert_eq!(
            bss_desc,
            fidl_mlme::BssDescription {
                bssid: BSSID.0,
                ssid: b"foo-ssid".to_vec(),
                bss_type: fidl_mlme::BssTypes::Infrastructure,
                beacon_period: BEACON_INTERVAL,
                dtim_period: 1,
                timestamp: TIMESTAMP,
                local_time: 0,
                cap: CAPABILITY_INFO.0,
                rates: vec![0xb0, 0x48, 0x60, 0x6c],
                country: Some(vec![
                    // Note: We read the last padding byte, which may not be the right behavior
                    0x55, 0x53, 0x20, 0x24, 0x04, 0x24, 0x34, 0x04, 0x1e, 0x64, 0x0c, 0x1e, 0x95,
                    0x05, 0x24, 0x00,
                ]),
                rsne: Some(vec![
                    0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac,
                    0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x01, 0x28, 0x00,
                ]),
                vendor_ies: Some(vec![
                    0xdd, 0x07, 0x00, 0x0b, 0x86, 0x01, 0x04, 0x08, 0x09, 0xdd, 0x18, 0x00, 0x50,
                    0xf2, 0x02, 0x01, 0x01, 0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00,
                    0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00
                ]),
                ht_cap: Some(Box::new(fidl_mlme::HtCapabilities {
                    bytes: [
                        0xef, 0x09, 0x17, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00
                    ]
                })),
                ht_op: Some(Box::new(fidl_mlme::HtOperation {
                    bytes: [
                        0x8c, 0x0d, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
                    ]
                })),
                vht_cap: Some(Box::new(fidl_mlme::VhtCapabilities {
                    bytes: [0x91, 0x59, 0x82, 0x0f, 0xea, 0xff, 0x00, 0x00, 0xea, 0xff, 0x00, 0x00]
                })),
                vht_op: Some(Box::new(fidl_mlme::VhtOperation {
                    bytes: [0x00, 0x00, 0x00, 0x00, 0x00]
                })),
                rssi_dbm: RX_INFO.rssi_dbm,
                rcpi_dbmh: RX_INFO.rcpi_dbmh,
                rsni_dbh: RX_INFO.snr_dbh,
                chan: fidl_common::WlanChan {
                    primary: 140,
                    cbw: fidl_common::Cbw::Cbw40,
                    secondary80: 0,
                },
            }
        );
    }
}
