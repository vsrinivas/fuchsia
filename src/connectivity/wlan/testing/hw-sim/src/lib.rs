// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_common::WlanMacRole,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{
        StartScanArgs, TxArgs, WlanRxInfo, WlantapPhyConfig, WlantapPhyEvent, WlantapPhyProxy,
    },
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::prelude::*,
    ieee80211::{Bssid, Ssid},
    lazy_static::lazy_static,
    pin_utils::pin_mut,
    std::{convert::TryFrom, future::Future, marker::Unpin},
    tracing::{debug, error},
    wlan_common::{
        bss::Protection,
        data_writer,
        ie::{
            rsn::{
                cipher::{Cipher, CIPHER_CCMP_128, CIPHER_TKIP},
                rsne,
                suite_filter::DEFAULT_GROUP_MGMT_CIPHER,
            },
            wpa,
        },
        mac, mgmt_writer, TimeUnit,
    },
    wlan_frame_writer::write_frame_with_dynamic_buf,
    wlan_rsn::{self, rsna::SecAssocUpdate},
};

pub mod test_utils;
pub use device_helper::*;
pub use eth_helper::*;
pub use event_handler_helper::*;
pub use wlancfg_helper::*;

mod config;
mod device_helper;
mod eth_helper;
mod event_handler_helper;
mod wlancfg_helper;

pub const PSK_STR_LEN: usize = 64;
pub const CLIENT_MAC_ADDR: [u8; 6] = [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b];
pub const AP_MAC_ADDR: Bssid = Bssid([0x70, 0xf1, 0x1c, 0x05, 0x2d, 0x7f]);
lazy_static! {
    pub static ref AP_SSID: Ssid = Ssid::try_from("ap_ssid").unwrap();
}
pub const ETH_DST_MAC: [u8; 6] = [0x65, 0x74, 0x68, 0x64, 0x73, 0x74];
pub const CHANNEL_1: fidl_common::WlanChannel = fidl_common::WlanChannel {
    primary: 1,
    secondary80: 0,
    cbw: fidl_common::ChannelBandwidth::Cbw20,
};
pub const WLANCFG_DEFAULT_AP_CHANNEL: fidl_common::WlanChannel = fidl_common::WlanChannel {
    primary: 11,
    secondary80: 0,
    cbw: fidl_common::ChannelBandwidth::Cbw20,
};

// TODO(fxbug.dev/108667): This sleep was introduced to preserve the old timing behavior
// of scanning when hw-sim depending on the SoftMAC driver iterating through all of the
// channels.
lazy_static! {
    pub static ref ARTIFICIAL_SCAN_SLEEP: fuchsia_zircon::Duration = 2.seconds();
}

pub fn default_wlantap_config_client() -> WlantapPhyConfig {
    wlantap_config_client(format!("wlantap-client"), CLIENT_MAC_ADDR)
}

pub fn wlantap_config_client(name: String, mac_addr: [u8; 6]) -> WlantapPhyConfig {
    config::create_wlantap_config(name, mac_addr, WlanMacRole::Client)
}

pub fn default_wlantap_config_ap() -> WlantapPhyConfig {
    wlantap_config_ap(format!("wlantap-ap"), AP_MAC_ADDR.0)
}

pub fn wlantap_config_ap(name: String, mac_addr: [u8; 6]) -> WlantapPhyConfig {
    config::create_wlantap_config(name, mac_addr, WlanMacRole::Ap)
}

pub fn create_rx_info(channel: &fidl_common::WlanChannel, rssi_dbm: i8) -> WlanRxInfo {
    // should match enum WlanRxInfoValid::RSSI in zircon/system/banjo/fuchsia.hardware.wlan.associnfo/info.banjo
    const WLAN_RX_INFO_VALID_RSSI: u32 = 0x10;
    WlanRxInfo {
        rx_flags: 0,
        valid_fields: if rssi_dbm == 0 { 0 } else { WLAN_RX_INFO_VALID_RSSI },
        phy: fidl_common::WlanPhyType::Dsss,
        data_rate: 0,
        channel: channel.clone(),
        mcs: 0,
        rssi_dbm,
        snr_dbh: 0,
    }
}

