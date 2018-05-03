// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use] extern crate bitfield;
extern crate byteorder;
extern crate failure;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate wlantap_client;
extern crate fidl_wlan_device;
extern crate fidl_wlantap;
extern crate futures;

use std::sync::{Arc, Mutex};
use futures::prelude::*;
use wlantap_client::Wlantap;
use zx::prelude::*;

mod mac_frames;

fn create_2_4_ghz_band_info() -> fidl_wlan_device::BandInfo {
    fidl_wlan_device::BandInfo{
        description: String::from("2.4 GHz"),
        ht_caps: fidl_wlan_device::HtCapabilities{
            ht_capability_info: 0x01fe,
            ampdu_params: 0,
            supported_mcs_set: [
                // 0  1  2     3  4  5  6  7  8  9 10 11    12 13 14 15
                0xff, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0
            ],
            ht_ext_capabilities: 0,
            tx_beamforming_capabilities: 0,
            asel_capabilities: 0
        },
        vht_caps: None,
        basic_rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
        supported_channels: fidl_wlan_device::ChannelList{
            base_freq: 2407,
            channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
        }
    }
}

fn create_wlantap_config() -> fidl_wlantap::WlantapPhyConfig {
    use fidl_wlan_device::SupportedPhy;
    fidl_wlantap::WlantapPhyConfig {
        phy_info: fidl_wlan_device::PhyInfo{
            id: 0,
            dev_path: None,
            hw_mac_address: [ 0x67, 0x62, 0x6f, 0x6e, 0x69, 0x6b ],
            supported_phys: vec![
                SupportedPhy::Dsss, SupportedPhy::Cck, SupportedPhy::Ofdm, SupportedPhy::Ht
            ],
            driver_features: vec![],
            mac_roles: vec![fidl_wlan_device::MacRole::Client],
            caps: vec![],
            bands: vec![
                create_2_4_ghz_band_info()
            ]
        },
        name: String::from("wlantap0")
    }
}

struct State {
    current_channel: fidl_wlan_device::Channel,
    frame_buf: Vec<u8>,
}

impl State {
    fn new() -> Self {
        Self {
            current_channel: fidl_wlan_device::Channel {
                primary: 0,
                cbw: 0,
                secondary80: 0
            },
            frame_buf: vec![]
        }
    }
}

const CHANNEL: u8 = 6;
const BSS_ID: [u8; 6] = [ 0x62, 0x73, 0x73, 0x62, 0x73, 0x73 ];

fn send_beacon(frame_buf: &mut Vec<u8>, channel: &fidl_wlan_device::Channel,
               proxy: &fidl_wlantap::WlantapPhyProxy)
    -> Result<(), failure::Error>
{
    frame_buf.clear();
    mac_frames::MacFrameWriter::<&mut Vec<u8>>::new(frame_buf)
        .beacon(
            &mac_frames::MgmtHeader{
                frame_control: mac_frames::FrameControl(0), // will be filled automatically
                duration: 0,
                addr1: mac_frames::BROADCAST_ADDR.clone(),
                addr2: BSS_ID.clone(),
                addr3: BSS_ID.clone(),
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
        .ssid("fakenet".as_bytes())?
        .supported_rates(&[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?
        .dsss_parameter_set(CHANNEL)?;

    let rx_info = &mut fidl_wlantap::WlanRxInfo {
        rx_flags: 0,
        valid_fields: 0,
        phy: 0,
        data_rate: 0,
        chan: fidl_wlan_device::Channel { // TODO(FIDL-54): use clone()
            primary: channel.primary,
            cbw: channel.cbw,
            secondary80: channel.secondary80
        },
        mcs: 0,
        rssi: 0,
        rcpi: 0,
        snr: 0,
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
        proxy.take_event_stream().for_each(move |event| {
            match event {
                fidl_wlantap::WlantapPhyEvent::SetChannel{ args } => {
                    let mut state = state.lock().unwrap();
                    state.current_channel = args.chan;
                    println!("setting channel to {:?}", state.current_channel);
                },
                _ => {}
            }
            Ok(())
        })
        .map(|_| ())
        .recover(|e| eprintln!("error running wlantap event listener: {:?}", e))
    };
    let beacon_timer = async::Interval::<zx::Status>::new(102_400_000.nanos())
        .for_each(move |_| {
            let state = &mut *state.lock().map_err(|e| {
                eprintln!("beacon timer callback: Failed to lock mutex: {:?}", e);
                zx::Status::INTERNAL
            })?;
            if state.current_channel.primary == CHANNEL {
                eprintln!("sending beacon!");
                send_beacon(&mut state.frame_buf, &state.current_channel, &proxy);
            }
            Ok(())
        })
        .map(|_| ())
        .recover::<Never, _>(|e| eprintln!("error running beacon timer: {:?}", e));
    exec.run_singlethreaded(event_listener.join(beacon_timer));
    Ok(())
}
