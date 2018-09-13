// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::format_err;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use fidl_fuchsia_wlan_device as fidl_wlan_dev;
use fuchsia_wlan_dev as wlan_dev;
use fuchsia_vfs_watcher::{Watcher, WatchEvent};
use fuchsia_zircon::Status as zx_Status;
use futures::prelude::*;
use log::{error, info};
use std::io;
use std::fs::File;
use std::path::{Path, PathBuf};
use std::str::FromStr;

const PHY_PATH: &str = "/dev/class/wlanphy";
const IFACE_PATH: &str = "/dev/class/wlanif";

pub struct NewPhyDevice {
    pub id: u16,
    pub proxy: fidl_wlan_dev::PhyProxy,
    pub device: wlan_dev::Device,
}

pub struct NewIfaceDevice {
    pub id: u16,
    pub proxy: fidl_mlme::MlmeProxy,
    pub device: wlan_dev::Device,
}

pub fn watch_phy_devices()
    -> io::Result<impl Stream<Item = io::Result<NewPhyDevice>>>
{
    Ok(watch_new_devices(PHY_PATH)?
        .try_filter_map(|path| future::ready(Ok(handle_open_error(&path, new_phy(&path))))))
}

pub fn watch_iface_devices()
    -> io::Result<impl Stream<Item = io::Result<NewIfaceDevice>>>
{
    Ok(watch_new_devices(IFACE_PATH)?
        .try_filter_map(|path| {
            future::ready(Ok(handle_open_error(&path, new_iface(&path))))
        }))
}

fn handle_open_error<T>(path: &PathBuf, r: Result<T, failure::Error>) -> Option<T> {
    if let Err(ref e) = &r {
        if let Some(&zx_Status::ALREADY_BOUND) = e.cause().downcast_ref::<zx_Status>() {
            info!("iface {:?} already open, deferring", path.display())
        } else {
            error!("Error opening device '{}': {}", path.display(), e);
        }
    }
    r.ok()
}

fn watch_new_devices<P: AsRef<Path>>(path: P)
    -> io::Result<impl Stream<Item = io::Result<PathBuf>>>
{
    let dir = File::open(&path)?;
    let watcher = Watcher::new(&dir)?;
    Ok(watcher.try_filter_map(move |msg| {
        future::ready(Ok(match msg.event {
            WatchEvent::EXISTING | WatchEvent::ADD_FILE => Some(path.as_ref().join(msg.filename)),
            _ => None
        }))
    }))
}

fn new_phy(path: &PathBuf) -> Result<NewPhyDevice, failure::Error> {
    let id = id_from_path(path)?;
    let device = wlan_dev::Device::new(path)?;
    let proxy = wlan_dev::connect_wlan_phy(&device)?;
    Ok(NewPhyDevice{ id, proxy, device })
}

fn new_iface(path: &PathBuf) -> Result<NewIfaceDevice, failure::Error> {
    let id = id_from_path(path)?;
    let device = wlan_dev::Device::new(path)?;
    let proxy = fidl_mlme::MlmeProxy::new(wlan_dev::connect_wlan_iface(&device)?);
    Ok(NewIfaceDevice{ id, proxy, device })
}

fn id_from_path(path: &PathBuf) -> Result<u16, failure::Error> {
    let file_name = path.file_name().ok_or_else(
        || format_err!("Invalid device path"))?;
    let file_name_str = file_name.to_str().ok_or_else(
        || format_err!("Filename is not valid UTF-8"))?;
    let id = u16::from_str(&file_name_str).map_err(
        |e| format_err!("Failed to parse device filename as a numeric ID: {}", e))?;
    Ok(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_device::{self as fidl_wlan_dev, SupportedPhy};
    use fidl_fuchsia_wlan_tap as fidl_wlantap;
    use fuchsia_async::{self as fasync, TimeoutExt};
    use fuchsia_zircon::prelude::*;
    use wlantap_client;

    #[test]
    fn watch_phys() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let mut new_phy_stream = watch_phy_devices().expect("watch_phy_devices() failed");
        let wlantap = wlantap_client::Wlantap::open().expect("Failed to connect to wlantapctl");
        let _tap_phy = wlantap.create_phy(create_wlantap_config(*b"wtchph"));
        let new_phy = exec.run_singlethreaded(
            new_phy_stream.next().on_timeout(2.seconds().after_now(),
                || panic!("Didn't get a new phy in time"))
            )
            .expect("new_phy_stream ended without yielding a phy")
            .expect("new_phy_stream returned an error");
        let query_resp = exec.run_singlethreaded(new_phy.proxy.query()).expect("phy query failed");
        assert_eq!(*b"wtchph", query_resp.info.hw_mac_address);
    }

    fn create_wlantap_config(mac_addr: [u8; 6]) -> fidl_wlantap::WlantapPhyConfig {
        fidl_wlantap::WlantapPhyConfig {
            phy_info: fidl_wlan_dev::PhyInfo {
                id: 0,
                dev_path: None,
                hw_mac_address: mac_addr,
                supported_phys: vec![
                    SupportedPhy::Dsss, SupportedPhy::Cck, SupportedPhy::Ofdm, SupportedPhy::Ht
                ],
                driver_features: vec![],
                mac_roles: vec![fidl_wlan_dev::MacRole::Client],
                caps: vec![],
                bands: vec![create_2_4_ghz_band_info()]
            },
            name: String::from("devwatchtap")
        }
    }

    fn create_2_4_ghz_band_info() -> fidl_wlan_dev::BandInfo {
        fidl_wlan_dev::BandInfo{
            description: String::from("2.4 GHz"),
            ht_caps: fidl_wlan_dev::HtCapabilities {
                ht_capability_info: 0x01fe,
                ampdu_params: 0,
                supported_mcs_set: [
                    0xff, 0, 0, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0
                ],
                ht_ext_capabilities: 0,
                tx_beamforming_capabilities: 0,
                asel_capabilities: 0
            },
            vht_caps: None,
            basic_rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
            supported_channels: fidl_wlan_dev::ChannelList {
                base_freq: 2407,
                channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14]
            }
        }
    }
}
