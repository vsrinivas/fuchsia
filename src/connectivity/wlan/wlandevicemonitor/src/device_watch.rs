// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl::endpoints::Proxy,
    fidl_fuchsia_io as fio, fidl_fuchsia_wlan_device as fidl_wlan_dev, fuchsia_async as fasync,
    fuchsia_vfs_watcher::{WatchEvent, Watcher},
    fuchsia_zircon as zx,
    futures::prelude::*,
    log::{error, warn},
    std::{fs::File, str::FromStr},
};

pub struct NewPhyDevice {
    pub id: u16,
    pub proxy: fidl_wlan_dev::PhyProxy,
    pub device_path: String,
}

pub fn watch_phy_devices(
    device_directory: &str,
) -> Result<impl Stream<Item = Result<NewPhyDevice, anyhow::Error>>, anyhow::Error> {
    Ok(watch_new_devices(device_directory)?.try_filter_map(|device_path| {
        future::ready(Ok(handle_open_error(new_phy(device_path.clone()), "phy", &device_path)))
    }))
}

fn handle_open_error<T>(
    r: Result<T, anyhow::Error>,
    device_type: &'static str,
    device_path: &str,
) -> Option<T> {
    if let Err(ref e) = &r {
        if let Some(&zx::Status::ALREADY_BOUND) = e.downcast_ref::<zx::Status>() {
            warn!("Cannot open already-bound device: {} '{}'", device_type, device_path)
        } else {
            error!("Error opening {} '{}': {}", device_type, device_path, e)
        }
    }
    r.ok()
}

/// Watches a specified device directory for new WLAN PHYs.
///
/// When new entries are discovered in the specified directory the paths to the new devices are
/// sent along the stream that is returned by this function.
///
/// Note that a `DeviceEnv` trait is required in order for this function to work.  This enables
/// wlandevicemonitor to function in real and in simulated environments where devices are presented
/// differently.
fn watch_new_devices(
    device_directory: &str,
) -> Result<impl Stream<Item = Result<String, anyhow::Error>>, anyhow::Error> {
    let raw_dir = File::open(&device_directory)?;
    let zircon_channel = fdio::clone_channel(&raw_dir)?;
    let async_channel = fasync::Channel::from_channel(zircon_channel)?;
    let directory = fio::DirectoryProxy::from_channel(async_channel);
    Ok(async move {
        let watcher = Watcher::new(directory).await?;
        Ok(watcher
            .try_filter_map(move |msg| {
                future::ready(Ok(match msg.event {
                    WatchEvent::EXISTING | WatchEvent::ADD_FILE => {
                        Some(String::from(msg.filename.to_string_lossy()))
                    }
                    _ => None,
                }))
            })
            .err_into())
    }
    .try_flatten_stream())
}