pub enum BeaconOrProbeResp<'a> {
    Beacon,
    ProbeResp { wsc_ie: Option<&'a [u8]> },
}

fn generate_probe_or_beacon(
    type_: BeaconOrProbeResp<'_>,
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    ssid: &Ssid,
    protection: &Protection,
) -> Result<Vec<u8>, anyhow::Error> {
    let wpa1_ie = wpa::fake_wpa_ies::fake_deprecated_wpa1_vendor_ie();
    // Unrealistically long beacon period so that auth/assoc don't timeout on slow bots.
    let beacon_interval = TimeUnit::DEFAULT_BEACON_INTERVAL * 20u16;
    let capabilities = mac::CapabilityInfo(0)
        // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the ESS subfield to 1 and the IBSS
        // subfield to 0 within transmitted Beacon or Probe Response frames.
        .with_ess(true)
        .with_ibss(false)
        // IEEE Std 802.11-2016, 9.4.1.4: An AP sets the Privacy subfield to 1 within
        // transmitted Beacon, Probe Response, (Re)Association Response frames if data
        // confidentiality is required for all Data frames exchanged within the BSS.
        .with_privacy(*protection != Protection::Open);
    let beacon_hdr = match type_ {
        BeaconOrProbeResp::Beacon => Some(mac::BeaconHdr::new(beacon_interval, capabilities)),
        BeaconOrProbeResp::ProbeResp { wsc_ie: _ } => None,
    };
    let proberesp_hdr = match type_ {
        BeaconOrProbeResp::Beacon => None,
        BeaconOrProbeResp::ProbeResp { .. } => {
            Some(mac::ProbeRespHdr::new(beacon_interval, capabilities))
        }
    };

    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(match type_ {
                        BeaconOrProbeResp::Beacon => mac::MgmtSubtype::BEACON,
                        BeaconOrProbeResp::ProbeResp{..} => mac::MgmtSubtype::PROBE_RESP
                    }),
                match type_ {
                    BeaconOrProbeResp::Beacon => mac::BCAST_ADDR,
                    BeaconOrProbeResp::ProbeResp{..} => CLIENT_MAC_ADDR
                },
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::BeaconHdr?: beacon_hdr,
            mac::ProbeRespHdr?: proberesp_hdr,
        },
        ies: {
            ssid: ssid,
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0xe0, 0x6c],
            extended_supported_rates: { /* continues from supported_rates */ },
            dsss_param_set: &ie::DsssParamSet { current_channel: channel.primary },
            rsne?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep | Protection::Wpa1 => None,
                Protection::Wpa1Wpa2Personal | Protection::Wpa2Personal => Some(rsne::Rsne::wpa2_rsne()),
                Protection::Wpa2Wpa3Personal => Some(rsne::Rsne::wpa2_wpa3_rsne()),
                Protection::Wpa3Personal => Some(rsne::Rsne::wpa3_rsne()),
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wpa1?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep => None,
                Protection::Wpa1 | Protection::Wpa1Wpa2Personal => Some(&wpa1_ie),
                Protection::Wpa2Personal | Protection::Wpa2Wpa3Personal | Protection::Wpa3Personal => None,
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wsc?: match type_ {
                BeaconOrProbeResp::Beacon => None,
                BeaconOrProbeResp::ProbeResp{ wsc_ie } => wsc_ie.clone()
            }
        },
    })?;
    Ok(buf)
}

pub fn send_beacon(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    ssid: &Ssid,
    protection: &Protection,
    proxy: &WlantapPhyProxy,
    rssi_dbm: i8,
) -> Result<(), anyhow::Error> {
    let buf =
        generate_probe_or_beacon(BeaconOrProbeResp::Beacon, channel, bssid, ssid, protection)?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, rssi_dbm))?;
    Ok(())
}

