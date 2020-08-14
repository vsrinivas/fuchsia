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
    async fn provision_network(&self, params: ProvisioningParams) -> ZxResult<()> {
        use std::convert::TryInto;
        fx_log_info!("Got provision command: {:?}", params);

        if params.identity.raw_name.is_none() {
            // We must at least have the network name specified.
            Err(ZxStatus::INVALID_ARGS)?;
        }

        let u8_channel: Option<u8> = if let Some(channel) = params.identity.channel {
            Some(channel.try_into().map_err(|err| {
                fx_log_err!("Error with channel value: {:?}", err);
                ZxStatus::INVALID_ARGS
            })?)
        } else {
            None
        };

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock().await;

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        let task = async {
            // Bring down the mesh networking stack, if it is up.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), false).verify())
                .await?;

            // Bring down the interface, if it is up.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), false).verify())
                .await?;

            // Make sure that any existing network state is cleared out.
            self.frame_handler.send_request(CmdNetClear).await?;

            // From here down we are provisioning the NCP with the new network identity.

            // Set the network name.
            if let Some(network_name) = params.identity.raw_name {
                self.frame_handler
                    .send_request(
                        CmdPropValueSet(PropNet::NetworkName.into(), network_name).verify(),
                    )
                    .await?;
            } else {
                // This code is unreachable because to verified that this
                // field was populated in an earlier check.
                unreachable!("Network name not set");
            }

            // Set the channel.
            if let Some(channel) = u8_channel {
                self.frame_handler
                    .send_request(CmdPropValueSet(PropPhy::Chan.into(), channel).verify())
                    .await?;
            } else {
                // In this case we are using whatever the previous channel was.
            }

            // Set the XPANID, if we have one.
            if let Some(xpanid) = params.identity.xpanid {
                self.frame_handler
                    .send_request(CmdPropValueSet(PropNet::Xpanid.into(), xpanid).verify())
                    .await?;
            }

            // Set the PANID, if we have one.
            if let Some(panid) = params.identity.panid {
                self.frame_handler
                    .send_request(CmdPropValueSet(PropMac::Panid.into(), panid).verify())
                    .await?;
            }

            if self.driver_state.lock().has_cap(Cap::NetSave) {
                // If we have the NetSave capability, go ahead and send the
                // net save command.
                self.frame_handler.send_request(CmdNetSave).await?;
            } else {
                // If we don't have the NetSave capability, we assume that
                // it is saved automatically and tickle PropNet::Saved to
                // make sure the other parts of the driver are aware.
                self.on_prop_value_is(Prop::Net(PropNet::Saved), &[1u8])?;
            }

            Ok(())
        };

        self.apply_standard_combinators(task.boxed()).await
    }

    async fn leave_network(&self) -> ZxResult<()> {
        fx_log_info!("Got leave command");

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock().await;

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        let task = async {
            // Bring down the mesh networking stack, if it is up.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::StackUp.into(), false).verify())
                .await?;

            // Bring down the interface, if it is up.
            self.frame_handler
                .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), false).verify())
                .await?;

            // Make sure that any existing network state is cleared out.
            self.frame_handler.send_request(CmdNetClear).await?;

            Ok(())
        };

        let ret = self.apply_standard_combinators(task.boxed()).await;

        // Finally, issue a software reset command to make sure that
        // we have a clean slate.
        self.frame_handler
            .send_request(CmdReset)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
            .await
            .or(ret)
    }

    async fn set_active(&self, enabled: bool) -> ZxResult<()> {
        fx_log_info!("Got set active command: {:?}", enabled);

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock().await;

        let mut driver_state = self.driver_state.lock();

        let new_state = if enabled {
            driver_state.connectivity_state.activated()
        } else {
            driver_state.connectivity_state.deactivated()
        };

        if new_state != driver_state.connectivity_state {
            let old_state = driver_state.connectivity_state;
            driver_state.connectivity_state = new_state;
            std::mem::drop(driver_state);
            self.on_connectivity_state_change(new_state, old_state);
        }

        Ok(())
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

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock().await;

        traceln!("Got API task lock, waiting until we are ready.");

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
        futures::stream::unfold(
            None,
            move |last_state: Option<(Identity, AsyncConditionWait<'_>)>| async move {
                let mut snapshot;
                if let Some((last_state, mut condition)) = last_state {
                    // The first copy of the identity has already been emitted
                    // by the stream, so we need to wait for changes before we emit more.
                    loop {
                        // This loop is where our stream waits for
                        // the next change to the identity.

                        // Wait for the driver state change condition to unblock.
                        condition.await;

                        // Set up the condition for the next iteration.
                        condition = self.driver_state_change.wait();

                        // Grab our identity snapshot and make sure it is actually different.
                        snapshot = self.identity_snapshot();
                        if snapshot != last_state {
                            break;
                        }
                    }
                    Some((Ok(snapshot.clone()), Some((snapshot, condition))))
                } else {
                    // This is the first item being emitted from the stream,
                    // so we end up emitting the current identity and
                    // setting ourselves up for the next iteration.
                    let condition = self.driver_state_change.wait();
                    snapshot = self.identity_snapshot();
                    Some((Ok(snapshot.clone()), Some((snapshot, condition))))
                }
            },
        )
        .boxed()
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

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock().await;

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

    fn identity_snapshot(&self) -> Identity {
        let driver_state = self.driver_state.lock();
        driver_state.identity.clone()
    }
}
