// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;

use async_trait::async_trait;
use fasync::Time;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    DeviceState, EnergyScanParameters, EnergyScanResult, NetworkScanParameters,
    ProvisioningMonitorMarker,
};
use futures::future::ready;
use futures::stream::BoxStream;
use lowpan_driver_common::{AsyncConditionWait, Driver as LowpanDriver, FutureExt as _, ZxResult};

/// API-related tasks. Implementation of [`lowpan_driver_common::Driver`].
#[async_trait]
impl<DS: SpinelDeviceClient> LowpanDriver for SpinelDriver<DS> {
    async fn provision_network(&self, _params: ProvisioningParams) -> ZxResult<()> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    async fn leave_network(&self) -> ZxResult<()> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    async fn set_active(&self, _enabled: bool) -> ZxResult<()> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    async fn get_supported_network_types(&self) -> ZxResult<Vec<String>> {
        fx_log_info!("Got get_supported_network_types command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<InterfaceType, _>(Prop::InterfaceType)
            .map_ok(|x| match x {
                InterfaceType::ZigbeeIp => {
                    vec![fidl_fuchsia_lowpan::NET_TYPE_ZIGBEE_IP_1_X.to_string()]
                }
                InterfaceType::Thread => vec![fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()],
                _ => vec![],
            })
            .await
    }

    async fn get_supported_channels(&self) -> ZxResult<Vec<ChannelInfo>> {
        use fidl_fuchsia_lowpan::ChannelInfo;

        fx_log_info!("Got get_supported_channels command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        let results = self.get_property_simple::<Vec<u8>, _>(PropPhy::ChanSupported).await?;

        // TODO: Actually calculate all of the fields for channel info struct

        Ok(results
            .into_iter()
            .map(|x| ChannelInfo {
                id: Some(x.to_string()),
                index: Some(u16::from(x)),
                masked_by_regulatory_domain: Some(false),
                ..ChannelInfo::empty()
            })
            .collect())
    }

    fn watch_device_state(&self) -> BoxStream<'_, ZxResult<DeviceState>> {
        futures::stream::unfold(
            None,
            move |last_state: Option<(DeviceState, AsyncConditionWait<'_>)>| async move {
                let mut snapshot;
                if let Some((last_state, mut condition)) = last_state {
                    // The first item has already been emitted by the stream, so
                    // we need to wait for changes before we emit more.
                    loop {
                        // This loop is where our stream waits for
                        // the next change to the device state.

                        // Wait for the driver state change condition to unblock.
                        condition.await;

                        // Set up the condition for the next iteration.
                        condition = self.driver_state_change.wait();

                        snapshot = self.device_state_snapshot();
                        if snapshot != last_state {
                            break;
                        }
                    }

                    // We start out with our "delta" being a clone of the
                    // current device state. We will then selectively clear
                    // the fields it contains so that only fields that have
                    // changed are represented.
                    let mut delta = snapshot.clone();

                    if last_state.connectivity_state == snapshot.connectivity_state {
                        delta.connectivity_state = None;
                    }

                    if last_state.role == snapshot.role {
                        delta.role = None;
                    }

                    Some((Ok(delta), Some((snapshot, condition))))
                } else {
                    // This is the first item being emitted from the stream,
                    // so we end up emitting the current device state and
                    // setting ourselves up for the next iteration.
                    let condition = self.driver_state_change.wait();
                    snapshot = self.device_state_snapshot();
                    Some((Ok(snapshot.clone()), Some((snapshot, condition))))
                }
            },
        )
        .boxed()
    }

    fn watch_identity(&self) -> BoxStream<'_, ZxResult<Identity>> {
        ready(Err(ZxStatus::NOT_SUPPORTED)).into_stream().boxed()
    }

    async fn form_network(
        &self,
        _params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        // We don't care about errors here because
        // we are simply reporting that this isn't implemented.
        let _ = progress.close_with_epitaph(ZxStatus::NOT_SUPPORTED);
    }

    async fn join_network(
        &self,
        _params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    ) {
        // We don't care about errors here because
        // we are simply reporting that this isn't implemented.
        let _ = progress.close_with_epitaph(ZxStatus::NOT_SUPPORTED);
    }

    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    fn start_energy_scan(
        &self,
        _params: &EnergyScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<EnergyScanResult>>> {
        ready(Err(ZxStatus::NOT_SUPPORTED)).into_stream().boxed()
    }

    fn start_network_scan(
        &self,
        _params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>> {
        ready(Err(ZxStatus::NOT_SUPPORTED)).into_stream().boxed()
    }

    async fn reset(&self) -> ZxResult<()> {
        fx_log_info!("Got reset command");

        // Cancel everyone with an outstanding command.
        self.frame_handler.clear();

        let task = async {
            if self.get_init_state().is_initialized() {
                fx_log_info!("reset: Sending reset command");
                self.frame_handler
                    .send_request(CmdReset)
                    .boxed()
                    .map(|result| match result {
                        Ok(()) => Ok(()),
                        Err(e) if e.downcast_ref::<Canceled>().is_some() => Ok(()),
                        Err(e) => Err(ZxStatus::from(ErrorAdapter(e))),
                    })
                    .cancel_upon(self.ncp_did_reset.wait(), Ok(()))
                    .await?;
                fx_log_info!("reset: Waiting for driver to start initializing");
                self.wait_for_state(DriverState::is_initializing).await;
            }

            fx_log_info!("reset: Waiting for driver to finish initializing");
            self.wait_for_state(DriverState::is_initialized).await;
            Ok(())
        };

        task.on_timeout(Time::after(DEFAULT_TIMEOUT), ncp_cmd_timeout!(self)).await?;

        Ok(())
    }

    async fn get_factory_mac_address(&self) -> ZxResult<Vec<u8>> {
        fx_log_info!("Got get_factory_mac_address command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<Vec<u8>, _>(Prop::HwAddr).await
    }

    async fn get_current_mac_address(&self) -> ZxResult<Vec<u8>> {
        fx_log_info!("Got get_current_mac_address command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<Vec<u8>, _>(PropMac::LongAddr).await
    }

    async fn get_ncp_version(&self) -> ZxResult<String> {
        fx_log_info!("Got get_ncp_version command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<String, _>(Prop::NcpVersion).await
    }

    async fn get_current_channel(&self) -> ZxResult<u16> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<u8, _>(PropPhy::Chan).map_ok(|x| x as u16).await
    }

    // Returns the current RSSI measured by the radio.
    // <fxb/44668>
    async fn get_current_rssi(&self) -> ZxResult<i32> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<i8, _>(PropPhy::Rssi).map_ok(|x| x as i32).await
    }

    async fn get_partition_id(&self) -> ZxResult<u32> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<u32, _>(PropNet::PartitionId).await
    }

    async fn get_thread_rloc16(&self) -> ZxResult<u16> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<u16, _>(PropThread::Rloc16).await
    }

    async fn get_thread_router_id(&self) -> ZxResult<u8> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    async fn send_mfg_command(&self, command: &str) -> ZxResult<String> {
        fx_log_info!("Got send_mfg_command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.apply_standard_combinators(
            self.frame_handler
                .send_request(
                    CmdPropValueSet(PropStream::Mfg.into(), command.to_string())
                        .returning::<String>(),
                )
                .boxed(),
        )
        .await
    }
}

impl<DS: SpinelDeviceClient> SpinelDriver<DS> {
    fn device_state_snapshot(&self) -> DeviceState {
        let driver_state = self.driver_state.lock();
        DeviceState {
            connectivity_state: Some(driver_state.connectivity_state),
            role: Some(driver_state.role),
        }
    }
}