pub fn send_probe_resp(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    ssid: &Ssid,
    protection: &Protection,
    wsc_ie: Option<&[u8]>,
    proxy: &WlantapPhyProxy,
    rssi_dbm: i8,
) -> Result<(), anyhow::Error> {
    let buf = generate_probe_or_beacon(
        BeaconOrProbeResp::ProbeResp { wsc_ie },
        channel,
        bssid,
        ssid,
        protection,
    )?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, rssi_dbm))?;
    Ok(())
}

pub fn send_sae_authentication_frame(
    sae_frame: &fidl_mlme::SaeFrame,
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::AUTH),
                CLIENT_MAC_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::AuthHdr: &mac::AuthHdr {
                auth_alg_num: mac::AuthAlgorithmNumber::SAE,
                auth_txn_seq_num: sae_frame.seq_num,
                status_code: sae_frame.status_code.into(),
            },
        },
        body: &sae_frame.sae_fields[..],
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_open_authentication_success(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::AUTH),
                CLIENT_MAC_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::AuthHdr: &mac::AuthHdr {
                auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
                auth_txn_seq_num: 2,
                status_code: fidl_ieee80211::StatusCode::Success.into(),
            },
        },
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_association_response(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    status_code: mac::StatusCode,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_RESP),
                CLIENT_MAC_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::AssocRespHdr: &mac::AssocRespHdr {
                capabilities: mac::CapabilityInfo(0).with_ess(true).with_short_preamble(true),
                status_code,
                aid: 2, // does not matter
            },
        },
        ies: {
            // These rates will be captured in assoc_ctx to initialize Minstrel. 11b rates are
            // ignored.
            // tx_vec_idx:        _     _     _   129   130     _   131   132
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24],
            // tx_vec_idx:              133 134 basic_135  136
            extended_supported_rates:  &[48, 72, 128 + 96, 108],
        },
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_disassociate(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    reason_code: mac::ReasonCode,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::DISASSOC),
                CLIENT_MAC_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::DisassocHdr: &mac::DisassocHdr {
                reason_code,
            },
        },
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn password_or_psk_to_policy_credential<S: ToString>(
    password_or_psk: Option<S>,
) -> fidl_policy::Credential {
    return match password_or_psk {
        None => fidl_policy::Credential::None(fidl_policy::Empty),
        Some(p) => {
            let p = p.to_string().as_bytes().to_vec();
            if p.len() == PSK_STR_LEN {
                // The PSK is given in a 64 character hexadecimal string.
                let psk = hex::decode(p).expect("Failed to decode psk");
                fidl_policy::Credential::Psk(psk)
            } else {
                fidl_policy::Credential::Password(p)
            }
        }
    };
}