fn new_phy(device_filename: String) -> Result<NewPhyDevice, anyhow::Error> {
    let device_path = format!("{}/{}", crate::PHY_PATH, device_filename);
    let device = std::fs::File::open(&device_path)?;

    let (local, remote) = zx::Channel::create()?;
    let connector_channel = fdio::clone_channel(&device)?;
    let connector = fidl_fuchsia_wlan_device::ConnectorProxy::new(fasync::Channel::from_channel(
        connector_channel,
    )?);
    connector.connect(fidl::endpoints::ServerEnd::new(remote))?;
    let proxy = fidl_fuchsia_wlan_device::PhyProxy::new(fasync::Channel::from_channel(local)?);

    Ok(NewPhyDevice {
        id: u16::from_str(&device_filename)
            .map_err(|e| format_err!("Failed to parse device filename as a numeric ID: {}", e))?,
        proxy,
        device_path,
    })
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::PHY_PATH,
        fidl_fuchsia_wlan_common as fidl_wlan_common,
        fidl_fuchsia_wlan_device::{self as fidl_wlan_dev},
        fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_tap as fidl_wlantap,
        fuchsia_zircon::prelude::*,
        futures::{poll, task::Poll},
        log::info,
        pin_utils::pin_mut,
        std::convert::TryInto,
        wlan_common::{ie::*, test_utils::ExpectWithin},
        wlantap_client,
        zerocopy::AsBytes,
    };

    #[test]
    fn watch_phys() {
        let mut exec = fasync::TestExecutor::new().expect("Failed to create an executor");
        let phy_watcher = watch_phy_devices(PHY_PATH).expect("Failed to create phy_watcher");
        pin_mut!(phy_watcher);

        // Wait for the wlantap to appear.
        let raw_dir = File::open("/dev").expect("failed to open /dev");
        let zircon_channel =
            fdio::clone_channel(&raw_dir).expect("failed to clone directory channel");
        let async_channel = fasync::Channel::from_channel(zircon_channel)
            .expect("failed to create async channel from zircon channel");
        let dir = fio::DirectoryProxy::from_channel(async_channel);
        let monitor_fut = device_watcher::recursive_wait_and_open_node(&dir, "sys/test/wlantapctl");
        pin_mut!(monitor_fut);

        info!("Beginning wlantapctl monitor.");
        exec.run_singlethreaded(async {
            monitor_fut.await.expect("error while watching for wlantapctl")
        });
        info!("wlantapctl discovered.");

        // Now that the wlantapctl device is present, connect to it.
        let wlantap = wlantap_client::Wlantap::open().expect("Failed to connect to wlantapctl");

        // Create an intentionally unused variable instead of a plain
        // underscore. Otherwise, this end of the channel will be
        // dropped and cause the phy device to begin unbinding.
        let _wlantap_phy =
            wlantap.create_phy(create_wlantap_config()).expect("failed to create PHY");
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

    #[test]
    fn handle_open_succeeds() {
        assert!(handle_open_error(Ok(()), "phy", "/phy/path").is_some())
    }

    #[test]
    fn handle_open_fails() {
        assert!(
            handle_open_error::<()>(Err(format_err!("test failure")), "phy", "/phy/path").is_none()
        )
    }

    fn create_wlantap_config() -> fidl_wlantap::WlantapPhyConfig {
        fidl_wlantap::WlantapPhyConfig {
            sta_addr: [1; 6],
            supported_phys: vec![
                fidl_wlan_common::WlanPhyType::Dsss,
                fidl_wlan_common::WlanPhyType::Hr,
                fidl_wlan_common::WlanPhyType::Ofdm,
                fidl_wlan_common::WlanPhyType::Erp,
                fidl_wlan_common::WlanPhyType::Ht,
            ],
            mac_role: fidl_wlan_common::WlanMacRole::Client,
            hardware_capability: 0,
            bands: vec![create_2_4_ghz_band_info()],
            name: String::from("devwatchtap"),
            quiet: false,
            discovery_support: fidl_wlan_common::DiscoverySupport {
                scan_offload: fidl_wlan_common::ScanOffloadExtension {
                    supported: false,
                    scan_cancel_supported: false,
                },
                probe_response_offload: fidl_wlan_common::ProbeResponseOffloadExtension {
                    supported: false,
                },
            },
            mac_sublayer_support: fidl_wlan_common::MacSublayerSupport {
                rate_selection_offload: fidl_wlan_common::RateSelectionOffloadExtension {
                    supported: false,
                },
                data_plane: fidl_wlan_common::DataPlaneExtension {
                    data_plane_type: fidl_wlan_common::DataPlaneType::EthernetDevice,
                },
                device: fidl_wlan_common::DeviceExtension {
                    is_synthetic: false,
                    mac_implementation_type: fidl_wlan_common::MacImplementationType::Softmac,
                    tx_status_report_supported: false,
                },
            },
            security_support: fidl_wlan_common::SecuritySupport {
                sae: fidl_wlan_common::SaeFeature {
                    driver_handler_supported: false,
                    sme_handler_supported: false,
                },
                mfp: fidl_wlan_common::MfpFeature { supported: false },
            },
            spectrum_management_support: fidl_wlan_common::SpectrumManagementSupport {
                dfs: fidl_wlan_common::DfsFeature { supported: false },
            },
        }
    }

    fn create_2_4_ghz_band_info() -> fidl_wlan_dev::BandInfo {
        fidl_wlan_dev::BandInfo {
            band: fidl_wlan_common::WlanBand::TwoGhz,
            ht_caps: Some(Box::new(fidl_ieee80211::HtCapabilities {
                bytes: fake_ht_capabilities().as_bytes().try_into().unwrap(),
            })),
            vht_caps: None,
            rates: vec![2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108],
            operating_channels: vec![1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14],
        }
    }
}
