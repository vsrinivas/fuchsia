// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device::MacRole,
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fidl_fuchsia_wlan_policy::{
        self as fidl_policy, Credential, Empty, NetworkConfig, NetworkIdentifier, SecurityType,
    },
    fidl_fuchsia_wlan_tap::{
        SetChannelArgs, TxArgs, WlanRxInfo, WlantapPhyConfig, WlantapPhyEvent, WlantapPhyProxy,
    },
    fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog as syslog,
    fuchsia_zircon::prelude::*,
    ieee80211::{Bssid, Ssid},
    lazy_static::lazy_static,
    log::{debug, error},
    pin_utils::pin_mut,
    std::{future::Future, marker::Unpin},
    wlan_common::{
        bss::Protection,
        data_writer,
        ie::{
            rsn::{
                cipher::{CIPHER_CCMP_128, CIPHER_TKIP},
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
pub use eth_helper::*;
pub use event_handler_helper::*;
pub use wlancfg_helper::*;
pub use wlanstack_helper::*;

mod config;
mod eth_helper;
mod event_handler_helper;
mod wlancfg_helper;
mod wlanstack_helper;

pub const CLIENT_MAC_ADDR: [u8; 6] = [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b];
pub const AP_MAC_ADDR: Bssid = Bssid([0x70, 0xf1, 0x1c, 0x05, 0x2d, 0x7f]);
lazy_static! {
    pub static ref AP_SSID: Ssid = Ssid::from("ap_ssid");
}
pub const ETH_DST_MAC: [u8; 6] = [0x65, 0x74, 0x68, 0x64, 0x73, 0x74];
pub const CHANNEL: fidl_common::WlanChannel = fidl_common::WlanChannel {
    primary: 1,
    secondary80: 0,
    cbw: fidl_common::ChannelBandwidth::Cbw20,
};
pub const WLANCFG_DEFAULT_AP_CHANNEL: fidl_common::WlanChannel = fidl_common::WlanChannel {
    primary: 11,
    secondary80: 0,
    cbw: fidl_common::ChannelBandwidth::Cbw20,
};

pub fn default_wlantap_config_client() -> WlantapPhyConfig {
    wlantap_config_client(format!("wlantap-client"), CLIENT_MAC_ADDR)
}

pub fn wlantap_config_client(name: String, mac_addr: [u8; 6]) -> WlantapPhyConfig {
    config::create_wlantap_config(name, mac_addr, MacRole::Client)
}

pub fn default_wlantap_config_ap() -> WlantapPhyConfig {
    wlantap_config_ap(format!("wlantap-ap"), AP_MAC_ADDR.0)
}

pub fn wlantap_config_ap(name: String, mac_addr: [u8; 6]) -> WlantapPhyConfig {
    config::create_wlantap_config(name, mac_addr, MacRole::Ap)
}

pub fn create_rx_info(channel: &fidl_common::WlanChannel, rssi_dbm: i8) -> WlanRxInfo {
    // should match enum WlanRxInfoValid::RSSI in zircon/system/banjo/fuchsia.hardware.wlan.info/info.banjo
    const WLAN_RX_INFO_VALID_RSSI: u32 = 0x10;
    WlanRxInfo {
        rx_flags: 0,
        valid_fields: if rssi_dbm == 0 { 0 } else { WLAN_RX_INFO_VALID_RSSI },
        phy: 0,
        data_rate: 0,
        channel: fidl_common::WlanChannel {
            // TODO(fxbug.dev/7391): use clone()
            primary: channel.primary,
            cbw: channel.cbw,
            secondary80: channel.secondary80,
        },
        mcs: 0,
        rssi_dbm,
        snr_dbh: 0,
    }
}

pub fn send_beacon(
    channel: &fidl_common::WlanChannel,
    bssid: &Bssid,
    ssid: &Ssid,
    protection: &Protection,
    proxy: &WlantapPhyProxy,
    rssi_dbm: i8,
) -> Result<(), anyhow::Error> {
    let wpa1_ie = default_deprecated_wpa1_vendor_ie();

    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::BEACON),
                mac::BCAST_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::BeaconHdr: &mac::BeaconHdr::new(
                // Unrealistically long beacon period so that auth/assoc don't timeout on slow bots.
                TimeUnit::DEFAULT_BEACON_INTERVAL * 20u16,
                mac::CapabilityInfo(0).with_privacy(*protection != Protection::Open),
            ),
        },
        ies: {
            ssid: ssid,
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0xe0, 0x6c],
            extended_supported_rates: { /* continues from supported_rates */ },
            dsss_param_set: &ie::DsssParamSet { current_channel: channel.primary },
            rsne?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep | Protection::Wpa1 => None,
                Protection::Wpa1Wpa2Personal | Protection::Wpa2Personal => Some(default_wpa2_psk_rsne()),
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
        },
    })?;
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
) -> Result<(), anyhow::Error> {
    let wpa1_ie = default_deprecated_wpa1_vendor_ie();

    let (buf, _bytes_written) = write_frame_with_dynamic_buf!(vec![], {
        headers: {
            mac::MgmtHdr: &mgmt_writer::mgmt_hdr_from_ap(
                mac::FrameControl(0)
                    .with_frame_type(mac::FrameType::MGMT)
                    .with_mgmt_subtype(mac::MgmtSubtype::PROBE_RESP),
                CLIENT_MAC_ADDR,
                *bssid,
                mac::SequenceControl(0).with_seq_num(123),
            ),
            mac::ProbeRespHdr: &mac::ProbeRespHdr::new(
                // Unrealistically long beacon period so that auth/assoc don't timeout on slow bots.
                TimeUnit::DEFAULT_BEACON_INTERVAL * 20u16,
                mac::CapabilityInfo(0).with_ess(true).with_short_preamble(true),
            ),
        },
        ies: {
            ssid: ssid,
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0xe0, 0x6c],
            extended_supported_rates: { /* continues from supported_rates */ },
            dsss_param_set: &ie::DsssParamSet { current_channel: channel.primary },
            rsne?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep | Protection::Wpa1 => None,
                Protection::Wpa1Wpa2Personal | Protection::Wpa2Personal => Some(default_wpa2_psk_rsne()),
                Protection::Wpa2Wpa3Personal | Protection::Wpa3Personal => Some(rsne::Rsne::wpa3_rsne()),
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wpa1?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep => None,
                Protection::Wpa1 | Protection::Wpa1Wpa2Personal => Some(&wpa1_ie),
                Protection::Wpa2Personal | Protection::Wpa2Wpa3Personal | Protection::Wpa3Personal => None,
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wsc?: wsc_ie,
        }
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
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
                status_code: convert_to_mac_status_code(sae_frame.status_code),
            },
        },
        body: &sae_frame.sae_fields[..],
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