pub fn create_authenticator(
    bssid: &Bssid,
    ssid: &Ssid,
    password_or_psk: &str,
    // The group key cipher
    gtk_cipher: Cipher,
    // The advertised protection in the IEs during the 4-way handshake
    advertised_protection: Protection,
    // The protection used for the actual handshake
    supplicant_protection: Protection,
) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(gtk_cipher).expect("creating gtk provider");

    let advertised_protection_info = match advertised_protection {
        Protection::Wpa3Personal => wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa3_rsne()),
        Protection::Wpa2Wpa3Personal => {
            wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa2_wpa3_rsne())
        }
        Protection::Wpa2Personal | Protection::Wpa1Wpa2Personal => {
            wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa2_rsne())
        }
        Protection::Wpa2PersonalTkipOnly | Protection::Wpa1Wpa2PersonalTkipOnly => {
            panic!("need tkip support")
        }
        Protection::Wpa1 => {
            wlan_rsn::ProtectionInfo::LegacyWpa(wpa::fake_wpa_ies::fake_deprecated_wpa1_vendor_ie())
        }
        _ => {
            panic!("{} not implemented", advertised_protection)
        }
    };

    match supplicant_protection {
        Protection::Wpa1 | Protection::Wpa2Personal => {
            let psk = match password_or_psk.len() {
                PSK_STR_LEN => {
                    // The PSK is given in a 64 character hexadecimal string.
                    hex::decode(password_or_psk).expect("Failed to decode psk").into_boxed_slice()
                }
                _ => {
                    wlan_rsn::psk::compute(password_or_psk.as_bytes(), ssid).expect("computing PSK")
                }
            };
            let supplicant_protection_info = match supplicant_protection {
                Protection::Wpa1 => wlan_rsn::ProtectionInfo::LegacyWpa(
                    wpa::fake_wpa_ies::fake_deprecated_wpa1_vendor_ie(),
                ),
                Protection::Wpa2Personal => wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa2_rsne()),
                _ => unreachable!("impossible combination in this nested match"),
            };
            wlan_rsn::Authenticator::new_wpa2psk_ccmp128(
                nonce_rdr,
                std::sync::Arc::new(std::sync::Mutex::new(gtk_provider)),
                psk,
                CLIENT_MAC_ADDR,
                supplicant_protection_info,
                bssid.0,
                advertised_protection_info,
            )
            .expect("creating authenticator")
        }
        Protection::Wpa3Personal => {
            let igtk_provider = wlan_rsn::IgtkProvider::new(DEFAULT_GROUP_MGMT_CIPHER)
                .expect("creating igtk provider");
            let supplicant_protection_info =
                wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa3_rsne());
            wlan_rsn::Authenticator::new_wpa3(
                nonce_rdr,
                std::sync::Arc::new(std::sync::Mutex::new(gtk_provider)),
                std::sync::Arc::new(std::sync::Mutex::new(igtk_provider)),
                ssid.clone(),
                password_or_psk.as_bytes().to_vec(),
                CLIENT_MAC_ADDR,
                supplicant_protection_info,
                bssid.0,
                advertised_protection_info,
            )
            .expect("creating authenticator")
        }
        _ => {
            panic!("Cannot create an authenticator for {}", supplicant_protection)
        }
    }
}

pub fn process_tx_auth_updates(
    authenticator: &mut wlan_rsn::Authenticator,
    update_sink: &mut wlan_rsn::rsna::UpdateSink,
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    phy: &WlantapPhyProxy,
    ready_for_sae_frames: bool,
    ready_for_eapol_frames: bool,
) -> Result<(), anyhow::Error> {
    if !ready_for_sae_frames && !ready_for_eapol_frames {
        return Ok(());
    }

    // TODO(fxbug.dev/69580): Use Vec::drain_filter instead.
    let mut i = 0;
    while i < update_sink.len() {
        match &update_sink[i] {
            SecAssocUpdate::TxSaeFrame(sae_frame) if ready_for_sae_frames => {
                send_sae_authentication_frame(&sae_frame, &CHANNEL_1, bssid, &phy)
                    .expect("Error sending fake SAE authentication frame.");
                update_sink.remove(i);
            }
            SecAssocUpdate::TxEapolKeyFrame { frame, .. } if ready_for_eapol_frames => {
                rx_wlan_data_frame(
                    channel,
                    &CLIENT_MAC_ADDR,
                    &bssid.0,
                    &bssid.0,
                    &frame[..],
                    mac::ETHER_TYPE_EAPOL,
                    phy,
                )?;
                update_sink.remove(i);
                authenticator
                    .on_eapol_conf(update_sink, fidl_mlme::EapolResultCode::Success)
                    .expect("Error sending EAPOL confirm");
            }
            _ => i += 1,
        };
    }

    Ok(())
}

pub struct BeaconInfo<'a> {
    pub channel: fidl_common::WlanChannel,
    pub bssid: Bssid,
    pub ssid: Ssid,
    pub protection: Protection,
    pub rssi_dbm: i8,
    pub beacon_or_probe: BeaconOrProbeResp<'a>,
}

impl<'a> std::default::Default for BeaconInfo<'a> {
    fn default() -> Self {
        Self {
            channel: fidl_common::WlanChannel {
                primary: 1,
                secondary80: 0,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
            },
            bssid: Bssid([0; 6]),
            ssid: Ssid::empty(),
            protection: Protection::Open,
            rssi_dbm: 0,
            beacon_or_probe: BeaconOrProbeResp::Beacon,
        }
    }
}

