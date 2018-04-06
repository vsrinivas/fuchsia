// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate byteorder;
extern crate failure;
extern crate fuchsia_async as async;
extern crate wlantap_client;
extern crate fidl_wlan_device;
extern crate fidl_wlantap;
extern crate futures;

use std::sync::{Arc, Mutex};
use wlantap_client::{Wlantap, WlantapListener};

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
}

impl State {
    fn new() -> Self {
        Self {
            current_channel: fidl_wlan_device::Channel {
                primary: 0,
                cbw: 0,
                secondary80: 0
            },
        }
    }
}

struct Listener {
    state: Arc<Mutex<State>>,
}

impl WlantapListener for Listener {
    fn set_channel(&mut self, _proxy: &fidl_wlantap::WlantapPhyProxy,
                   wlanmac_id: u16, channel: fidl_wlan_device::Channel) {
        let mut state = self.state.lock().unwrap();
        state.current_channel = channel;
        println!("setting channel to {:?}", state.current_channel);
    }
}

fn main() -> Result<(), failure::Error> {
    let mut exec = async::Executor::new()?;
    let wlantap = Wlantap::open()?;
    let state = Arc::new(Mutex::new(State::new()));
    let listener = Listener{ state: state.clone() };
    let (proxy, server) = wlantap.create_phy(create_wlantap_config(), listener)?;
    exec.run_singlethreaded(server)?;
    Ok(())
}