fn convert_to_mac_status_code(fidl_status_code: fidl_ieee80211::StatusCode) -> mac::StatusCode {
    match fidl_status_code {
        fidl_ieee80211::StatusCode::Success => mac::StatusCode::SUCCESS,
        fidl_ieee80211::StatusCode::RefusedReasonUnspecified => mac::StatusCode::REFUSED,
        fidl_ieee80211::StatusCode::AntiCloggingTokenRequired => {
            mac::StatusCode::ANTI_CLOGGING_TOKEN_REQUIRED
        }
        fidl_ieee80211::StatusCode::UnsupportedFiniteCyclicGroup => {
            mac::StatusCode::UNSUPPORTED_FINITE_CYCLIC_GROUP
        }
        fidl_ieee80211::StatusCode::RejectedSequenceTimeout => {
            mac::StatusCode::REJECTED_SEQUENCE_TIMEOUT
        }
        _ => mac::StatusCode::REFUSED,
    }
}

fn convert_to_ieee80211_status_code(
    mac_status_code: mac::StatusCode,
) -> fidl_ieee80211::StatusCode {
    match mac_status_code {
        mac::StatusCode::SUCCESS => fidl_ieee80211::StatusCode::Success,
        mac::StatusCode::REFUSED => fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
        mac::StatusCode::ANTI_CLOGGING_TOKEN_REQUIRED => {
            fidl_ieee80211::StatusCode::AntiCloggingTokenRequired
        }
        mac::StatusCode::UNSUPPORTED_FINITE_CYCLIC_GROUP => {
            fidl_ieee80211::StatusCode::UnsupportedFiniteCyclicGroup
        }
        mac::StatusCode::REJECTED_SEQUENCE_TIMEOUT => {
            fidl_ieee80211::StatusCode::RejectedSequenceTimeout
        }
        _ => fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
    }
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
                status_code: mac::StatusCode::SUCCESS,
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

fn default_wpa2_psk_rsne() -> wlan_common::ie::rsn::rsne::Rsne {
    rsne::Rsne::wpa2_rsne()
}

fn default_deprecated_wpa1_vendor_ie() -> wlan_common::ie::wpa::WpaIe {
    wpa::fake_wpa_ies::fake_deprecated_wpa1_vendor_ie()
}

pub fn create_network_config<S: ToString>(
    ssid: &Ssid,
    security_type: SecurityType,
    password: Option<S>,
) -> NetworkConfig {
    let network_id = NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type };
    let credential = match password {
        None => Credential::None(Empty),
        Some(p) => Credential::Password(p.to_string().as_bytes().to_vec()),
    };
    NetworkConfig { id: Some(network_id), credential: Some(credential), ..NetworkConfig::EMPTY }
}

