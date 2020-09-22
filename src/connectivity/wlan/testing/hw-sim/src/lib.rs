// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_common::{Cbw, WlanChan},
    fidl_fuchsia_wlan_device::MacRole,
    fidl_fuchsia_wlan_policy::{
        self as wlan_policy, Credential, Empty, NetworkConfig, NetworkIdentifier, SecurityType,
    },
    fidl_fuchsia_wlan_service::{ConnectConfig, ErrCode, State, WlanProxy, WlanStatus},
    fidl_fuchsia_wlan_tap::{
        SetChannelArgs, TxArgs, WlanRxInfo, WlantapPhyConfig, WlantapPhyEvent, WlantapPhyProxy,
    },
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog,
    fuchsia_zircon::prelude::*,
    log::{debug, error},
    std::{future::Future, marker::Unpin},
    wlan_common::{
        bss::Protection,
        data_writer,
        ie::{
            rsn::{akm, cipher, rsne},
            wpa::WpaIe,
        },
        mac, mgmt_writer,
        organization::Oui,
        TimeUnit,
    },
    wlan_frame_writer::write_frame_with_dynamic_buf,
    wlan_rsn::rsna::SecAssocUpdate,
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
pub const AP_MAC_ADDR: mac::Bssid = mac::Bssid([0x70, 0xf1, 0x1c, 0x05, 0x2d, 0x7f]);
pub const ETH_DST_MAC: [u8; 6] = [0x65, 0x74, 0x68, 0x64, 0x73, 0x74];
pub const CHANNEL: WlanChan = WlanChan { primary: 1, secondary80: 0, cbw: Cbw::Cbw20 };
pub const WLANCFG_DEFAULT_AP_CHANNEL: WlanChan =
    WlanChan { primary: 11, secondary80: 0, cbw: Cbw::Cbw20 };

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

pub fn create_rx_info(channel: &WlanChan, rssi_dbm: i8) -> WlanRxInfo {
    // should match enum WlanRxInfoValid::RSSI in zircon/system/banjo/ddk.protocol.wlan.info/info.banjo
    const WLAN_RX_INFO_VALID_RSSI: u32 = 0x10;
    WlanRxInfo {
        rx_flags: 0,
        valid_fields: if rssi_dbm == 0 { 0 } else { WLAN_RX_INFO_VALID_RSSI },
        phy: 0,
        data_rate: 0,
        chan: WlanChan {
            // TODO(fxbug.dev/7391): use clone()
            primary: channel.primary,
            cbw: channel.cbw,
            secondary80: channel.secondary80,
        },
        mcs: 0,
        rssi_dbm,
        rcpi_dbmh: 0,
        snr_dbh: 0,
    }
}

pub fn send_beacon(
    channel: &WlanChan,
    bssid: &mac::Bssid,
    ssid: &[u8],
    protection: &Protection,
    proxy: &WlantapPhyProxy,
    rssi_dbm: i8,
) -> Result<(), anyhow::Error> {
    let rsne = default_wpa2_psk_rsne();
    let wpa1_ie = default_wpa1_vendor_ie();

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
            mac::BeaconHdr: &mac::BeaconHdr {
                timestamp: 0,
                // Unrealistically long beacon period so that auth/assoc don't timeout on slow bots.
                beacon_interval: TimeUnit::DEFAULT_BEACON_INTERVAL * 20u16,
                capabilities: mac::CapabilityInfo(0).with_privacy(*protection != Protection::Open),
            },
        },
        ies: {
            ssid: ssid,
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0xe0, 0x6c],
            extended_supported_rates: { /* continues from supported_rates */ },
            dsss_param_set: &ie::DsssParamSet { current_chan: channel.primary },
            rsne?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep | Protection::Wpa1 => None,
                Protection::Wpa1Wpa2Personal | Protection::Wpa2Personal => Some(&rsne),
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wpa1?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep => None,
                Protection::Wpa1 | Protection::Wpa1Wpa2Personal => Some(&wpa1_ie),
                Protection::Wpa2Personal => None,
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
        },
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, rssi_dbm))?;
    Ok(())
}

