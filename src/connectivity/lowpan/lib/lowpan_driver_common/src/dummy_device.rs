// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Dummy Driver

use super::*;

use anyhow::Error;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    DeviceState, EnergyScanParameters, EnergyScanResult, NetworkScanParameters,
    ProvisioningMonitorMarker, ProvisioningMonitorRequest, ProvisioningProgress,
};
use fuchsia_zircon_status as zx_status;
use futures::stream::BoxStream;
use futures::FutureExt;
use hex;

/// A dummy LoWPAN Driver implementation, for testing.
#[derive(Debug, Copy, Clone, Default)]
pub struct DummyDevice {}

#[async_trait::async_trait]
impl Driver for DummyDevice {
    async fn provision_network(&self, params: ProvisioningParams) -> ZxResult<()> {
        fx_log_info!("Got provision command: {:?}", params);
        Ok(())
    }

    async fn leave_network(&self) -> ZxResult<()> {
        fx_log_info!("Got leave command");
        Ok(())
    }

    async fn reset(&self) -> ZxResult<()> {
        fx_log_info!("Got reset command");
        Ok(())
    }

    async fn set_active(&self, active: bool) -> ZxResult<()> {
        fx_log_info!("Got set active command: {:?}", active);
        Ok(())
    }

    async fn get_supported_network_types(&self) -> ZxResult<Vec<String>> {
        fx_log_info!("Got get_supported_network_types command");

        Ok(vec!["network_type_0".to_string(), "network_type_1".to_string()])
    }

    async fn get_supported_channels(&self) -> ZxResult<Vec<ChannelInfo>> {
        fx_log_info!("Got get_supported_channels command");
        let channel_info = ChannelInfo {
            id: Some("id".to_string()),
            index: Some(20),
            max_transmit_power: Some(-100),
            spectrum_center_frequency: Some(2450000000),
            spectrum_bandwidth: Some(2000000),
            masked_by_regulatory_domain: Some(false),
        };
        Ok(vec![channel_info])
    }

    async fn form_network(
        &self,
        params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        fx_log_info!("Got form command: {:?}", params);
        let mut request_stream = progress.into_stream().expect("progress into stream");

        let fut = async move {
            match request_stream.try_next().await? {
                Some(ProvisioningMonitorRequest::WatchProgress { responder: r }) => {
                    r.send(&mut Ok(dummy_device::ProvisioningProgress::Progress(0.4)))?;
                }
                None => {
                    return Err(format_err!("invalid request"));
                }
            };

            match request_stream.try_next().await? {
                Some(ProvisioningMonitorRequest::WatchProgress { responder: r }) => {
                    r.send(&mut Ok(dummy_device::ProvisioningProgress::Progress(0.6)))?;
                }
                None => {
                    return Err(format_err!("invalid request"));
                }
            };

            match request_stream.try_next().await? {
                Some(ProvisioningMonitorRequest::WatchProgress { responder: r }) => {
                    r.send(&mut Ok(dummy_device::ProvisioningProgress::Identity(params.identity)))?;
                }
                None => {
                    return Err(format_err!("invalid request"));
                }
            };

            Ok::<(), Error>(())
        };

        match fut.await {
            Ok(()) => {
                fx_log_info!("Replied to ProvisioningProgress requests");
            }
            Err(e) => {
                fx_log_info!("Error replying to ProvisioningProgress requests: {:?}", e);
            }
        }
    }

    async fn join_network(
        &self,
        params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        fx_log_info!("Got join command: {:?}", params);
        let mut request_stream = progress.into_stream().expect("progress into stream");

        let fut = async move {
            match request_stream.try_next().await? {
                Some(ProvisioningMonitorRequest::WatchProgress { responder: r }) => {
                    r.send(&mut Ok(dummy_device::ProvisioningProgress::Progress(0.5)))?;
                }
                None => {
                    return Err(format_err!("invalid request"));
                }
            };

            match request_stream.try_next().await? {
                Some(ProvisioningMonitorRequest::WatchProgress { responder: r }) => {
                    r.send(&mut Ok(dummy_device::ProvisioningProgress::Identity(params.identity)))?;
                }
                None => {
                    return Err(format_err!("invalid request"));
                }
            };

            Ok::<(), Error>(())
        };

        match fut.await {
            Ok(()) => {
                fx_log_info!("Replied to ProvisioningProgress requests");
            }
            Err(e) => {
                fx_log_info!("Error replying to ProvisioningProgress requests: {:?}", e);
            }
        }
    }

    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>> {
        fx_log_info!("Got get credential command");

        let res: Vec<u8> = hex::decode("000102030405060708090a0b0c0d0f".to_string())
            .map_err(|_| zx_status::Status::INTERNAL)?
            .to_vec();

        Ok(Some(fidl_fuchsia_lowpan::Credential::MasterKey(res)))
    }

    async fn get_factory_mac_address(&self) -> ZxResult<Vec<u8>> {
        fx_log_info!("Got get_factory_mac_address command");

        Ok(vec![0, 1, 2, 3, 4, 5, 6, 7])
    }

    async fn get_current_mac_address(&self) -> ZxResult<Vec<u8>> {
        fx_log_info!("Got get_current_mac_address command");

        Ok(vec![0, 1, 2, 3, 4, 5, 6, 7])
    }

