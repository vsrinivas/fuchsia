// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Dummy Driver

use super::*;

use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    DeviceState, EnergyScanParameters, EnergyScanResult, NetworkScanParameters,
    ProvisioningMonitorMarker,
};
use futures::stream::BoxStream;

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

        Ok(vec![])
    }

    async fn get_supported_channels(&self) -> ZxResult<Vec<ChannelInfo>> {
        fx_log_info!("Got get_supported_channels command");

        Ok(vec![])
    }

    async fn form_network(
        &self,
        params: ProvisioningParams,
        _progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        fx_log_info!("Got form command: {:?}", params);
    }

    async fn join_network(
        &self,
        params: ProvisioningParams,
        _progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        fx_log_info!("Got join command: {:?}", params);
    }

    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>> {
        fx_log_info!("Got get credential command");

        Ok(None)
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
        // TODO: Implement dummy energy scanner.
        futures::stream::empty().boxed()
    }

    fn start_network_scan(
        &self,
        _params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>> {
        // TODO: Implement dummy network scanner.
        futures::stream::empty().boxed()
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
        let initial = Ok(DeviceState { connectivity_state: None, role: None });

        ready(initial).into_stream().chain(pending()).boxed()
    }

    fn watch_identity(&self) -> BoxStream<'_, ZxResult<Identity>> {
        use futures::future::ready;
        use futures::stream::pending;
        let initial = Ok(Identity {
            raw_name: None,
            xpanid: None,
            net_type: None,
            channel: None,
            panid: None,
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
}