pub fn create_open_network_config(ssid: &Ssid) -> NetworkConfig {
    create_network_config(ssid, SecurityType::None, None::<String>)
}

pub fn create_wpa2_network_config<S: ToString>(ssid: &Ssid, password: S) -> NetworkConfig {
    create_network_config(ssid, SecurityType::Wpa2, Some(password))
}

pub fn create_wpa3_network_config<S: ToString>(ssid: &Ssid, password: S) -> NetworkConfig {
    create_network_config(ssid, SecurityType::Wpa3, Some(password))
}

// WPA1 still needs to be tested until we remove support.
pub fn create_deprecated_wpa1_psk_authenticator(
    bssid: &Bssid,
    ssid: &Ssid,
    passphrase: &str,
) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(CIPHER_TKIP).expect("creating gtk provider");
    let psk = wlan_rsn::psk::compute(passphrase.as_bytes(), ssid).expect("computing PSK");
    let s_protection = wlan_rsn::ProtectionInfo::LegacyWpa(default_deprecated_wpa1_vendor_ie());
    let a_protection = wlan_rsn::ProtectionInfo::LegacyWpa(default_deprecated_wpa1_vendor_ie());
    wlan_rsn::Authenticator::new_wpa2psk_ccmp128(
        nonce_rdr,
        std::sync::Arc::new(std::sync::Mutex::new(gtk_provider)),
        psk,
        CLIENT_MAC_ADDR,
        s_protection,
        bssid.0,
        a_protection,
    )
    .expect("creating authenticator")
}

pub fn create_wpa3_authenticator(bssid: &Bssid, passphrase: &str) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(CIPHER_CCMP_128).expect("creating gtk provider");
    let igtk_provider =
        wlan_rsn::IgtkProvider::new(DEFAULT_GROUP_MGMT_CIPHER).expect("creating igtk provider");
    let password = passphrase.as_bytes().to_vec();
    let s_rsne = wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa3_rsne());
    let a_rsne = wlan_rsn::ProtectionInfo::Rsne(rsne::Rsne::wpa3_rsne());
    wlan_rsn::Authenticator::new_wpa3(
        nonce_rdr,
        std::sync::Arc::new(std::sync::Mutex::new(gtk_provider)),
        std::sync::Arc::new(std::sync::Mutex::new(igtk_provider)),
        password,
        CLIENT_MAC_ADDR,
        s_rsne,
        bssid.0,
        a_rsne,
    )
    .expect("creating authenticator")
}