pub fn send_scan_result(phy: &WlantapPhyProxy, beacon_info: &BeaconInfo<'_>) {
    match beacon_info.beacon_or_probe {
        BeaconOrProbeResp::Beacon => {
            send_beacon(
                &beacon_info.channel,
                &beacon_info.bssid,
                &beacon_info.ssid,
                &beacon_info.protection,
                phy,
                beacon_info.rssi_dbm,
            )
            .unwrap();
        }
        BeaconOrProbeResp::ProbeResp { wsc_ie } => send_probe_resp(
            &beacon_info.channel,
            &beacon_info.bssid,
            &beacon_info.ssid,
            &beacon_info.protection,
            wsc_ie,
            phy,
            beacon_info.rssi_dbm,
        )
        .unwrap(),
    }
}

pub fn send_scan_complete(
    scan_id: u64,
    status: i32,
    phy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    tracing::info!(
        "TODO(fxbug.dev/108667): Sleep {} seconds before `ScanComplete()`",
        ARTIFICIAL_SCAN_SLEEP.into_seconds()
    );
    ARTIFICIAL_SCAN_SLEEP.sleep();
    phy.scan_complete(0, scan_id, status).map_err(|e| e.into())
}

pub fn handle_start_scan_event(
    args: &StartScanArgs,
    phy: &WlantapPhyProxy,
    beacon_info: &BeaconInfo<'_>,
) {
    debug!("Handling start scan event with scan_id {:?}", args.scan_id);
    send_scan_result(phy, beacon_info);
    send_scan_complete(args.scan_id, 0, phy).unwrap();
}

