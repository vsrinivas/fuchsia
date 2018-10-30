// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]

use {
    byteorder::{LittleEndian, ReadBytesExt},
    failure::format_err,
    fidl_fuchsia_wlan_device as wlan_device,
    fidl_fuchsia_wlan_mlme as wlan_mlme,
    fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async as fasync,
    fuchsia_zircon as zx,
    fuchsia_zircon::prelude::*,
    futures::{prelude::*, channel::mpsc},
    std::fs::{self, File},
    std::io::Cursor,
    std::sync::{Arc, Mutex},
    wlantap_client::Wlantap,
};

mod mac_frames;

#[cfg(test)]
mod test_utils;

const HW_MAC_ADDR: [u8; 6] = [0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b];
const BSSID: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x73, 0x73];

fn create_2_4_ghz_band_info() -> wlan_device::BandInfo {
    wlan_device::BandInfo{
        band_id: wlan_mlme::Band::WlanBand2Ghz,
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
            }

        })),
        vht_caps: None,
        basic_rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
        supported_channels: wlan_device::ChannelList{
            base_freq: 2407,
            channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
        }
    }
}

fn create_wlantap_config() -> wlantap::WlantapPhyConfig {
    use fidl_fuchsia_wlan_device::SupportedPhy;
    wlantap::WlantapPhyConfig {
        phy_info: wlan_device::PhyInfo{
            id: 0,
            dev_path: None,
            hw_mac_address: HW_MAC_ADDR,
            supported_phys: vec![
                SupportedPhy::Dsss, SupportedPhy::Cck, SupportedPhy::Ofdm, SupportedPhy::Ht
            ],
            driver_features: vec![],
            mac_roles: vec![wlan_device::MacRole::Client],
            caps: vec![],
            bands: vec![
                create_2_4_ghz_band_info()
            ]
        },
        name: String::from("wlantap0")
    }
}

struct State {
    current_channel: wlan_mlme::WlanChan,
    frame_buf: Vec<u8>,
    is_associated: bool,
}

impl State {
    fn new() -> Self {
        Self {
            current_channel: wlan_mlme::WlanChan {
                primary: 0,
                cbw: wlan_mlme::Cbw::Cbw20,
                secondary80: 0
            },
            frame_buf: vec![],
            is_associated: false,
        }
    }
}