pub fn create_wpa2_psk_authenticator(
    bssid: &Bssid,
    ssid: &Ssid,
    passphrase: &str,
) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(CIPHER_CCMP_128).expect("creating gtk provider");
    let psk = wlan_rsn::psk::compute(passphrase.as_bytes(), ssid).expect("computing PSK");
    let s_rsne = wlan_rsn::ProtectionInfo::Rsne(default_wpa2_psk_rsne());
    let a_rsne = wlan_rsn::ProtectionInfo::Rsne(default_wpa2_psk_rsne());
    wlan_rsn::Authenticator::new_wpa2psk_ccmp128(
        nonce_rdr,
        std::sync::Arc::new(std::sync::Mutex::new(gtk_provider)),
        psk,
        CLIENT_MAC_ADDR,
        s_rsne,
        bssid.0,
        a_rsne,
    )
    .expect("creating authenticator")
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
                send_sae_authentication_frame(&sae_frame, &CHANNEL, bssid, &phy)
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

pub fn handle_set_channel_event(
    args: &SetChannelArgs,
    phy: &WlantapPhyProxy,
    ssid: &Ssid,
    bssid: &Bssid,
    protection: &Protection,
) {
    debug!("Handling set channel event on channel {:?}", args.channel);
    if args.channel.primary == CHANNEL.primary {
        send_beacon(&args.channel, bssid, ssid, protection, &phy, 0).unwrap();
    }
}

