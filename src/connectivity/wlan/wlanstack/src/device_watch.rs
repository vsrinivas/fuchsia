// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::format_err;
use fidl_fuchsia_wlan_device as fidl_wlan_dev;
use fidl_fuchsia_wlan_mlme as fidl_mlme;
use fuchsia_vfs_watcher::{WatchEvent, Watcher};
use fuchsia_wlan_dev as wlan_dev;
use fuchsia_zircon::Status as zx_Status;
use futures::prelude::*;
use log::{error, info};
use std::fs::File;
use std::io;
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

pub fn watch_phy_devices() -> io::Result<impl Stream<Item = io::Result<NewPhyDevice>>> {
    Ok(watch_new_devices(PHY_PATH)?
        .try_filter_map(|path| future::ready(Ok(handle_open_error(&path, new_phy(&path))))))
}

#[deprecated(note = "function is obsolete once WLAN-927 landed")]
pub fn watch_iface_devices() -> io::Result<impl Stream<Item = io::Result<NewIfaceDevice>>> {
    #[allow(deprecated)]
    Ok(watch_new_devices(IFACE_PATH)?
        .try_filter_map(|path| future::ready(Ok(handle_open_error(&path, new_iface(&path))))))
}

fn handle_open_error<T>(path: &PathBuf, r: Result<T, failure::Error>) -> Option<T> {
    if let Err(ref e) = &r {
        if let Some(&zx_Status::ALREADY_BOUND) = e.as_fail().downcast_ref::<zx_Status>() {
            info!("iface {:?} already open, deferring", path.display())
        } else {
            error!("Error opening device '{}': {}", path.display(), e);
        }
    }
    r.ok()
}

fn watch_new_devices<P: AsRef<Path>>(
    path: P,
) -> io::Result<impl Stream<Item = io::Result<PathBuf>>> {
    let dir = File::open(&path)?;
    let watcher = Watcher::new(&dir)?;
    Ok(watcher.try_filter_map(move |msg| {
        future::ready(Ok(match msg.event {
            WatchEvent::EXISTING | WatchEvent::ADD_FILE => Some(path.as_ref().join(msg.filename)),
            _ => None,
        }))
    }))
}

fn new_phy(path: &PathBuf) -> Result<NewPhyDevice, failure::Error> {
    let id = id_from_path(path)?;
    let device = wlan_dev::Device::new(path)?;
    let proxy = wlan_dev::connect_wlan_phy(&device)?;
    Ok(NewPhyDevice { id, proxy, device })
}

#[deprecated(note = "function is obsolete once WLAN-927 landed")]
fn new_iface(path: &PathBuf) -> Result<NewIfaceDevice, failure::Error> {
    let id = id_from_path(path)?;
    let device = wlan_dev::Device::new(path)?;
    let proxy = fidl_mlme::MlmeProxy::new(wlan_dev::connect_wlan_iface(&device)?);
    Ok(NewIfaceDevice { id, proxy, device })
}

