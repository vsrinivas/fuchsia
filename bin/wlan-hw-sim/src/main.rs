// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, pin, arbitrary_self_types)]
#![deny(warnings)]

#[macro_use] extern crate bitfield;
extern crate byteorder;
extern crate failure;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate wlantap_client;
extern crate fidl_fuchsia_wlan_device as wlan_device;
extern crate fidl_fuchsia_wlan_service as fidl_wlan_service;
extern crate fidl_fuchsia_wlan_tap as wlantap;
#[cfg_attr(test, macro_use)] extern crate futures;

use futures::prelude::*;
use std::sync::{Arc, Mutex};
use wlantap_client::Wlantap;
use zx::prelude::*;

mod mac_frames;

#[cfg(test)]
mod test_utils;

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
    use wlan_device::SupportedPhy;
    wlantap::WlantapPhyConfig {
        phy_info: wlan_device::PhyInfo{
            id: 0,
            dev_path: None,
            hw_mac_address: [ 0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b ],
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
                capability_info: 0,
            })?
        .ssid(ssid.as_bytes())?
        .supported_rates(&[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?
        .dsss_parameter_set(channel.primary)?;

    let rx_info = &mut wlantap::WlanRxInfo {
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
    };
    proxy.rx(0, &mut frame_buf.iter().cloned(), rx_info)?;
    Ok(())
}

fn main() -> Result<(), failure::Error> {
    let mut exec = async::Executor::new()?;
    let wlantap = Wlantap::open()?;
    let state = Arc::new(Mutex::new(State::new()));
    let proxy = wlantap.create_phy(create_wlantap_config())?;
    let event_listener = {
        let state = state.clone();
        proxy.take_event_stream().try_for_each(move |event| {
            match event {
                wlantap::WlantapPhyEvent::SetChannel{ args } => {
                    let mut state = state.lock().unwrap();
                    state.current_channel = args.chan;
                    println!("setting channel to {:?}", state.current_channel);
                },
                _ => {}
            }
            future::ready(Ok(()))
        })
        .unwrap_or_else(|e| eprintln!("error running wlantap event listener: {:?}", e))
    };
    let beacon_timer = async::Interval::new(102_400_000.nanos())
        .for_each(move |_| {
            let state = &mut *state.lock().unwrap();
            if state.current_channel.primary == 6 {
                eprintln!("sending beacon!");
                send_beacon(&mut state.frame_buf, &state.current_channel,
                            &[0x62, 0x73, 0x73, 0x62, 0x73, 0x73], "fakenet", &proxy).unwrap();
            }
            future::ready(())
        });
    let ((), ()) = exec.run_singlethreaded(event_listener.join(beacon_timer));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const BSS_FOO: [u8; 6] = [0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f];
    const SSID_FOO: &str = "foo";
    const BSS_BAR: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x72];
    const SSID_BAR: &str = "bar";
    const BSS_BAZ: [u8; 6] = [0x62, 0x73, 0x73, 0x62, 0x61, 0x7a];
    const SSID_BAZ: &str = "baz";

    #[test]
    fn simulate_scan() {
        let mut exec = async::Executor::new().expect("Failed to create an executor");
        let mut helper = test_utils::TestHelper::begin_test(&mut exec, create_wlantap_config());

        let wlan_service = app::client::connect_to_service::<fidl_wlan_service::WlanMarker>()
            .expect("Failed to connect to wlan service");

        // A temporary workaround for NET-1102
        // TODO(gbonik): remove this once we transition to wlanstack2
        ::std::thread::sleep(::std::time::Duration::from_millis(500));

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

    fn scan(exec: &mut async::Executor,
            wlan_service: &fidl_wlan_service::WlanProxy,
            phy: &wlantap::WlantapPhyProxy,
            helper: &mut test_utils::TestHelper) -> fidl_wlan_service::ScanResult {
        let mut wlanstack_retry = test_utils::RetryWithBackoff::new(1.seconds());
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
