// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]

use {
    byteorder::{LittleEndian, ReadBytesExt},
    fidl_fuchsia_wlan_device as wlan_device,
    fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_async as fasync,
    fuchsia_zircon::prelude::*,
    futures::prelude::*,
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
        description: String::from("2.4 GHz"),
        ht_caps: wlan_device::HtCapabilities{
            ht_capability_info: 0x01fe,
            ampdu_params: 0,
            supported_mcs_set: [
                // 0  1  2     3     4  5  6  7  8  9 10 11    12 13 14 15
                0xff, 0, 0, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0
            ],
            ht_ext_capabilities: 0,
            tx_beamforming_capabilities: 0,
            asel_capabilities: 0
        },
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
    current_channel: wlan_device::Channel,
    frame_buf: Vec<u8>,
}

impl State {
    fn new() -> Self {
        Self {
            current_channel: wlan_device::Channel {
                primary: 0,
                cbw: 0,
                secondary80: 0
            },
            frame_buf: vec![]
        }
    }
}

fn send_beacon(frame_buf: &mut Vec<u8>, channel: &wlan_device::Channel, bss_id: &[u8; 6],
               ssid: &str, proxy: &wlantap::WlantapPhyProxy)
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
        .ssid(ssid.as_bytes())?
        .supported_rates(&[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?
        .dsss_parameter_set(channel.primary)?;

    let rx_info = &mut create_rx_info(channel);
    proxy.rx(0, &mut frame_buf.iter().cloned(), rx_info)?;
    Ok(())
}

fn send_authentication(frame_buf: &mut Vec<u8>, channel: &wlan_device::Channel, bss_id: &[u8; 6],
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
    frame_buf: &mut Vec<u8>, channel: &wlan_device::Channel, bss_id: &[u8; 6],
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

fn create_rx_info(channel: &wlan_device::Channel) -> wlantap::WlanRxInfo {
    wlantap::WlanRxInfo {
        rx_flags: 0,
        valid_fields: 0,
        phy: 0,
        data_rate: 0,
        chan: wlan_device::Channel { // TODO(FIDL-54): use clone()
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
        }
    }
}

fn main() -> Result<(), failure::Error> {
    let mut exec = fasync::Executor::new()?;
    let wlantap = Wlantap::open()?;
    let state = Arc::new(Mutex::new(State::new()));
    let proxy = wlantap.create_phy(create_wlantap_config())?;
    let event_listener = async {
        let state = state.clone();
        let mut events = proxy.take_event_stream();
        while let Some(event) = await!(events.try_next()).unwrap() {
            match event {
                wlantap::WlantapPhyEvent::SetChannel{ args } => {
                    let mut state = state.lock().unwrap();
                    state.current_channel = args.chan;
                    println!("setting channel to {:?}", state.current_channel);
                }
                wlantap::WlantapPhyEvent::Tx { args } => {
                    let mut state = state.lock().unwrap();
                    handle_tx(args, &mut state, &proxy);
                },
                _ => {}
            }
        }
    };

    let beacon_timer = async {
        let mut beacon_timer_stream = fasync::Interval::new(102_400_000.nanos());
        while let Some(_) = await!(beacon_timer_stream.next()) {
            let state = &mut *state.lock().unwrap();
            if state.current_channel.primary == 6 {
                eprintln!("sending beacon!");
                send_beacon(&mut state.frame_buf, &state.current_channel,
                         &BSSID, "fakenet", &proxy).unwrap();
            }
            }
    };
    exec.run_singlethreaded(event_listener.join(beacon_timer));
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
    const SSID_FOO: &str = "foo";
    const BSS_BAR: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x72];
    const SSID_BAR: &str = "bar";
    const BSS_BAZ: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x7a];
    const SSID_BAZ: &str = "baz";

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
            ( SSID_FOO.to_string(), BSS_FOO.to_vec() ),
            ( SSID_BAR.to_string(), BSS_BAR.to_vec() ),
            ( SSID_BAZ.to_string(), BSS_BAZ.to_vec() ),
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
