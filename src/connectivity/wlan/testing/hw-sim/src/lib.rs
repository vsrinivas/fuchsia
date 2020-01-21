// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common::{Cbw, WlanChan},
    fidl_fuchsia_wlan_device::MacRole,
    fidl_fuchsia_wlan_service::{ConnectConfig, ErrCode, State, WlanMarker, WlanProxy, WlanStatus},
    fidl_fuchsia_wlan_sme::ApConfig,
    fidl_fuchsia_wlan_tap::{WlanRxInfo, WlantapPhyConfig, WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::prelude::*,
    wlan_common::{
        appendable::Appendable,
        bss::Protection,
        channel, data_writer,
        ie::{
            self,
            rsn::{akm, cipher, rsne},
            wpa::WpaIe,
        },
        mac, mgmt_writer,
        organization::Oui,
        RadioConfig, TimeUnit,
    },
    wlan_rsn::rsna::SecAssocUpdate,
};

pub mod test_utils;
pub use eth_helper::*;
pub use event_handler_helper::*;
pub use wlanstack_helper::*;

mod config;
mod eth_helper;
mod event_handler_helper;
mod wlanstack_helper;

pub const CLIENT_MAC_ADDR: [u8; 6] = [0x66, 0x62, 0x6f, 0x6e, 0x69, 0x6b];
pub const AP_MAC_ADDR: mac::Bssid = mac::Bssid([0x70, 0xf1, 0x1c, 0x05, 0x2d, 0x7f]);
pub const ETH_DST_MAC: [u8; 6] = [0x64, 0x74, 0x68, 0x64, 0x73, 0x74];
pub const CHANNEL: WlanChan = WlanChan { primary: 1, secondary80: 0, cbw: Cbw::Cbw20 };

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

pub fn default_ap_config() -> ApConfig {
    ApConfig {
        ssid: String::from("fuchsia").into_bytes(),
        password: vec![],
        radio_cfg: RadioConfig::new(channel::Phy::Ht, channel::Cbw::Cbw20, CHANNEL.primary)
            .to_fidl(),
    }
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
            // TODO(FIDL-54): use clone()
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
    frame_buf: &mut Vec<u8>,
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    ssid: &[u8],
    protection: &Protection,
    proxy: &WlantapPhyProxy,
    rssi_dbm: i8,
) -> Result<(), anyhow::Error> {
    frame_buf.clear();

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::BEACON);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    const BROADCAST_ADDR: mac::MacAddr = [0xff; 6];
    mgmt_writer::write_mgmt_hdr(
        frame_buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, BROADCAST_ADDR, *bss_id, seq_ctrl),
        None,
    )?;

    frame_buf.append_value(&mac::BeaconHdr {
        timestamp: 0,
        beacon_interval: TimeUnit::DEFAULT_BEACON_INTERVAL,
        capabilities: mac::CapabilityInfo(0).with_privacy(*protection != Protection::Open),
    })?;

    ie::write_ssid(frame_buf, ssid)?;
    ie::write_supported_rates(frame_buf, &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?;
    ie::write_dsss_param_set(frame_buf, &ie::DsssParamSet { current_chan: channel.primary })?;
    match protection {
        Protection::Unknown => panic!("Cannot send beacon with unknown protection"),
        Protection::Open | Protection::Wep => (),
        Protection::Wpa1 => ie::write_wpa1_ie(frame_buf, &default_wpa1_vendor_ie())?,
        Protection::Wpa1Wpa2Personal => {
            default_wpa2_psk_rsne().write_into(frame_buf)?;
            ie::write_wpa1_ie(frame_buf, &default_wpa1_vendor_ie())?;
        }
        Protection::Wpa2Personal => default_wpa2_psk_rsne().write_into(frame_buf)?,
        _ => panic!("unsupported fake beacon: {:?}", protection),
    };

    proxy.rx(0, &mut frame_buf.iter().cloned(), &mut create_rx_info(channel, rssi_dbm))?;
    Ok(())
}