pub fn handle_tx_event<F>(
    args: &TxArgs,
    phy: &WlantapPhyProxy,
    ssid: &Ssid,
    bssid: &Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
    mut process_auth_update: F,
) where
    F: FnMut(
        &mut wlan_rsn::Authenticator,
        &mut wlan_rsn::rsna::UpdateSink,
        &fidl_common::WlanChannel,
        &Bssid,
        &WlantapPhyProxy,
        bool, // ready_for_sae_frames
        bool, // ready_for_eapol_frames
    ) -> Result<(), anyhow::Error>,
{
    debug!("Handling tx event.");
    match mac::MacFrame::parse(&args.packet.data[..], false) {
        Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
            match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                Some(mac::MgmtBody::Authentication { auth_hdr, elements }) => {
                    match auth_hdr.auth_alg_num {
                        mac::AuthAlgorithmNumber::OPEN => match authenticator {
                            Some(wlan_rsn::Authenticator {
                                auth_cfg: wlan_rsn::auth::Config::ComputedPsk(_),
                                ..
                            })
                            | None => {
                                send_open_authentication_success(&CHANNEL_1, bssid, &phy)
                                    .expect("Error sending fake OPEN authentication frame.");
                            }
                            _ => panic!(
                                "Unexpected OPEN authentication frame for {:?}",
                                authenticator
                            ),
                        },
                        mac::AuthAlgorithmNumber::SAE => {
                            let mut authenticator = authenticator.as_mut().unwrap_or_else(|| {
                                panic!("Unexpected SAE authentication frame with no Authenticator")
                            });
                            let mut update_sink = update_sink.as_mut().unwrap_or_else(|| {
                                panic!("No UpdateSink provided with Authenticator.")
                            });
                            tracing::info!("auth_txn_seq_num: {}", { auth_hdr.auth_txn_seq_num });
                            // Reset authenticator state just in case this is not the 1st
                            // connection attempt. SAE handshake uses multiple frames, so only
                            // reset on the first auth frame.
                            // Note: If we want to test retransmission of auth frame 1, we should
                            //       find a different way to do this.
                            if auth_hdr.auth_txn_seq_num == 1 {
                                authenticator.reset();
                                // Clear out update sink so that the authenticator doesn't process
                                // the PMK from the first attempt.
                                update_sink.clear();
                            }

                            authenticator
                                .on_sae_frame_rx(
                                    &mut update_sink,
                                    fidl_mlme::SaeFrame {
                                        peer_sta_address: bssid.0,
                                        // TODO(fxbug.dev/91353): All reserved values mapped to REFUSED_REASON_UNSPECIFIED.
                                        status_code: Option::<fidl_ieee80211::StatusCode>::from(
                                            auth_hdr.status_code,
                                        )
                                        .unwrap_or(
                                            fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
                                        ),
                                        seq_num: auth_hdr.auth_txn_seq_num,
                                        sae_fields: elements.to_vec(),
                                    },
                                )
                                .expect("processing SAE frame");
                            process_auth_update(
                                &mut authenticator,
                                &mut update_sink,
                                &CHANNEL_1,
                                bssid,
                                &phy,
                                true,
                                false,
                            )
                            .expect("processing authenticator updates during authentication");
                        }
                        auth_alg_num => {
                            panic!("Unexpected authentication algorithm number: {:?}", auth_alg_num)
                        }
                    }
                }
                Some(mac::MgmtBody::AssociationReq { .. }) => {
                    send_association_response(
                        &CHANNEL_1,
                        bssid,
                        fidl_ieee80211::StatusCode::Success.into(),
                        &phy,
                    )
                    .expect("Error sending fake association response frame.");
                    if let Some(authenticator) = authenticator {
                        let mut update_sink = update_sink.as_mut().unwrap_or_else(|| {
                            panic!("No UpdateSink provided with Authenticator.")
                        });
                        match authenticator.auth_cfg {
                            wlan_rsn::auth::Config::Sae { .. } => {}
                            wlan_rsn::auth::Config::ComputedPsk(_) => authenticator.reset(),
                            wlan_rsn::auth::Config::DriverSae { .. } => panic!(
                                "hw-sim does not support wlan_rsn::auth::Config::DriverSae(_)"
                            ),
                        }
                        authenticator.initiate(&mut update_sink).expect("initiating authenticator");
                        process_auth_update(authenticator, &mut update_sink, &CHANNEL_1, bssid, &phy, true, true).expect(
                            "processing authenticator updates immediately after association complete",
                        );
                    }
                }
                Some(mac::MgmtBody::ProbeReq { .. }) => {
                    // Normally, the AP would only send probe response on the channel it's
                    // on, but our TestHelper doesn't have that feature yet and it
                    // does not affect any current tests.
                    send_probe_resp(&CHANNEL_1, &bssid, ssid, protection, None, &phy, -10)
                        .expect("Error sending fake probe response frame");
                }
                _ => {}
            }
        }
        // EAPOL frames are transmitted as data frames with LLC protocol being EAPOL
        Some(mac::MacFrame::Data { .. }) => {
            let msdus = mac::MsduIterator::from_raw_data_frame(&args.packet.data[..], false)
                .expect("reading msdu from data frame");
            for mac::Msdu { llc_frame, .. } in msdus {
                assert_eq!(llc_frame.hdr.protocol_id.to_native(), mac::ETHER_TYPE_EAPOL);
                if let Some(authenticator) = authenticator {
                    let mut update_sink = update_sink
                        .as_mut()
                        .unwrap_or_else(|| panic!("No UpdateSink provided with Authenticator."));
                    let mic_size = authenticator.get_negotiated_protection().mic_size;
                    let frame_rx = eapol::KeyFrameRx::parse(mic_size as usize, llc_frame.body)
                        .expect("parsing EAPOL frame");
                    if let Err(e) =
                        authenticator.on_eapol_frame(&mut update_sink, eapol::Frame::Key(frame_rx))
                    {
                        error!("error sending EAPOL frame to authenticator: {}", e);
                    }
                    process_auth_update(
                        authenticator,
                        update_sink,
                        &CHANNEL_1,
                        bssid,
                        &phy,
                        true,
                        true,
                    )
                    .expect("processing authenticator updates after EAPOL frame");
                }
            }
        }
        _ => (),
    }
}