fn id_from_path(path: &PathBuf) -> Result<u16, failure::Error> {
    let file_name = path.file_name().ok_or_else(|| format_err!("Invalid device path"))?;
    let file_name_str =
        file_name.to_str().ok_or_else(|| format_err!("Filename is not valid UTF-8"))?;
    let id = u16::from_str(&file_name_str)
        .map_err(|e| format_err!("Failed to parse device filename as a numeric ID: {}", e))?;
    Ok(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_wlan_common as fidl_common;
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
        for _ in 0..10 {
            // 5 is more than enough even for Toulouse but let's be generous
            let new_phy = exec
                .run_singlethreaded(
                    new_phy_stream
                        .next()
                        .on_timeout(2.seconds().after_now(), || panic!("No more phys")),
                )
                .expect("new_phy_stream ended without yielding a phy")
                .expect("new_phy_stream returned an error");
            let query_resp =
                exec.run_singlethreaded(new_phy.proxy.query()).expect("phy query failed");
            if b"wtchph" == &query_resp.info.hw_mac_address {
                return;
            }
        }
        panic!("Did not get the phy we are looking for");
    }

    fn create_wlantap_config(mac_addr: [u8; 6]) -> fidl_wlantap::WlantapPhyConfig {
        fidl_wlantap::WlantapPhyConfig {
            phy_info: fidl_wlan_dev::PhyInfo {
                id: 0,
                dev_path: None,
                hw_mac_address: mac_addr,
                supported_phys: vec![
                    SupportedPhy::Dsss,
                    SupportedPhy::Cck,
                    SupportedPhy::Ofdm,
                    SupportedPhy::Ht,
                ],
                driver_features: vec![],
                mac_roles: vec![fidl_wlan_dev::MacRole::Client],
                caps: vec![],
                bands: vec![create_2_4_ghz_band_info()],
            },
            name: String::from("devwatchtap"),
            quiet: false,
        }
    }

    fn create_2_4_ghz_band_info() -> fidl_wlan_dev::BandInfo {
        fidl_wlan_dev::BandInfo {
            band_id: fidl_common::Band::WlanBand2Ghz,
            ht_caps: Some(Box::new(fidl_mlme::HtCapabilities {
                ht_cap_info: fidl_mlme::HtCapabilityInfo {
                    ldpc_coding_cap: false,
                    chan_width_set: fidl_mlme::ChanWidthSet::TwentyForty as u8,
                    sm_power_save: fidl_mlme::SmPowerSave::Disabled as u8,
                    greenfield: true,
                    short_gi_20: true,
                    short_gi_40: true,
                    tx_stbc: true,
                    rx_stbc: 1,
                    delayed_block_ack: false,
                    max_amsdu_len: fidl_mlme::MaxAmsduLen::Octets3839 as u8,
                    dsss_in_40: false,
                    intolerant_40: false,
                    lsig_txop_protect: false,
                },
                ampdu_params: fidl_mlme::AmpduParams {
                    exponent: 0,
                    min_start_spacing: fidl_mlme::MinMpduStartSpacing::NoRestrict as u8,
                },
                mcs_set: fidl_mlme::SupportedMcsSet {
                    rx_mcs_set: 0x01000000ff,
                    rx_highest_rate: 0,
                    tx_mcs_set_defined: true,
                    tx_rx_diff: false,
                    tx_max_ss: 1,
                    tx_ueqm: false,
                },
                ht_ext_cap: fidl_mlme::HtExtCapabilities {
                    pco: false,
                    pco_transition: fidl_mlme::PcoTransitionTime::PcoReserved as u8,
                    mcs_feedback: fidl_mlme::McsFeedback::McsNofeedback as u8,
                    htc_ht_support: false,
                    rd_responder: false,
                },
                txbf_cap: fidl_mlme::TxBfCapability {
                    implicit_rx: false,
                    rx_stag_sounding: false,
                    tx_stag_sounding: false,
                    rx_ndp: false,
                    tx_ndp: false,
                    implicit: false,
                    calibration: fidl_mlme::Calibration::CalibrationNone as u8,
                    csi: false,
                    noncomp_steering: false,
                    comp_steering: false,
                    csi_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
                    noncomp_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
                    comp_feedback: fidl_mlme::Feedback::FeedbackNone as u8,
                    min_grouping: fidl_mlme::MinGroup::MinGroupOne as u8,
                    csi_antennas: 1,
                    noncomp_steering_ants: 1,
                    comp_steering_ants: 1,
                    csi_rows: 1,
                    chan_estimation: 1,
                },
                asel_cap: fidl_mlme::AselCapability {
                    asel: false,
                    csi_feedback_tx_asel: false,
                    ant_idx_feedback_tx_asel: false,
                    explicit_csi_feedback: false,
                    antenna_idx_feedback: false,
                    rx_asel: false,
                    tx_sounding_ppdu: false,
                },
            })),
            vht_caps: None,
            basic_rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
            supported_channels: fidl_wlan_dev::ChannelList {
                base_freq: 2407,
                channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
            },
        }
    }
}