pub fn handle_tx_event<F>(
    args: &TxArgs,
    phy: &WlantapPhyProxy,
    bssid: &Bssid,
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
                    // Reset authenticator state just in case this is not the 1st connection attempt
                    // SAE handshake uses multiple frames, so only reset on the first auth frame.
                    // Note: If we want to test retransmission of auth frame 1, we should find
                    //       a different way to do this.
                    if auth_hdr.auth_txn_seq_num == 1 {
                        authenticator.as_mut().map(|authenticator| authenticator.reset());
                    }

                    match auth_hdr.auth_alg_num {
                        mac::AuthAlgorithmNumber::OPEN => match authenticator {
                            Some(wlan_rsn::Authenticator {
                                auth_cfg: wlan_rsn::auth::Config::ComputedPsk(_),
                                ..
                            })
                            | None => {
                                send_open_authentication_success(&CHANNEL, bssid, &phy)
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
                            authenticator
                                .on_sae_frame_rx(
                                    &mut update_sink,
                                    fidl_mlme::SaeFrame {
                                        peer_sta_address: bssid.0,
                                        status_code: convert_to_ieee80211_status_code(
                                            auth_hdr.status_code,
                                        ),
                                        seq_num: auth_hdr.auth_txn_seq_num,
                                        sae_fields: elements.to_vec(),
                                    },
                                )
                                .expect("processing SAE frame");
                            process_auth_update(
                                &mut authenticator,
                                &mut update_sink,
                                &CHANNEL,
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
                    send_association_response(&CHANNEL, bssid, mac::StatusCode::SUCCESS, &phy)
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
                        process_auth_update(authenticator, &mut update_sink, &CHANNEL, bssid, &phy, true, true).expect(
                            "processing authenticator updates immediately after association complete",
                        );
                    }
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
                        &CHANNEL,
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
        WlantapPhyEvent::SetChannel { args } => {
            handle_set_channel_event(&args, phy, ssid, bssid, protection)
        }
        WlantapPhyEvent::Tx { args } => match authenticator {
            Some(_) => match update_sink {
                Some(_) => handle_tx_event(
                    &args,
                    phy,
                    bssid,
                    authenticator,
                    update_sink,
                    process_tx_auth_updates,
                ),
                None => panic!("No UpdateSink provided with Authenticator."),
            },
            None => {
                handle_tx_event(&args, phy, bssid, &mut None, &mut None, process_tx_auth_updates)
            }
        },
        _ => (),
    }
}

pub async fn save_network_and_wait_until_connected(
    ssid: &Ssid,
    security_type: fidl_policy::SecurityType,
    password: Option<&str>,
) -> (fidl_policy::ClientControllerProxy, fidl_policy::ClientStateUpdatesRequestStream) {
    // Connect to the client policy service and get a client controller.
    let (client_controller, mut client_state_update_stream) =
        wlancfg_helper::init_client_controller().await;

    save_network(&client_controller, ssid, security_type, password).await;
    assert_connecting(
        &mut client_state_update_stream,
        fidl_policy::NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type },
    )
    .await;

    // Wait until the policy layer indicates that the client has successfully connected.
    wait_until_client_state(&mut client_state_update_stream, |update| {
        has_ssid_and_state(update, ssid, fidl_policy::ConnectionState::Connected)
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
            30.seconds(),
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

async fn connect_with_security_type(
    helper: &mut test_utils::TestHelper,
    ssid: &Ssid,
    bssid: &Bssid,
    passphrase: Option<&str>,
    security_type: fidl_policy::SecurityType,
) {
    let connect_fut = save_network_and_wait_until_connected(ssid, security_type, passphrase);
    pin_mut!(connect_fut);

    // Validate the connect request.
    let (mut authenticator, mut update_sink, protection) = match security_type {
        fidl_policy::SecurityType::Wpa3 => (
            passphrase.map(|p| create_wpa3_authenticator(bssid, p)),
            Some(wlan_rsn::rsna::UpdateSink::default()),
            Protection::Wpa3Personal,
        ),
        fidl_policy::SecurityType::Wpa2 => (
            passphrase.map(|p| create_wpa2_psk_authenticator(bssid, ssid, p)),
            Some(wlan_rsn::rsna::UpdateSink::default()),
            Protection::Wpa2Personal,
        ),
        fidl_policy::SecurityType::Wpa => (
            passphrase.map(|p| create_deprecated_wpa1_psk_authenticator(bssid, ssid, p)),
            Some(wlan_rsn::rsna::UpdateSink::default()),
            Protection::Wpa1,
        ),
        fidl_policy::SecurityType::Wep => {
            panic!("hw-sim does not support connecting to a AP with WEP security type")
        }
        fidl_policy::SecurityType::None => (None, None, Protection::Open),
    };

    connect_to_ap(
        connect_fut,
        helper,
        ssid,
        bssid,
        &protection,
        &mut authenticator,
        &mut update_sink,
    )
    .await;
}

pub async fn connect_wpa3(
    helper: &mut test_utils::TestHelper,
    ssid: &Ssid,
    bssid: &Bssid,
    passphrase: &str,
) {
    connect_with_security_type(
        helper,
        ssid,
        bssid,
        Some(passphrase),
        fidl_policy::SecurityType::Wpa3,
    )
    .await;
}

pub async fn connect_wpa2(
    helper: &mut test_utils::TestHelper,
    ssid: &Ssid,
    bssid: &Bssid,
    passphrase: &str,
) {
    connect_with_security_type(
        helper,
        ssid,
        bssid,
        Some(passphrase),
        fidl_policy::SecurityType::Wpa2,
    )
    .await;
}

pub async fn connect_deprecated_wpa1(
    helper: &mut test_utils::TestHelper,
    ssid: &Ssid,
    bssid: &Bssid,
    passphrase: &str,
) {
    connect_with_security_type(
        helper,
        ssid,
        bssid,
        Some(passphrase),
        fidl_policy::SecurityType::Wpa,
    )
    .await;
}

pub async fn connect_open(helper: &mut test_utils::TestHelper, ssid: &Ssid, bssid: &Bssid) {
    connect_with_security_type(helper, ssid, bssid, None, fidl_policy::SecurityType::None).await;
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

pub async fn loop_until_iface_is_found() {
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

        match scan_proxy.get_next().await.expect("getting scan results") {
            Err(_) => {
                let slept = retry.sleep_unless_timed_out().await;
                assert!(slept, "Wlanstack did not recognize the interface in time");
            }
            Ok(_) => return,
        }
    }
}

pub fn init_syslog() {
    syslog::init().unwrap();

    // Change the severity to DEBUG in order to increase the verbosity of hw-sim logging.
    // For example, DEBUG level logging prints more information about the internal state of
    // the main_future polled by TestHelper::run_until_complete_or_timeout during a test.
    syslog::set_severity(syslog::levels::INFO);
}