pub fn send_client_authentication(
    frame_buf: &mut Vec<u8>,
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    frame_buf.clear();

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::AUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    mgmt_writer::write_mgmt_hdr(
        frame_buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, *bss_id, CLIENT_MAC_ADDR, seq_ctrl),
        None,
    )?;

    frame_buf.append_value(&mac::AuthHdr {
        auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
        auth_txn_seq_num: 1,
        status_code: mac::StatusCode::SUCCESS,
    })?;

    proxy.rx(0, &mut frame_buf.iter().cloned(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_authentication(
    frame_buf: &mut Vec<u8>,
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    frame_buf.clear();

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::AUTH);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    mgmt_writer::write_mgmt_hdr(
        frame_buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, CLIENT_MAC_ADDR, *bss_id, seq_ctrl),
        None,
    )?;

    frame_buf.append_value(&mac::AuthHdr {
        auth_alg_num: mac::AuthAlgorithmNumber::OPEN,
        auth_txn_seq_num: 2,
        status_code: mac::StatusCode::SUCCESS,
    })?;

    proxy.rx(0, &mut frame_buf.iter().cloned(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_association_request(
    frame_buf: &mut Vec<u8>,
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    frame_buf.clear();

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_REQ);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    mgmt_writer::write_mgmt_hdr(
        frame_buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, *bss_id, CLIENT_MAC_ADDR, seq_ctrl),
        None,
    )?;

    frame_buf.append_value(&mac::AssocReqHdr {
        capabilities: mac::CapabilityInfo(0),
        listen_interval: 0,
    })?;

    ie::write_ssid(frame_buf, b"fuchsia")?;
    // tx_vec_idx:                            _     _     _   129   130     _   131   132
    ie::write_supported_rates(frame_buf, &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?;
    // tx_vec_idx:                            133 134 basic_135  136
    ie::write_ext_supported_rates(frame_buf, &[48, 72, 128 + 96, 108])?;

    proxy.rx(0, &mut frame_buf.iter().cloned(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub fn send_association_response(
    frame_buf: &mut Vec<u8>,
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    frame_buf.clear();

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::ASSOC_RESP);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    mgmt_writer::write_mgmt_hdr(
        frame_buf,
        mgmt_writer::mgmt_hdr_from_ap(frame_ctrl, CLIENT_MAC_ADDR, *bss_id, seq_ctrl),
        None,
    )?;

    let cap_info = mac::CapabilityInfo(0).with_ess(true).with_short_preamble(true);
    frame_buf.append_value(&mac::AssocRespHdr {
        capabilities: cap_info,
        status_code: mac::StatusCode::SUCCESS,
        aid: 2, // does not matter
    })?;

    // These rates will be captured in assoc_ctx to initialize Minstrel. 11b rates are ignored.
    // tx_vec_idx:                            _     _     _   129   130     _   131   132
    ie::write_supported_rates(frame_buf, &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?;
    // tx_vec_idx:                            133 134 basic_135  136
    ie::write_ext_supported_rates(frame_buf, &[48, 72, 128 + 96, 108])?;

    proxy.rx(0, &mut frame_buf.iter().cloned(), &mut create_rx_info(channel, 0))?;
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

fn create_wpa2_psk_authenticator(
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

fn handle_connect_events(
    event: WlantapPhyEvent,
    phy: &WlantapPhyProxy,
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
) {
    match event {
        WlantapPhyEvent::SetChannel { args } => {
            println!("channel: {:?}", args.chan);
            if args.chan.primary == CHANNEL.primary {
                send_beacon(&mut vec![], &args.chan, bssid, ssid, protection, &phy, 0).unwrap();
            }
        }
        WlantapPhyEvent::Tx { args } => {
            match mac::MacFrame::parse(&args.packet.data[..], false) {
                Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                    match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                        Some(mac::MgmtBody::Authentication { .. }) => {
                            send_authentication(&mut vec![], &CHANNEL, bssid, &phy)
                                .expect("Error sending fake authentication frame.");
                        }
                        Some(mac::MgmtBody::AssociationReq { .. }) => {
                            send_association_response(&mut vec![], &CHANNEL, bssid, &phy)
                                .expect("Error sending fake association response frame.");
                            if let Some(authenticator) = authenticator {
                                let mut updates = wlan_rsn::rsna::UpdateSink::default();
                                authenticator
                                    .initiate(&mut updates)
                                    .expect("initiating authenticator");
                                process_auth_update(&mut updates, &CHANNEL, bssid, &phy)
                                    .expect("processing authenticator updates after initiation");
                            }
                        }
                        _ => {}
                    }
                }
                // EAPOL frames are transmitted as data frames with LLC protocol being EAPOL
                Some(mac::MacFrame::Data { .. }) => {
                    let msdus =
                        mac::MsduIterator::from_raw_data_frame(&args.packet.data[..], false)
                            .expect("reading msdu from data frame");
                    for mac::Msdu { llc_frame, .. } in msdus {
                        assert_eq!(llc_frame.hdr.protocol_id.to_native(), mac::ETHER_TYPE_EAPOL);
                        if let Some(authenticator) = authenticator {
                            let mut updates = wlan_rsn::rsna::UpdateSink::default();
                            let mic_size = authenticator.get_negotiated_protection().mic_size;
                            let frame_rx =
                                eapol::KeyFrameRx::parse(mic_size as usize, llc_frame.body)
                                    .expect("parsing EAPOL frame");
                            if let Err(e) = authenticator
                                .on_eapol_frame(&mut updates, eapol::Frame::Key(frame_rx))
                            {
                                println!("error sending EAPOL frame to authenticator: {}", e);
                            }
                            process_auth_update(&mut updates, &CHANNEL, bssid, &phy)
                                .expect("processing authenticator updates after EAPOL frame");
                        }
                    }
                }
                _ => {}
            }
        }
        _ => {}
    }
}

pub async fn connect(
    wlan_service: &WlanProxy,
    phy: &WlantapPhyProxy,
    helper: &mut test_utils::TestHelper,
    ssid: &[u8],
    bssid: &mac::Bssid,
    passphrase: Option<&str>,
) {
    let mut connect_config = create_connect_config(ssid, passphrase.unwrap_or(&""));
    let mut authenticator = passphrase.map(|p| create_wpa2_psk_authenticator(bssid, ssid, p));
    let connect_fut = wlan_service.connect(&mut connect_config);
    let protection = match passphrase {
        Some(_) => Protection::Wpa2Personal,
        None => Protection::Open,
    };
    let error = helper
        .run_until_complete_or_timeout(
            30.seconds(),
            format!("connect to {}({:2x?})", String::from_utf8_lossy(ssid), bssid),
            |event| {
                handle_connect_events(event, &phy, ssid, bssid, &protection, &mut authenticator);
            },
            connect_fut,
        )
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
    let buf: &mut Vec<u8> = &mut vec![];

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_data_subtype(mac::DataSubtype(0))
        .with_from_ds(true);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(3);

    data_writer::write_data_hdr(
        buf,
        mac::FixedDataHdrFields {
            frame_ctrl,
            duration: 0,
            addr1: *addr1,
            addr2: *addr2,
            addr3: *addr3,
            seq_ctrl,
        },
        mac::OptionalDataHdrFields::none(),
    )?;

    data_writer::write_snap_llc_hdr(buf, ether_type)?;
    buf.append_bytes(payload)?;

    phy.rx(0, &mut buf.iter().cloned(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

pub async fn loop_until_iface_is_found() {
    let wlan_service = connect_to_service::<WlanMarker>().expect("connecting to wlan service");
    let mut retry = test_utils::RetryWithBackoff::infinite_with_max_interval(10.seconds());
    loop {
        let status = wlan_service.status().await.expect("getting wlan status");
        if status.error.code != ErrCode::Ok {
            let slept = retry.sleep_unless_timed_out().await;
            assert!(slept, "Wlanstack did not recognize the interface in time");
        } else {
            return;
        }
    }
}
