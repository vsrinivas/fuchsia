// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl::endpoints::Proxy,
    fidl_fuchsia_wlan_device as fidl_wlan_dev, fuchsia_async as fasync,
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon::Status as zx_Status,
    futures::prelude::*,
    log::{error, info},
    std::io,
    std::path::{Path, PathBuf},
    std::str::FromStr,
};

pub struct NewPhyDevice {
    pub id: u16,
    pub proxy: fidl_wlan_dev::PhyProxy,
    pub device: wlan_dev::Device,
}

pub fn watch_phy_devices<E: wlan_dev::DeviceEnv>(
) -> io::Result<impl Stream<Item = Result<NewPhyDevice, anyhow::Error>>> {
    Ok(watch_new_devices::<_, E>(E::PHY_PATH)?.try_filter_map(|path| {
        future::ready(Ok(handle_open_error(&path, new_phy::<E>(&path), "phy")))
    }))
}

fn handle_open_error<T>(
    path: &PathBuf,
    r: Result<T, anyhow::Error>,
    context: &'static str,
) -> Option<T> {
    if let Err(ref e) = &r {
        if let Some(&zx_Status::ALREADY_BOUND) = e.downcast_ref::<zx_Status>() {
            info!("{} '{}' already open, deferring", context, path.display())
        } else {
            error!("Error opening {} '{}': {}", context, path.display(), e)
        }
    }
    r.ok()
}

fn watch_new_devices<P: AsRef<Path>, E: wlan_dev::DeviceEnv>(
    path: P,
) -> io::Result<impl Stream<Item = Result<PathBuf, anyhow::Error>>> {
    let dir = E::open_dir(&path)?;
    let channel = fdio::clone_channel(&dir)?;
    let async_channel = fasync::Channel::from_channel(channel)?;
    let directory = fidl_fuchsia_io::DirectoryProxy::from_channel(async_channel);
    Ok(async move {
        let watcher = Watcher::new(directory).await?;
        Ok(watcher
            .try_filter_map(move |msg| {
                future::ready(Ok(match msg.event {
                    WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                        Some(path.as_ref().join(msg.filename))
                    }
                    _ => None,
                }))
            })
            .err_into())
    }
    .try_flatten_stream())
}

fn new_phy<E: wlan_dev::DeviceEnv>(path: &PathBuf) -> Result<NewPhyDevice, anyhow::Error> {
    let id = id_from_path(path)?;
    let device = E::device_from_path(path)?;
    let proxy = wlan_dev::connect_wlan_phy(&device)?;
    Ok(NewPhyDevice { id, proxy, device })
}

fn id_from_path(path: &PathBuf) -> Result<u16, anyhow::Error> {
    let file_name = path.file_name().ok_or_else(|| format_err!("Invalid device path"))?;
    let file_name_str =
        file_name.to_str().ok_or_else(|| format_err!("Filename is not valid UTF-8"))?;
    let id = u16::from_str(&file_name_str)
        .map_err(|e| format_err!("Failed to parse device filename as a numeric ID: {}", e))?;
    Ok(id)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_wlan_common as fidl_common,
        fidl_fuchsia_wlan_device::{self as fidl_wlan_dev, SupportedPhy},
        fidl_fuchsia_wlan_internal as fidl_internal, fidl_fuchsia_wlan_tap as fidl_wlantap,
        fuchsia_zircon::prelude::*,
        futures::{poll, task::Poll},
        isolated_devmgr::IsolatedDeviceEnv,
        pin_utils::pin_mut,
        std::convert::TryInto,
        wlan_common::{ie::*, test_utils::ExpectWithin},
        wlantap_client,
        zerocopy::AsBytes,
    };

    #[test]
    fn watch_phys() {
        let mut exec = fasync::Executor::new().expect("Failed to create an executor");
        let phy_watcher =
            watch_phy_devices::<IsolatedDeviceEnv>().expect("Failed to create phy_watcher");
        pin_mut!(phy_watcher);
        let wlantap = wlantap_client::Wlantap::open_from_isolated_devmgr()
            .expect("Failed to connect to wlantapctl");
        // Create an intentionally unused variable instead of a plain
        // underscore. Otherwise, this end of the channel will be
        // dropped and cause the phy device to begin unbinding.
        let _wlantap_phy = wlantap.create_phy(create_wlantap_config());
        exec.run_singlethreaded(async {
            phy_watcher
                .next()
                .expect_within(5.seconds(), "phy_watcher did not respond")
                .await
                .expect("phy_watcher ended without yielding a phy")
                .expect("phy_watcher returned an error");
            if let Poll::Ready(..) = poll!(phy_watcher.next()) {
                panic!("phy_watcher found more than one phy");
            }
        })
    }

    fn create_wlantap_config() -> fidl_wlantap::WlantapPhyConfig {
        fidl_wlantap::WlantapPhyConfig {
            phy_info: fidl_wlan_dev::PhyInfo {
                // TODO(fxbug.dev/64309): The id and dev_path fields are ignored.
                id: 0,
                dev_path: None,
                hw_mac_address: [0; 6],
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
            ht_caps: Some(Box::new(fidl_internal::HtCapabilities {
                bytes: fake_ht_capabilities().as_bytes().try_into().unwrap(),
            })),
            vht_caps: None,
            rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
            supported_channels: fidl_wlan_dev::ChannelList {
                base_freq: 2407,
                channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
            },
        }
    }
}