fn send_beacon(frame_buf: &mut Vec<u8>, channel: &wlan_mlme::WlanChan, bss_id: &[u8; 6],
               ssid: &[u8], proxy: &wlantap::WlantapPhyProxy)
    -> Result<(), failure::Error>
{
    frame_buf.clear();
    mac_frames::MacFrameWriter::<&mut Vec<u8>>::new(frame_buf)
        .beacon(
            &mac_frames::MgmtHeader{
                frame_control: mac_frames::FrameControl(0), // will be filled automatically
                duration: 0,
                addr1: mac_frames::BROADCAST_ADDR.clone(),
                addr2: bss_id.clone(),
                addr3: bss_id.clone(),
                seq_control: mac_frames::SeqControl {
                    frag_num: 0,
                    seq_num: 123
                },
                ht_control: None
            },
            &mac_frames::BeaconFields{
                timestamp: 0,
                beacon_interval: 100,
                capability_info: mac_frames::CapabilityInfo(0),
            })?
        .ssid(ssid)?
        .supported_rates(&[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?
        .dsss_parameter_set(channel.primary)?;

    let rx_info = &mut create_rx_info(channel);
    proxy.rx(0, &mut frame_buf.iter().cloned(), rx_info)?;
    Ok(())
}

fn send_authentication(frame_buf: &mut Vec<u8>, channel: &wlan_mlme::WlanChan, bss_id: &[u8; 6],
                       proxy: &wlantap::WlantapPhyProxy)
    -> Result<(), failure::Error>
{
    frame_buf.clear();
    mac_frames::MacFrameWriter::<&mut Vec<u8>>::new(frame_buf).authentication(
        &mac_frames::MgmtHeader {
            frame_control: mac_frames::FrameControl(0), // will be filled automatically
            duration: 0,
            addr1: HW_MAC_ADDR,
            addr2: bss_id.clone(),
            addr3: bss_id.clone(),
            seq_control: mac_frames::SeqControl {
                frag_num: 0,
                seq_num: 123,
            },
            ht_control: None,
        },
        &mac_frames::AuthenticationFields {
            auth_algorithm_number: mac_frames::AuthAlgorithm::OpenSystem as u16,
            auth_txn_seq_number: 2, // Always 2 for successful authentication
            status_code: mac_frames::StatusCode::Success as u16,
        },
    )?;

    let rx_info = &mut create_rx_info(channel);
    proxy.rx(0, &mut frame_buf.iter().cloned(), rx_info)?;
    Ok(())
}

fn send_association_response(
    frame_buf: &mut Vec<u8>, channel: &wlan_mlme::WlanChan, bss_id: &[u8; 6],
    proxy: &wlantap::WlantapPhyProxy,
) -> Result<(), failure::Error> {
    frame_buf.clear();
    let mut cap_info = mac_frames::CapabilityInfo(0);
    cap_info.set_ess(true);
    cap_info.set_short_preamble(true);
    mac_frames::MacFrameWriter::<&mut Vec<u8>>::new(frame_buf).association_response(
        &mac_frames::MgmtHeader {
            frame_control: mac_frames::FrameControl(0), // will be filled automatically
            duration: 0,
            addr1: HW_MAC_ADDR,
            addr2: bss_id.clone(),
            addr3: bss_id.clone(),
            seq_control: mac_frames::SeqControl {
                frag_num: 0,
                seq_num: 123,
            },
            ht_control: None,
        },
        &mac_frames::AssociationResponseFields {
            capability_info: cap_info,
            status_code: 0,    // Success
            association_id: 2, // Can be any
        },
    )?;

    let rx_info = &mut create_rx_info(channel);
    proxy.rx(0, &mut frame_buf.iter().cloned(), rx_info)?;
    Ok(())
}

fn create_rx_info(channel: &wlan_mlme::WlanChan) -> wlantap::WlanRxInfo {
    wlantap::WlanRxInfo {
        rx_flags: 0,
        valid_fields: 0,
        phy: 0,
        data_rate: 0,
        chan: wlan_mlme::WlanChan { // TODO(FIDL-54): use clone()
            primary: channel.primary,
            cbw: channel.cbw,
            secondary80: channel.secondary80
        },
        mcs: 0,
        rssi_dbm: 0,
        rcpi_dbmh: 0,
        snr_dbh: 0,
    }
}

fn handle_tx(args: wlantap::TxArgs, state: &mut State, proxy: &wlantap::WlantapPhyProxy) {
    let mut reader = Cursor::new(args.packet.data);
    let frame_ctrl = reader.read_u16::<LittleEndian>().unwrap();
    let frame_ctrl = mac_frames::FrameControl(frame_ctrl);
    println!("Frame Control: type: {:?}, subtype: {:?}", frame_ctrl.typ(), frame_ctrl.subtype());
    if frame_ctrl.typ() == mac_frames::FrameControlType::Mgmt as u16 {
        if frame_ctrl.subtype() == mac_frames::MgmtSubtype::Authentication as u16 {
            println!("Authentication received.");
            send_authentication(&mut state.frame_buf, &state.current_channel, &BSSID, proxy)
                .expect("Error sending fake authentication frame.");
            println!("Authentication sent.");
        } else if frame_ctrl.subtype() == mac_frames::MgmtSubtype::AssociationRequest as u16 {
            println!("Association Request received.");
            send_association_response(&mut state.frame_buf, &state.current_channel, &BSSID, proxy)
                .expect("Error sending fake association response frame.");
            println!("Association Response sent.");
            state.is_associated = true;
        }
    }
}

async fn create_eth_client(mac: &[u8; 6]) -> Result<ethernet::Client, failure::Error> {
    const ETH_PATH : &str = "/dev/class/ethernet";
    let files = fs::read_dir(ETH_PATH)?;
    for file in files {
        let vmo = zx::Vmo::create_with_opts(
            zx::VmoOptions::NON_RESIZABLE,
            256 * ethernet::DEFAULT_BUFFER_SIZE as u64,
        )?;

        let path = file?.path();
        let dev = File::open(path)?;
        if let Ok(client) = await!(ethernet::Client::from_file(dev, vmo,
                                   ethernet::DEFAULT_BUFFER_SIZE, "wlan-hw-sim")) {
            if let Ok(info) = await!(client.info()) {
                if &info.mac.octets == mac { return Ok(client); }
            }
        }
    }
    Err(format_err!("No ethernet device found with MAC address {:?}", mac))
}

fn main() -> Result<(), failure::Error> {
    let mut exec = fasync::Executor::new()?;
    let wlantap = Wlantap::open()?;
    let state = Arc::new(Mutex::new(State::new()));
    let proxy = wlantap.create_phy(create_wlantap_config())?;
    let (sender, mut receiver) = mpsc::channel(1);

    let event_listener = async {
        let state = state.clone();
        let mut events = proxy.take_event_stream();
        while let Some(event) = await!(events.try_next()).unwrap() {
            match event {
                wlantap::WlantapPhyEvent::SetChannel { args } => {
                    let mut state = state.lock().unwrap();
                    state.current_channel = args.chan;
                    println!("setting channel to {:?}", state.current_channel);
                }
                wlantap::WlantapPhyEvent::Tx { args } => {
                    let mut state = state.lock().unwrap();
                    let was_associated = state.is_associated;
                    handle_tx(args, &mut state, &proxy);
                    if !was_associated && state.is_associated {
                        await!(sender.send(()));
                    }
                }
                _ => {}
            }
        }
    };

    let beacon_timer = async {
        let mut beacon_timer_stream = fasync::Interval::new(102_400_000.nanos());
        while let Some(_) = await!(beacon_timer_stream.next()) {
            let state = &mut *state.lock().unwrap();
            if state.current_channel.primary == 6 {
                if !state.is_associated { eprintln!("sending beacon!"); }
                send_beacon(&mut state.frame_buf, &state.current_channel, &BSSID,
                          "fakenet".as_bytes(), &proxy).unwrap();
            }
        }
    };

    let ethernet_sender = async {
        match await!(receiver.next()) {
            Some(()) => println !("associated signal received from mpsc channel"),
            other => {
                println!("mpsc channel returned {:?}", other);
                return;
            }
        };

        let client = await!(create_eth_client(&HW_MAC_ADDR))
                     .expect("cannot create ethernet client");
        println!("ethernet client created: {:?}", client);
        println!("info: {:?} status: {:?}", await!(client.info()).unwrap(),
                 await!(client.get_status()).unwrap());
    };

    exec.run_singlethreaded(event_listener.join3(beacon_timer, ethernet_sender));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        fidl_fuchsia_wlan_service as fidl_wlan_service,
        fuchsia_app as app,
    };

    const BSS_FOO: [u8; 6] = [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f];
    const SSID_FOO: &[u8] = b"foo";
    const BSS_BAR: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x72];
    const SSID_BAR: &[u8] = b"bar";
    const BSS_BAZ: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x7a];
    const SSID_BAZ: &[u8] = b"baz";

    #[test]
    fn simulate_scan() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let mut helper = test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config());

        let wlan_service = app::client::connect_to_service::<fidl_wlan_service::WlanMarker>()
            .expect("Failed to connect to wlan service");

        let proxy = helper.proxy();
        let scan_result = scan(&mut exec, &wlan_service, &proxy, &mut helper);

        assert_eq!(fidl_wlan_service::ErrCode::Ok, scan_result.error.code,
                   "The error message was: {}", scan_result.error.description);
        let mut aps:Vec<_> = scan_result.aps.expect("Got empty scan results")
            .into_iter().map(|ap| (ap.ssid, ap.bssid)).collect();
        aps.sort();
        let mut expected_aps = [
            ( String::from_utf8_lossy(SSID_FOO).to_string(), BSS_FOO.to_vec() ),
            ( String::from_utf8_lossy(SSID_BAR).to_string(), BSS_BAR.to_vec() ),
            ( String::from_utf8_lossy(SSID_BAZ).to_string(), BSS_BAZ.to_vec() ),
        ];
        expected_aps.sort();
        assert_eq!(&expected_aps, &aps[..]);
    }

    fn scan(exec: &mut fasync::Executor,
            wlan_service: &fidl_wlan_service::WlanProxy,
            phy: &wlantap::WlantapPhyProxy,
            helper: &mut test_utils::TestHelper) -> fidl_wlan_service::ScanResult {
        let mut wlanstack_retry = test_utils::RetryWithBackoff::new(5.seconds());
        loop {
            let scan_result = helper.run(exec, 10.seconds(), "receive a scan response",
               |event| {
                   match event {
                       wlantap::WlantapPhyEvent::SetChannel { args } => {
                           println!("set channel to {:?}", args.chan);
                           if args.chan.primary == 1 {
                               send_beacon(&mut vec![], &args.chan, &BSS_FOO, SSID_FOO, &phy)
                                   .unwrap();
                           } else if args.chan.primary == 6 {
                               send_beacon(&mut vec![], &args.chan, &BSS_BAR, SSID_BAR, &phy)
                                   .unwrap();
                           } else if args.chan.primary == 11 {
                               send_beacon(&mut vec![], &args.chan, &BSS_BAZ, SSID_BAZ, &phy)
                                   .unwrap();
                           }
                       },
                       _ => {}
                   }
               },
               wlan_service.scan(&mut fidl_wlan_service::ScanRequest { timeout: 5 })).unwrap();
            if scan_result.error.code == fidl_wlan_service::ErrCode::NotFound {
                let slept = wlanstack_retry.sleep_unless_timed_out();
                assert!(slept, "Wlanstack did not recognize the interface in time");
            } else {
                return scan_result;
            }
        }
    }
}