pub fn send_probe_resp(
    channel: &WlanChan,
    bssid: &mac::Bssid,
    ssid: &[u8],
    protection: &Protection,
    wsc_ie: Option<&[u8]>,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let rsne = default_wpa2_psk_rsne();
    let wpa1_ie = default_wpa1_vendor_ie();

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
            mac::ProbeRespHdr: &mac::ProbeRespHdr {
                timestamp: 0,
                // Unrealistically long beacon period so that auth/assoc don't timeout on slow bots.
                beacon_interval: TimeUnit::DEFAULT_BEACON_INTERVAL * 20u16,
                capabilities: mac::CapabilityInfo(0).with_ess(true).with_short_preamble(true),
            },
        },
        ies: {
            ssid: ssid,
            supported_rates: &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24, 0x30, 0x48, 0xe0, 0x6c],
            extended_supported_rates: { /* continues from supported_rates */ },
            dsss_param_set: &ie::DsssParamSet { current_chan: channel.primary },
            rsne?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep | Protection::Wpa1 => None,
                Protection::Wpa1Wpa2Personal | Protection::Wpa2Personal => Some(&rsne),
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wpa1?: match protection {
                Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
                Protection::Open | Protection::Wep => None,
                Protection::Wpa1 | Protection::Wpa1Wpa2Personal => Some(&wpa1_ie),
                Protection::Wpa2Personal => None,
                _ => panic!("unsupported fake beacon: {:?}", protection),
            },
            wsc?: wsc_ie,
        }
    })?;
    proxy.rx(0, &buf, &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_authentication(
    channel: &WlanChan,
    bssid: &mac::Bssid,
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
    channel: &WlanChan,
    bssid: &mac::Bssid,
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

fn default_wpa2_psk_rsne() -> wlan_common::ie::rsn::rsne::Rsne {
    let mut rsne = rsne::Rsne::new();
    rsne.group_data_cipher_suite = Some(cipher::Cipher::new_dot11(cipher::CCMP_128));
    rsne.pairwise_cipher_suites = vec![cipher::Cipher::new_dot11(cipher::CCMP_128)];
    rsne.akm_suites = vec![akm::Akm::new_dot11(akm::PSK)];
    rsne.rsn_capabilities = Some(rsne::RsnCapabilities(0));
    rsne
}

fn default_wpa1_vendor_ie() -> wlan_common::ie::wpa::WpaIe {
    WpaIe {
        unicast_cipher_list: vec![cipher::Cipher { oui: Oui::MSFT, suite_type: cipher::CCMP_128 }],
        akm_list: vec![akm::Akm { oui: Oui::MSFT, suite_type: akm::PSK }],
        ..Default::default()
    }
}

pub fn create_connect_config<S: ToString>(ssid: &[u8], passphrase: S) -> ConnectConfig {
    ConnectConfig {
        ssid: String::from_utf8_lossy(ssid).to_string(),
        pass_phrase: passphrase.to_string(),
        scan_interval: 5,
        bssid: "".to_string(), // BSSID is ignored by wlancfg
    }
}

pub fn create_network_config<S: ToString>(
    ssid: &[u8],
    security_type: SecurityType,
    password: Option<S>,
) -> NetworkConfig {
    let network_id = NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type };
    let credential = match password {
        None => Credential::None(Empty),
        Some(p) => Credential::Password(p.to_string().as_bytes().to_vec()),
    };
    NetworkConfig { id: Some(network_id), credential: Some(credential) }
}

pub fn create_open_network_config(ssid: &[u8]) -> NetworkConfig {
    create_network_config(ssid, SecurityType::None, None::<String>)
}

pub fn create_wpa2_network_config<S: ToString>(ssid: &[u8], password: S) -> NetworkConfig {
    create_network_config(ssid, SecurityType::Wpa2, Some(password))
}

pub fn create_wpa3_network_config<S: ToString>(ssid: &[u8], password: S) -> NetworkConfig {
    create_network_config(ssid, SecurityType::Wpa3, Some(password))
}

// WPA1 still needs to be tested until we remove support.
pub fn create_deprecated_wpa1_psk_authenticator(
    bssid: &mac::Bssid,
    ssid: &[u8],
    passphrase: &str,
) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(cipher::Cipher::new_dot11(cipher::CCMP_128))
        .expect("creating gtk provider");
    let psk = wlan_rsn::psk::compute(passphrase.as_bytes(), ssid).expect("computing PSK");
    let s_protection = wlan_rsn::ProtectionInfo::LegacyWpa(default_wpa1_vendor_ie());
    let a_protection = wlan_rsn::ProtectionInfo::LegacyWpa(default_wpa1_vendor_ie());
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

pub fn create_wpa2_psk_authenticator(
    bssid: &mac::Bssid,
    ssid: &[u8],
    passphrase: &str,
) -> wlan_rsn::Authenticator {
    let nonce_rdr = wlan_rsn::nonce::NonceReader::new(&bssid.0).expect("creating nonce reader");
    let gtk_provider = wlan_rsn::GtkProvider::new(cipher::Cipher::new_dot11(cipher::CCMP_128))
        .expect("creating gtk provider");
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

fn process_auth_update(
    updates: &mut wlan_rsn::rsna::UpdateSink,
    channel: &WlanChan,
    bssid: &mac::Bssid,
    phy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    for update in updates {
        if let SecAssocUpdate::TxEapolKeyFrame(frame) = update {
            rx_wlan_data_frame(
                channel,
                &CLIENT_MAC_ADDR,
                &bssid.0,
                &bssid.0,
                &frame[..],
                mac::ETHER_TYPE_EAPOL,
                phy,
            )?
        }
    }
    Ok(())
}

pub fn handle_set_channel_event(
    args: &SetChannelArgs,
    phy: &WlantapPhyProxy,
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
) {
    debug!("Handling set channel event on channel {:?}", args.chan);
    if args.chan.primary == CHANNEL.primary {
        send_beacon(&args.chan, bssid, ssid, protection, &phy, 0).unwrap();
    }
}

pub fn handle_tx_event(
    args: &TxArgs,
    phy: &WlantapPhyProxy,
    bssid: &mac::Bssid,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
) {
    debug!("Handling tx event.");
    match mac::MacFrame::parse(&args.packet.data[..], false) {
        Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
            match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                Some(mac::MgmtBody::Authentication { .. }) => {
                    send_authentication(&CHANNEL, bssid, &phy)
                        .expect("Error sending fake authentication frame.");
                }
                Some(mac::MgmtBody::AssociationReq { .. }) => {
                    send_association_response(&CHANNEL, bssid, mac::StatusCode::SUCCESS, &phy)
                        .expect("Error sending fake association response frame.");
                    if let Some(authenticator) = authenticator {
                        let mut updates = wlan_rsn::rsna::UpdateSink::default();
                        authenticator.initiate(&mut updates).expect("initiating authenticator");
                        process_auth_update(&mut updates, &CHANNEL, bssid, &phy)
                            .expect("processing authenticator updates after initiation");
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
                    let mut updates = wlan_rsn::rsna::UpdateSink::default();
                    let mic_size = authenticator.get_negotiated_protection().mic_size;
                    let frame_rx = eapol::KeyFrameRx::parse(mic_size as usize, llc_frame.body)
                        .expect("parsing EAPOL frame");
                    if let Err(e) =
                        authenticator.on_eapol_frame(&mut updates, eapol::Frame::Key(frame_rx))
                    {
                        error!("error sending EAPOL frame to authenticator: {}", e);
                    }
                    process_auth_update(&mut updates, &CHANNEL, bssid, &phy)
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
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
) {
    match event {
        WlantapPhyEvent::SetChannel { args } => {
            handle_set_channel_event(&args, phy, ssid, bssid, protection)
        }
        WlantapPhyEvent::Tx { args } => handle_tx_event(&args, phy, bssid, authenticator),
        _ => (),
    }
}

pub async fn connect_to_ap<F, R>(
    connect_fut: F,
    helper: &mut test_utils::TestHelper,
    ap_ssid: &[u8],
    ap_bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
) -> R
where
    F: Future<Output = R> + Unpin,
{
    let phy = helper.proxy();
    helper
        .run_until_complete_or_timeout(
            30.seconds(),
            format!("connecting to {} ({:02X?})", String::from_utf8_lossy(ap_ssid), ap_bssid),
            |event| {
                handle_connect_events(&event, &phy, ap_ssid, ap_bssid, protection, authenticator);
            },
            connect_fut,
        )
        .await
}

pub async fn connect_to_wpa2_ap(
    wlan_service: &WlanProxy,
    helper: &mut test_utils::TestHelper,
    ap_ssid: &[u8],
    ap_bssid: &mac::Bssid,
    passphrase: &str,
) {
    let mut connect_config = create_connect_config(ap_ssid, passphrase);
    let mut authenticator = Some(create_wpa2_psk_authenticator(ap_bssid, ap_ssid, passphrase));
    let protection = Protection::Wpa2Personal;
    let connect_fut = wlan_service.connect(&mut connect_config);
    let error =
        connect_to_ap(connect_fut, helper, ap_ssid, ap_bssid, &protection, &mut authenticator)
            .await
            .expect("connecting via wlancfg service");
    assert_eq!(error.code, ErrCode::Ok, "connect failed: {:?}", error);
}

pub async fn connect_to_open_ap(
    wlan_service: &WlanProxy,
    helper: &mut test_utils::TestHelper,
    ap_ssid: &[u8],
    ap_bssid: &mac::Bssid,
) {
    let mut connect_config = create_connect_config(ap_ssid, "");
    let protection = Protection::Open;
    let connect_fut = wlan_service.connect(&mut connect_config);
    let error = connect_to_ap(connect_fut, helper, ap_ssid, ap_bssid, &protection, &mut None)
        .await
        .expect("connecting via wlancfg service");
    assert_eq!(error.code, ErrCode::Ok, "connect failed: {:?}", error);
}

pub fn assert_associated_state(
    status: WlanStatus,
    bssid: &mac::Bssid,
    ssid: &[u8],
    channel: &WlanChan,
    is_secure: bool,
) {
    assert_eq!(status.error.code, ErrCode::Ok);
    assert_eq!(status.state, State::Associated);
    let ap = status.current_ap.expect("expect to be associated to an AP");
    assert_eq!(ap.bssid, bssid.0.to_vec());
    assert_eq!(ap.ssid, String::from_utf8_lossy(ssid).to_string());
    assert_eq!(ap.chan, *channel);
    assert!(ap.is_compatible);
    assert_eq!(ap.is_secure, is_secure);
}

pub fn rx_wlan_data_frame(
    channel: &WlanChan,
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
    let policy_provider = connect_to_service::<wlan_policy::ClientProviderMarker>()
        .expect("connecting to wlan policy");
    let (client_controller, server_end) = create_proxy().expect("creating client controller");
    let (update_client_end, _update_server_end) =
        create_endpoints().expect("creating client listener");
    let () =
        policy_provider.get_controller(server_end, update_client_end).expect("getting controller");

    // Attempt to issue a scan command until the request succeeds.  Scanning will fail until a
    // client interface is available.  A successful response to a scan request indicates that the
    // client policy layer is ready to use.
    // TODO(57415): Figure out a new way to signal that the client policy layer is ready to go.
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