pub fn handle_connect_events(
    event: &WlantapPhyEvent,
    phy: &WlantapPhyProxy,
    ssid: &Ssid,
    bssid: &Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
) {
    match event {
        WlantapPhyEvent::StartScan { args } => handle_start_scan_event(
            &args,
            phy,
            &BeaconInfo {
                channel: CHANNEL_1.clone(),
                bssid: bssid.clone(),
                ssid: ssid.clone(),
                protection: protection.clone(),
                rssi_dbm: -30,
                beacon_or_probe: BeaconOrProbeResp::Beacon,
            },
        ),
        WlantapPhyEvent::Tx { args } => match authenticator {
            Some(_) => match update_sink {
                Some(_) => handle_tx_event(
                    &args,
                    phy,
                    ssid,
                    bssid,
                    protection,
                    authenticator,
                    update_sink,
                    process_tx_auth_updates,
                ),
                None => panic!("No UpdateSink provided with Authenticator."),
            },
            None => handle_tx_event(
                &args,
                phy,
                ssid,
                bssid,
                protection,
                &mut None,
                &mut None,
                process_tx_auth_updates,
            ),
        },
        _ => (),
    }
}

pub async fn save_network_and_wait_until_connected(
    ssid: &Ssid,
    security_type: fidl_policy::SecurityType,
    credential: fidl_policy::Credential,
) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
    // Connect to the client policy service and get a client controller.
    let (client_controller, mut client_state_update_stream) =
        wlancfg_helper::init_client_controller().await;

    save_network(&client_controller, ssid, security_type, credential).await;

    // Wait until the policy layer indicates that the client has successfully connected.
    let id = fidl_policy::NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type.clone() };
    wait_until_client_state(&mut client_state_update_stream, |update| {
        has_id_and_state(update, &id, fidl_policy::ConnectionState::Connected)
    })
    .await;

    (client_controller, client_state_update_stream)
}

pub async fn connect_to_ap<F, R>(
    connect_fut: F,
    helper: &mut test_utils::TestHelper,
    ap_ssid: &Ssid,
    ap_bssid: &Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
    timeout: Option<i64>,
) -> R
where
    F: Future<Output = R> + Unpin,
{
    // Assert UpdateSink provided if there is an Authenticator.
    if matches!(authenticator, Some(_)) && !matches!(update_sink, Some(_)) {
        panic!("No UpdateSink provided with Authenticator");
    };

    let phy = helper.proxy();
    helper
        .run_until_complete_or_timeout(
            timeout.unwrap_or(30).seconds(),
            format!("connecting to {} ({:02X?})", ap_ssid.to_string_not_redactable(), ap_bssid),
            |event| {
                handle_connect_events(
                    &event,
                    &phy,
                    ap_ssid,
                    ap_bssid,
                    protection,
                    authenticator,
                    update_sink,
                );
            },
            connect_fut,
        )
        .await
}

pub async fn connect_with_security_type(
    helper: &mut test_utils::TestHelper,
    ssid: &Ssid,
    bssid: &Bssid,
    password_or_psk: Option<&str>,
    bss_protection: Protection,
    policy_security_type: fidl_policy::SecurityType,
) {
    let credential = password_or_psk_to_policy_credential(password_or_psk);
    let connect_fut = save_network_and_wait_until_connected(ssid, policy_security_type, credential);
    pin_mut!(connect_fut);

    // Create the authenticator
    let (mut authenticator, mut update_sink) = match bss_protection {
        Protection::Wpa3Personal | Protection::Wpa2Wpa3Personal => (
            password_or_psk.map(|p| {
                create_authenticator(
                    bssid,
                    ssid,
                    p,
                    CIPHER_CCMP_128,
                    bss_protection,
                    Protection::Wpa3Personal,
                )
            }),
            Some(wlan_rsn::rsna::UpdateSink::default()),
        ),
        Protection::Wpa2Personal | Protection::Wpa1Wpa2Personal => (
            password_or_psk.map(|p| {
                create_authenticator(
                    bssid,
                    ssid,
                    p,
                    CIPHER_CCMP_128,
                    bss_protection,
                    Protection::Wpa2Personal,
                )
            }),
            Some(wlan_rsn::rsna::UpdateSink::default()),
        ),
        Protection::Wpa2PersonalTkipOnly | Protection::Wpa1Wpa2PersonalTkipOnly => {
            panic!("need tkip support")
        }
        Protection::Wpa1 => (
            password_or_psk.map(|p| {
                create_authenticator(bssid, ssid, p, CIPHER_TKIP, bss_protection, Protection::Wpa1)
            }),
            Some(wlan_rsn::rsna::UpdateSink::default()),
        ),
        Protection::Open => (None, None),
        _ => {
            panic!("This helper doesn't yet support {}", bss_protection)
        }
    };

    connect_to_ap(
        connect_fut,
        helper,
        ssid,
        bssid,
        &bss_protection,
        &mut authenticator,
        &mut update_sink,
        None,
    )
    .await;
}