    fn start_energy_scan(
        &self,
        _params: &EnergyScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<EnergyScanResult>>> {
        // NOTE: Updates to the returned value may need to be reflected
        //       in `crate::lowpan_device::tests::test_energy_scan`.
        futures::stream::empty()
            .chain(
                ready(vec![EnergyScanResult {
                    channel_index: Some(11),
                    max_rssi: Some(-20),
                    min_rssi: Some(-90),
                }])
                .into_stream(),
            )
            .chain(ready(vec![]).into_stream())
            .chain(
                ready(vec![
                    EnergyScanResult {
                        channel_index: Some(12),
                        max_rssi: Some(-30),
                        min_rssi: Some(-90),
                    },
                    EnergyScanResult {
                        channel_index: Some(13),
                        max_rssi: Some(-25),
                        min_rssi: Some(-90),
                    },
                ])
                .into_stream(),
            )
            .chain(
                ready(vec![
                    EnergyScanResult {
                        channel_index: Some(14),
                        max_rssi: Some(-45),
                        min_rssi: Some(-90),
                    },
                    EnergyScanResult {
                        channel_index: Some(15),
                        max_rssi: Some(-40),
                        min_rssi: Some(-50),
                    },
                ])
                .into_stream(),
            )
            .map(|x| Ok(x))
            .boxed()
    }

    fn start_network_scan(
        &self,
        _params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>> {
        // NOTE: Updates to the returned value may need to be reflected
        //       in `crate::lowpan_device::tests::test_network_scan`.
        futures::stream::empty()
            .chain(
                ready(vec![BeaconInfo {
                    identity: Identity {
                        raw_name: Some("MyNet".as_bytes().to_vec()),
                        xpanid: Some(vec![0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]),
                        net_type: Some(fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()),
                        channel: Some(11),
                        panid: Some(0x1234),
                        ..Identity::empty()
                    },
                    rssi: -40,
                    lqi: 0,
                    address: vec![0x02, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05],
                    flags: vec![],
                }])
                .into_stream(),
            )
            .chain(ready(vec![]).into_stream())
            .chain(
                ready(vec![
                    BeaconInfo {
                        identity: Identity {
                            raw_name: Some("MyNet".as_bytes().to_vec()),
                            xpanid: Some(vec![0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77]),
                            net_type: Some(fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()),
                            channel: Some(11),
                            panid: Some(0x1234),
                            ..Identity::empty()
                        },
                        rssi: -60,
                        lqi: 0,
                        address: vec![0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x13, 0x37],
                        flags: vec![],
                    },
                    BeaconInfo {
                        identity: Identity {
                            raw_name: Some("MyNet2".as_bytes().to_vec()),
                            xpanid: Some(vec![0xFF, 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33, 0xFF]),
                            net_type: Some(fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()),
                            channel: Some(12),
                            panid: Some(0x5678),
                            ..Identity::empty()
                        },
                        rssi: -26,
                        lqi: 0,
                        address: vec![0x02, 0x00, 0x00, 0x00, 0xde, 0xad, 0xbe, 0xef],
                        flags: vec![],
                    },
                ])
                .into_stream(),
            )
            .map(|x| Ok(x))
            .boxed()
    }

    async fn get_ncp_version(&self) -> ZxResult<String> {
        fx_log_info!("Got get_ncp_version command");
        Ok("LowpanDummyDriver/0.0".to_string())
    }

    async fn get_current_channel(&self) -> ZxResult<u16> {
        fx_log_info!("Got get_current_channel command");

        Ok(1)
    }

    async fn get_current_rssi(&self) -> ZxResult<i32> {
        fx_log_info!("Got get_current_rssi command");

        Ok(0)
    }

    fn watch_device_state(&self) -> BoxStream<'_, ZxResult<DeviceState>> {
        use futures::future::ready;
        use futures::stream::pending;
        let initial =
            Ok(DeviceState { connectivity_state: Some(ConnectivityState::Ready), role: None });

        ready(initial).into_stream().chain(pending()).boxed()
    }

    fn watch_identity(&self) -> BoxStream<'_, ZxResult<Identity>> {
        use futures::future::ready;
        use futures::stream::pending;
        let initial = Ok(Identity {
            raw_name: Some(b"ABC1234".to_vec()),
            xpanid: None,
            net_type: None,
            channel: None,
            panid: None,
            ..Identity::empty()
        });

        ready(initial).into_stream().chain(pending()).boxed()
    }

    async fn get_partition_id(&self) -> ZxResult<u32> {
        fx_log_info!("Got get_partition_id command");

        Ok(0)
    }

    async fn get_thread_rloc16(&self) -> ZxResult<u16> {
        fx_log_info!("Got get_thread_rloc16 command");

        Ok(0xffff)
    }

    async fn get_thread_router_id(&self) -> ZxResult<u8> {
        fx_log_info!("Got get_thread_router_id command");

        Ok(0)
    }

    async fn send_mfg_command(&self, command: &str) -> ZxResult<String> {
        fx_log_info!("Got send_mfg_command command: {:?}", command);

        Ok("error: The dummy driver currently has no manufacturing commands.".to_string())
    }
}