pub fn rx_wlan_data_frame(
    channel: &fidl_common::WlanChannel,
    addr1: &[u8; 6],
    addr2: &[u8; 6],
    addr3: &[u8; 6],
    payload: &[u8],
    ether_type: u16,
    phy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let (mut buf, bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::FixedDataHdrFields: &mac::FixedDataHdrFields {
                frame_ctrl: mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::DATA)
                    .with_data_subtype(mac::DataSubtype(0))
                    .with_from_ds(true),
                duration: 0,
                addr1: *addr1,
                addr2: *addr2,
                addr3: *addr3,
                seq_ctrl: mac::SequenceControl(0).with_seq_num(3),
            },
            mac::LlcHdr: &data_writer::make_snap_llc_hdr(ether_type),
        },
        payload: payload,
    })?;
    buf.truncate(bytes_written);

    phy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub async fn loop_until_iface_is_found(helper: &mut test_utils::TestHelper) {
    // Connect to the client policy service and get a client controller.
    let policy_provider = connect_to_protocol::<fidl_policy::ClientProviderMarker>()
        .expect("connecting to wlan policy");
    let (client_controller, server_end) = create_proxy().expect("creating client controller");
    let (update_client_end, _update_server_end) =
        create_endpoints().expect("creating client listener");
    let () =
        policy_provider.get_controller(server_end, update_client_end).expect("getting controller");

    // Attempt to issue a scan command until the request succeeds.  Scanning will fail until a
    // client interface is available.  A successful response to a scan request indicates that the
    // client policy layer is ready to use.
    // TODO(fxbug.dev/57415): Figure out a new way to signal that the client policy layer is ready to go.
    let mut retry = test_utils::RetryWithBackoff::infinite_with_max_interval(10.seconds());
    loop {
        let (scan_proxy, server_end) = create_proxy().unwrap();
        client_controller.scan_for_networks(server_end).expect("requesting scan");

        let fut = async move { scan_proxy.get_next().await.expect("getting scan results") };
        pin_mut!(fut);

        let phy = helper.proxy();
        let scan_event =
            EventHandlerBuilder::new().on_start_scan(ScanResults::new(&phy, vec![])).build();

        // Once a client interface is available for scanning, it takes up to around 30s for a scan
        // to complete (see fxb/109900). Allow at least double that amount of time to reduce
        // flakiness and longer than the timeout WLAN policy should have.
        match helper
            .run_until_complete_or_timeout(70.seconds(), "receive a scan response", scan_event, fut)
            .await
        {
            Err(_) => {
                retry.sleep_unless_after_deadline().await.unwrap_or_else(|_| {
                    panic!("Wlanstack did not recognize the interface in time")
                });
            }
            Ok(_) => return,
        }
    }
}

pub fn init_syslog() {
    diagnostics_log::init!(
        &[],
        diagnostics_log::Interest {
            min_severity: Some(diagnostics_log::Severity::Info),
            ..diagnostics_log::Interest::EMPTY
        }
    );
}
