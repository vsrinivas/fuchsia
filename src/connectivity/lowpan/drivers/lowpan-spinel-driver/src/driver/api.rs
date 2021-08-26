// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;

use anyhow::Error;
use async_trait::async_trait;
use core::num::NonZeroU16;
use fasync::Time;
use fidl_fuchsia_lowpan::BeaconInfo;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    AllCounters, DeviceState, EnergyScanParameters, ExternalRoute, NetworkScanParameters,
    OnMeshPrefix, ProvisionError, ProvisioningProgress,
};
use fidl_fuchsia_lowpan_test::*;
use fuchsia_async::TimeoutExt;
use fuchsia_zircon::Duration;
use futures::stream::BoxStream;
use futures::{StreamExt, TryFutureExt};
use lowpan_driver_common::{AsyncConditionWait, Driver as LowpanDriver, FutureExt, ZxResult};
use spinel_pack::{TryUnpack, EUI64};
use std::convert::TryInto;

const JOIN_TIMEOUT: Duration = Duration::from_seconds(120);

/// Helpers for API-related tasks.
impl<DS: SpinelDeviceClient, NI: NetworkInterface> SpinelDriver<DS, NI> {
    async fn set_scan_mask(&self, scan_mask: Option<&Vec<u16>>) -> Result<(), Error> {
        if let Some(mask) = scan_mask {
            let u8_mask = mask.iter().try_fold(
                Vec::<u8>::new(),
                |mut acc, &x| -> Result<Vec<u8>, Error> {
                    acc.push(TryInto::<u8>::try_into(x)?);
                    Ok(acc)
                },
            )?;

            self.frame_handler
                .send_request(CmdPropValueSet(PropMac::ScanMask.into(), u8_mask))
                .await?
        } else {
            self.frame_handler.send_request(CmdPropValueSet(PropMac::ScanMask.into(), ())).await?
        }
        Ok(())
    }

    /// Helper function for methods that return streams. Allows you
    /// to have an initialization method that returns a lock which can be
    /// held while another stream is running (presumably from `inspect_as_stream`)
    fn start_ongoing_stream_process<'a, R, FInit, SStream, L>(
        &'a self,
        init_task: FInit,
        stream: SStream,
        timeout: Time,
    ) -> BoxStream<'a, ZxResult<R>>
    where
        R: Send + 'a,
        FInit: Send + Future<Output = Result<L, Error>> + 'a,
        SStream: Send + Stream<Item = Result<R, Error>> + 'a,
        L: Send + 'a,
    {
        enum InternalState<'a, R, L> {
            Init(crate::future::BoxFuture<'a, ZxResult<L>>, BoxStream<'a, ZxResult<R>>),
            Running(L, BoxStream<'a, ZxResult<R>>),
            Done,
        }

        let init_task = init_task
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
            .on_timeout(Time::after(DEFAULT_TIMEOUT), ncp_cmd_timeout!(self));

        let stream = stream.map_err(|e| ZxStatus::from(ErrorAdapter(e)));

        futures::stream::unfold(
            InternalState::Init(init_task.boxed(), stream.boxed()),
            move |mut last_state: InternalState<'_, R, L>| async move {
                last_state = match last_state {
                    InternalState::Init(init_task, stream) => {
                        traceln!("ongoing_stream_process: initializing");
                        match init_task.await {
                            Ok(lock) => InternalState::Running(lock, stream),
                            Err(err) => return Some((Err(err), InternalState::Done)),
                        }
                    }
                    last_state => last_state,
                };

                if let InternalState::Running(lock, mut stream) = last_state {
                    traceln!("ongoing_stream_process: getting next");
                    if let Some(next) = stream
                        .next()
                        .cancel_upon(self.ncp_did_reset.wait(), Some(Err(ZxStatus::CANCELED)))
                        .on_timeout(timeout, move || {
                            fx_log_err!("ongoing_stream_process: Timeout");
                            self.ncp_is_misbehaving();
                            Some(Err(ZxStatus::TIMED_OUT))
                        })
                        .await
                    {
                        return Some((next, InternalState::Running(lock, stream)));
                    }
                }

                traceln!("ongoing_stream_process: Done");

                None
            },
        )
        .boxed()
    }
}

/// API-related tasks. Implementation of [`lowpan_driver_common::Driver`].
#[async_trait]
impl<DS: SpinelDeviceClient, NI: NetworkInterface> LowpanDriver for SpinelDriver<DS, NI> {
    async fn provision_network(&self, params: ProvisioningParams) -> ZxResult<()> {
        use std::convert::TryInto;
        fx_log_debug!("Got provision command: {:?}", params);

        if params.identity.raw_name.is_none() {
            // We must at least have the network name specified.
            return Err(ZxStatus::INVALID_ARGS);
        }

        let net_type = if let Some(ref net_type) = params.identity.net_type {
            if self.is_net_type_supported(net_type.as_str()) {
                Some(net_type.clone())
            } else {
                fx_log_err!("Network type {:?} is not supported by this interface.", net_type);
                return Err(ZxStatus::NOT_SUPPORTED);
            }
        } else {
            let net_type = self.driver_state.lock().preferred_net_type.clone();
            if net_type.is_empty() {
                None
            } else {
                Some(net_type)
            }
        };

        let u8_channel: Option<u8> = if let Some(channel) = params.identity.channel {
            Some(channel.try_into().map_err(|err| {
                fx_log_err!("Error with channel value: {:?}", err);
                ZxStatus::INVALID_ARGS
            })?)
        } else {
            None
        };

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("provision_network").await?;

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

            // Update the network type field of the driver state identity.
            {
                let mut driver_state = self.driver_state.lock();
                driver_state.identity.net_type = net_type;
            }

            // Set the network name.
            if let Some(network_name) = params.identity.raw_name {
                let network_name = std::str::from_utf8(&network_name)?.to_string();
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

            // Set the credential, if we have one.
            if let Some(fidl_fuchsia_lowpan::Credential::MasterKey(key)) =
                params.credential.map(|x| *x)
            {
                self.frame_handler
                    .send_request(CmdPropValueSet(PropNet::MasterKey.into(), key).verify())
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
        let _lock = self.wait_for_api_task_lock("leave_network").await?;

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

        // Clear the frame handler prepare for (re)initialization.
        self.frame_handler.clear();
        self.driver_state.lock().prepare_for_init();
        self.driver_state_change.trigger();

        ret
    }

    async fn set_active(&self, enabled: bool) -> ZxResult<()> {
        fx_log_info!("Got set active command: {:?}", enabled);

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("set_active").await?;

        // Wait until we are initialized, if we aren't already.
        self.wait_for_state(DriverState::is_initialized).await;

        self.apply_standard_combinators(self.net_if.set_enabled(enabled).boxed()).await?;

        Ok(())
    }

    async fn get_supported_network_types(&self) -> ZxResult<Vec<String>> {
        fx_log_info!("Got get_supported_network_types command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        let mut ret = vec![];

        let driver_state = self.driver_state.lock();

        if !driver_state.preferred_net_type.is_empty() {
            ret.push(driver_state.preferred_net_type.clone());
        }

        Ok(ret)
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
                ..ChannelInfo::EMPTY
            })
            .collect())
    }

    fn watch_device_state(&self) -> BoxStream<'_, ZxResult<DeviceState>> {
        futures::stream::unfold(
            None,
            move |last_state: Option<(DeviceState, AsyncConditionWait<'_>)>| {
                async move {
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

                            // Wait until we are ready.
                            self.wait_for_state(DriverState::is_initialized).await;

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
                }
            },
        )
        .boxed()
    }

    fn watch_identity(&self) -> BoxStream<'_, ZxResult<Identity>> {
        futures::stream::unfold(
            None,
            move |last_state: Option<(Identity, AsyncConditionWait<'_>)>| {
                async move {
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

                            // Wait until we are ready.
                            self.wait_for_state(DriverState::is_initialized).await;

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
                }
            },
        )
        .boxed()
    }

    fn form_network(
        &self,
        params: ProvisioningParams,
    ) -> BoxStream<'_, ZxResult<Result<ProvisioningProgress, ProvisionError>>> {
        fx_log_debug!("Got form command: {:?}", params);

        let init_task = async move {
            // Wait for our turn.
            let _lock = self.wait_for_api_task_lock("form_network").await?;

            // Wait until we are ready.
            self.wait_for_state(DriverState::is_initialized).await;

            // TODO: Uncomment this line once implemented and remove the error line below
            // Ok(_lock)
            Result::<(), _>::Err(ZxStatus::NOT_SUPPORTED.into())
        };

        let stream = self.frame_handler.inspect_as_stream(|frame| {
            fx_log_debug!("form_network: Inspecting {:?}", frame);

            // This method may return the following values:
            //
            // * Normal Conditions:
            //    * None: Keep processing and emit nothing from the stream.
            //    * Some(Ok(Some(Ok(progress)))): Emit the given progress value from the stream.
            //    * Some(Ok(None)): Close the stream with no error.
            // * Error Conditions:
            //    * Some(Err(zx_error)): Close the stream with a `ZxStatus`.
            //    * Some(Ok(Some(Err(provision_err)))): Close the stream with a `ProvisionError`

            // TODO: Add code to monitor and emit results here.
            None
        });

        self.start_ongoing_stream_process(init_task, stream, Time::after(DEFAULT_TIMEOUT))
    }

    fn join_network(
        &self,
        params: JoinParams,
    ) -> BoxStream<'_, ZxResult<Result<ProvisioningProgress, ProvisionError>>> {
        fx_log_debug!("Got join command: {:?}", params);

        let init_task = async move {
            // Wait for our turn.
            let lock = self.wait_for_api_task_lock("join_network").await?;

            // Wait until we are ready.
            self.wait_for_state(DriverState::is_initialized).await;

            match params {
                JoinParams::JoinerParameter(joiner_params) => {
                    // For in-band joiner commissioning, pskd is required.
                    if joiner_params.pskd.as_ref().map(|x| x.is_empty()).unwrap_or(true) {
                        fx_log_err!("join network: pskd is empty");
                        return Err(Error::from(ZxStatus::INVALID_ARGS));
                    }

                    let joiner_commissioning_param: JoinerCommissioning =
                        joiner_params.try_into()?;

                    fx_log_info!("join network: init-task: start to set interface up prop");

                    // Bring up the network interface.
                    self.frame_handler
                        .send_request(CmdPropValueSet(PropNet::InterfaceUp.into(), true).verify())
                        .await
                        .context("Setting PropNet::InterfaceUp")?;

                    fx_log_info!("join network: init-task: start to set joiner commissioning prop");

                    // starting joiner commissioning
                    self.frame_handler
                        .send_request(CmdPropValueSet(
                            PropMeshcop::JoinerCommissioning.into(),
                            joiner_commissioning_param,
                        ))
                        .await
                        .context("error setting joiner commissioning property")?;

                    self.on_provisioned(false);

                    self.on_start_of_commissioning()?;
                }
                _ => {
                    fx_log_err!("join network: provision params not supported");
                    return Err(Error::from(ZxStatus::INVALID_ARGS));
                }
            }

            fx_log_info!("joiner commissioning: init-task: returning api lock");

            Ok(lock)
        };

        let stream = self.frame_handler.inspect_as_stream(|frame| {
            traceln!("join network stream: Inspecting {:?}", frame);

            // This method may return the following values:
            //
            // * Normal Conditions:
            //    * None: Keep processing and emit nothing from the stream.
            //    * Some(Ok(Some(Ok(progress)))): Emit the given progress value from the stream.
            //    * Some(Ok(None)): Close the stream with no error.
            // * Error Conditions:
            //    * Some(Err(zx_error)): Close the stream with a `ZxStatus`.
            //    * Some(Ok(Some(Err(provision_err)))): Close the stream with a `ProvisionError`

            match SpinelPropValueRef::try_unpack_from_slice(frame.payload) {
                Ok(prop_value) if prop_value.prop == Prop::Meshcop(PropMeshcop::JoinerState) => {
                    fx_log_info!(
                        "join network stream: found joiner state change: {:?}",
                        MeshcopJoinerState::try_unpack_from_slice(prop_value.value)
                    );
                    None
                }

                Ok(prop_value) if prop_value.prop == Prop::LastStatus => {
                    match Status::try_unpack_from_slice(prop_value.value) {
                        Ok(Status::Join(join_status)) => {
                            fx_log_info!("join network stream: inspect error {:?}", join_status);
                            match join_status {
                                StatusJoin::Failure => {
                                    Some(Ok(Some(Err(ProvisionError::Canceled))))
                                }
                                StatusJoin::Security => {
                                    Some(Ok(Some(Err(ProvisionError::CredentialRejected))))
                                }
                                StatusJoin::NoPeers => {
                                    Some(Ok(Some(Err(ProvisionError::NetworkNotFound))))
                                }
                                StatusJoin::Incompatible => {
                                    Some(Ok(Some(Err(ProvisionError::Canceled))))
                                }
                                StatusJoin::RspTimeout => {
                                    Some(Ok(Some(Err(ProvisionError::NetworkNotFound))))
                                }
                                StatusJoin::Success => Some(Ok(None)),
                            }
                        }
                        Err(err) => Some(
                            Err(err).context("join network stream: inspecting last_status frame"),
                        ),
                        _ => None,
                    }
                }
                Err(err) => Some(Err(err).context("join network stream: inspecting inbound frame")),
                _ => None,
            }
        });

        self.start_ongoing_stream_process(init_task, stream, Time::after(JOIN_TIMEOUT))
            .chain(
                async move { Ok(Ok(ProvisioningProgress::Identity(self.identity_snapshot()))) }
                    .into_stream()
                    .boxed(),
            )
            .boxed()
    }

    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>> {
        fx_log_info!("Got get credential command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        if self.driver_state.lock().is_ready() {
            self.get_property_simple::<Vec<u8>, _>(PropNet::MasterKey)
                .and_then(|key| {
                    futures::future::ready(if key.is_empty() {
                        Ok(None)
                    } else {
                        Ok(Some(fidl_fuchsia_lowpan::Credential::MasterKey(key)))
                    })
                })
                .await
        } else {
            Ok(None)
        }
    }

    fn start_energy_scan(
        &self,
        params: &EnergyScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<fidl_fuchsia_lowpan_device::EnergyScanResult>>> {
        fx_log_info!("Got energy scan command: {:?}", params);

        let channels = params.channels.clone();
        let dwell_time = params.dwell_time_ms;

        let init_task = async move {
            // Wait for our turn.
            let lock = self.wait_for_api_task_lock("start_energy_scan").await?;

            // Wait until we are ready.
            self.wait_for_state(DriverState::is_initialized).await;

            // Set the channel mask.
            self.set_scan_mask(channels.as_ref()).await?;

            // Set dwell time.
            if let Some(dwell_time) = dwell_time {
                self.frame_handler
                    .send_request(CmdPropValueSet(
                        PropMac::ScanPeriod.into(),
                        TryInto::<u16>::try_into(dwell_time)?,
                    ))
                    .await?
            } else {
                self.frame_handler
                    .send_request(CmdPropValueSet(
                        PropMac::ScanPeriod.into(),
                        DEFAULT_SCAN_DWELL_TIME_MS,
                    ))
                    .await?
            }

            // Start the scan.
            self.frame_handler
                .send_request(
                    CmdPropValueSet(PropMac::ScanState.into(), ScanState::Energy).verify(),
                )
                .await?;

            traceln!("energy_scan: Scan started!");

            Ok(lock
                .with_cleanup_request(CmdPropValueSet(PropMac::ScanState.into(), ScanState::Idle)))
        };

        let stream = self.frame_handler.inspect_as_stream(|frame| {
            traceln!("energy_scan: Inspecting {:?}", frame);
            if frame.cmd == Cmd::PropValueInserted {
                match SpinelPropValueRef::try_unpack_from_slice(frame.payload)
                    .context("energy_scan")
                {
                    Ok(prop_value) if prop_value.prop == Prop::Mac(PropMac::EnergyScanResult) => {
                        let mut iter = prop_value.value.iter();
                        let result = match EnergyScanResult::try_unpack(&mut iter) {
                            Ok(val) => val,
                            Err(err) => return Some(Err(err)),
                        };
                        fx_log_info!("energy_scan: got result: {:?}", result);

                        Some(Ok(Some(vec![fidl_fuchsia_lowpan_device::EnergyScanResult {
                            channel_index: Some(result.channel as u16),
                            max_rssi: Some(result.rssi as i32),
                            ..fidl_fuchsia_lowpan_device::EnergyScanResult::EMPTY
                        }])))
                    }
                    Err(err) => Some(Err(err)),
                    _ => None,
                }
            } else if frame.cmd == Cmd::PropValueIs {
                match SpinelPropValueRef::try_unpack_from_slice(frame.payload)
                    .context("energy_scan")
                {
                    Ok(prop_value) if prop_value.prop == Prop::Mac(PropMac::ScanState) => {
                        let mut iter = prop_value.value.iter();
                        if Some(ScanState::Energy) != ScanState::try_unpack(&mut iter).ok() {
                            fx_log_info!("energy_scan: scan ended");
                            Some(Ok(None))
                        } else {
                            None
                        }
                    }
                    Err(err) => Some(Err(err)),
                    _ => None,
                }
            } else {
                None
            }
        });

        self.start_ongoing_stream_process(init_task, stream, Time::after(DEFAULT_TIMEOUT))
    }

    fn start_network_scan(
        &self,
        params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>> {
        fx_log_debug!("Got network scan command: {:?}", params);

        let channels = params.channels.clone();
        let tx_power = params.tx_power_dbm;

        let init_task = async move {
            // Wait for our turn.
            let lock = self.wait_for_api_task_lock("start_network_scan").await?;

            // Wait until we are ready.
            self.wait_for_state(DriverState::is_initialized).await;

            // Set the channel mask.
            self.set_scan_mask(channels.as_ref()).await?;

            // Set beacon request transmit power
            if let Some(tx_power) = tx_power {
                // Saturate to signed 8-bit integer
                let tx_power = if tx_power > i8::MAX as i32 {
                    i8::MAX as i32
                } else if tx_power < i8::MIN as i32 {
                    i8::MIN as i32
                } else {
                    tx_power
                };
                self.frame_handler
                    .send_request(CmdPropValueSet(PropPhy::TxPower.into(), tx_power))
                    .await?
            }

            // Start the scan.
            self.frame_handler
                .send_request(
                    CmdPropValueSet(PropMac::ScanState.into(), ScanState::Beacon).verify(),
                )
                .await?;

            Ok(lock
                .with_cleanup_request(CmdPropValueSet(PropMac::ScanState.into(), ScanState::Idle)))
        };

        let stream = self.frame_handler.inspect_as_stream(|frame| {
            if frame.cmd == Cmd::PropValueInserted {
                match SpinelPropValueRef::try_unpack_from_slice(frame.payload)
                    .context("network_scan")
                {
                    Ok(prop_value) if prop_value.prop == Prop::Mac(PropMac::ScanBeacon) => {
                        let mut iter = prop_value.value.iter();
                        let result = match NetScanResult::try_unpack(&mut iter) {
                            Ok(val) => val,
                            Err(err) => {
                                // There was an error parsing the scan result.
                                // We don't treat this as fatal, we just skip this entry.
                                // We do print out the error, though.
                                fx_log_warn!(
                                    "Unable to parse network scan result: {:?} ({:x?})",
                                    err,
                                    prop_value.value
                                );
                                return None;
                            }
                        };

                        fx_log_debug!("network_scan: got result: {:?}", result);

                        Some(Ok(Some(vec![BeaconInfo {
                            identity: Identity {
                                raw_name: Some(result.net.network_name),
                                channel: Some(result.channel as u16),
                                panid: Some(result.mac.panid),
                                xpanid: Some(result.net.xpanid),
                                net_type: InterfaceType::from(result.net.net_type).to_net_type(),
                                ..Identity::EMPTY
                            },
                            rssi: result.rssi as i32,
                            lqi: result.mac.lqi,
                            address: result.mac.long_addr.0.to_vec(),
                            flags: vec![],
                        }])))
                    }
                    Err(err) => Some(Err(err)),
                    _ => None,
                }
            } else if frame.cmd == Cmd::PropValueIs {
                match SpinelPropValueRef::try_unpack_from_slice(frame.payload)
                    .context("network_scan")
                {
                    Ok(prop_value) if prop_value.prop == Prop::Mac(PropMac::ScanState) => {
                        let mut iter = prop_value.value.iter();
                        if Some(ScanState::Beacon) != ScanState::try_unpack(&mut iter).ok() {
                            fx_log_info!("network_scan: scan ended");
                            Some(Ok(None))
                        } else {
                            None
                        }
                    }
                    Err(err) => Some(Err(err)),
                    _ => None,
                }
            } else {
                None
            }
        });

        self.start_ongoing_stream_process(init_task, stream, Time::after(DEFAULT_TIMEOUT))
    }

    async fn reset(&self) -> ZxResult<()> {
        fx_log_info!("Got API request to reset");

        // Clear the frame handler one more time and prepare for (re)initialization.
        self.driver_state.lock().prepare_for_init();
        self.driver_state_change.trigger();
        self.frame_handler.clear();

        // Wait for initialization to complete.
        // The reset will happen during initialization.
        fx_log_info!("reset: Waiting for driver to finish initializing");
        self.wait_for_state(DriverState::is_initialized)
            .boxed()
            .map(|_| Ok(()))
            .on_timeout(Time::after(DEFAULT_TIMEOUT), ncp_cmd_timeout!(self))
            .await
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
    // <fxbug.dev/44668>
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

    async fn register_on_mesh_prefix(&self, net: OnMeshPrefix) -> ZxResult<()> {
        fx_log_info!("Got register_on_mesh_prefix command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        if net.subnet.is_none() {
            return Err(ZxStatus::INVALID_ARGS);
        }

        let on_mesh_net = OnMeshNet::from(net);

        let lock_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), true))
            // It is acceptable to get the error `Already` here.
            .map(|r| match r {
                Err(e) if e.downcast_ref::<Status>() == Some(&Status::Already) => Ok(()),
                other => other,
            })
            .inspect_err(|e| fx_log_err!("Unable to lock AllowLocalNetDataChange: {:?}", e));

        let cleanup_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), false))
            .inspect_err(|e| fx_log_err!("Unable to unlock AllowLocalNetDataChange: {:?}", e));

        let future = self
            .frame_handler
            .send_request(CmdPropValueInsert(PropThread::OnMeshNets.into(), on_mesh_net.clone()))
            .inspect_err(|e| fx_log_err!("register_on_mesh_prefix: Failed insert: {:?}", e));

        // Wait for our turn.
        let _lock = match self.wait_for_api_task_lock("register_on_mesh_prefix").await {
            Ok(x) => x,
            Err(x) => {
                fx_log_warn!("Failed waiting for API task lock: {:?}", x);
                return Err(ZxStatus::INTERNAL);
            }
        };

        self.apply_standard_combinators(
            async move {
                lock_future.await?;
                let ret = future.await;
                // This next line makes sure that we always run the cleanup
                // future, even if our primary future fails.
                ret.and(cleanup_future.await).and_then(move |ret| {
                    let mut driver_state = self.driver_state.lock();
                    driver_state.local_on_mesh_nets.insert(CorrelatedBox(on_mesh_net));
                    Ok(ret)
                })
            }
            .boxed(),
        )
        .await
    }

    async fn unregister_on_mesh_prefix(
        &self,
        subnet: fidl_fuchsia_lowpan::Ipv6Subnet,
    ) -> ZxResult<()> {
        fx_log_info!("Got unregister_on_mesh_prefix command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        {
            let mut driver_state = self.driver_state.lock();
            driver_state.local_on_mesh_nets.remove(&CorrelatedBox(OnMeshNet::from(subnet.clone())));
        }

        let lock_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), true))
            // It is acceptable to get the error `Already` here.
            .map(|r| match r {
                Err(e) if e.downcast_ref::<Status>() == Some(&Status::Already) => Ok(()),
                other => other,
            })
            .inspect_err(|e| fx_log_err!("Unable to lock AllowLocalNetDataChange: {:?}", e));

        let cleanup_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), false))
            .inspect_err(|e| fx_log_err!("Unable to unlock AllowLocalNetDataChange: {:?}", e));

        let future = self
            .frame_handler
            .send_request(CmdPropValueRemove(
                PropThread::OnMeshNets.into(),
                crate::spinel::Subnet::from(subnet),
            ))
            .inspect_err(|e| fx_log_err!("unregister_on_mesh_prefix: Failed remove: {:?}", e));

        // Wait for our turn.
        let _lock = match self.wait_for_api_task_lock("unregister_on_mesh_prefix").await {
            Ok(x) => x,
            Err(x) => {
                fx_log_warn!("Failed waiting for API task lock: {:?}", x);
                return Err(ZxStatus::INTERNAL);
            }
        };

        self.apply_standard_combinators(
            async move {
                lock_future.await?;
                let ret = future.await;
                // This next line makes sure that we always run the cleanup
                // future, even if our primary future fails.
                ret.and(cleanup_future.await)
            }
            .boxed(),
        )
        .await
    }

    async fn register_external_route(&self, net: ExternalRoute) -> ZxResult<()> {
        fx_log_info!("Got register_external_route command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        if net.subnet.is_none() {
            return Err(ZxStatus::INVALID_ARGS);
        }

        let external_route = crate::spinel::ExternalRoute::from(net);

        let lock_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), true))
            // It is acceptable to get the error `Already` here.
            .map(|r| match r {
                Err(e) if e.downcast_ref::<Status>() == Some(&Status::Already) => Ok(()),
                other => other,
            })
            .inspect_err(|e| fx_log_err!("Unable to lock AllowLocalNetDataChange: {:?}", e));

        let cleanup_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), false))
            .inspect_err(|e| fx_log_err!("Unable to unlock AllowLocalNetDataChange: {:?}", e));

        let future = self
            .frame_handler
            .send_request(CmdPropValueInsert(
                PropThread::OffMeshRoutes.into(),
                external_route.clone(),
            ))
            .inspect_err(|e| fx_log_err!("register_external_route: Failed insert: {:?}", e));

        // Wait for our turn.
        let _lock = match self.wait_for_api_task_lock("register_external_route").await {
            Ok(x) => x,
            Err(x) => {
                fx_log_warn!("Failed waiting for API task lock: {:?}", x);
                return Err(ZxStatus::INTERNAL);
            }
        };

        self.apply_standard_combinators(
            async move {
                lock_future.await?;
                let ret = future.await;
                // This next line makes sure that we always run the cleanup
                // future, even if our primary future fails.
                ret.and(cleanup_future.await).and_then(move |ret| {
                    let mut driver_state = self.driver_state.lock();
                    driver_state.local_external_routes.insert(CorrelatedBox(external_route));
                    Ok(ret)
                })
            }
            .boxed(),
        )
        .await
    }

    async fn unregister_external_route(
        &self,
        subnet: fidl_fuchsia_lowpan::Ipv6Subnet,
    ) -> ZxResult<()> {
        fx_log_info!("Got unregister_external_route command");

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        {
            let mut driver_state = self.driver_state.lock();
            driver_state
                .local_external_routes
                .remove(&CorrelatedBox(crate::spinel::ExternalRoute::from(subnet.clone())));
        }

        let lock_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), true))
            // It is acceptable to get the error `Already` here.
            .map(|r| match r {
                Err(e) if e.downcast_ref::<Status>() == Some(&Status::Already) => Ok(()),
                other => other,
            })
            .inspect_err(|e| fx_log_err!("Unable to lock AllowLocalNetDataChange: {:?}", e));

        let cleanup_future = self
            .frame_handler
            .send_request(CmdPropValueSet(PropThread::AllowLocalNetDataChange.into(), false))
            .inspect_err(|e| fx_log_err!("Unable to unlock AllowLocalNetDataChange: {:?}", e));

        let future = self
            .frame_handler
            .send_request(CmdPropValueRemove(
                PropThread::OffMeshRoutes.into(),
                crate::spinel::Subnet::from(subnet),
            ))
            .inspect_err(|e| fx_log_err!("unregister_external_route: Failed remove: {:?}", e));

        // Wait for our turn.
        let _lock = match self.wait_for_api_task_lock("unregister_external_router").await {
            Ok(x) => x,
            Err(x) => {
                fx_log_warn!("Failed waiting for API task lock: {:?}", x);
                return Err(ZxStatus::INTERNAL);
            }
        };

        self.apply_standard_combinators(
            async move {
                lock_future.await?;
                let ret = future.await;
                // This next line makes sure that we always run the cleanup
                // future, even if our primary future fails.
                ret.and(cleanup_future.await)
            }
            .boxed(),
        )
        .await
    }

    async fn get_local_on_mesh_prefixes(
        &self,
    ) -> ZxResult<Vec<fidl_fuchsia_lowpan_device::OnMeshPrefix>> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<Vec<OnMeshNet>, _>(PropThread::OnMeshNets)
            .map_ok(|x| {
                x.into_iter()
                    .filter(|x| x.local)
                    .map(std::convert::Into::<fidl_fuchsia_lowpan_device::OnMeshPrefix>::into)
                    .collect::<Vec<_>>()
            })
            .await
    }

    async fn get_local_external_routes(
        &self,
    ) -> ZxResult<Vec<fidl_fuchsia_lowpan_device::ExternalRoute>> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        self.get_property_simple::<Vec<crate::spinel::ExternalRoute>, _>(PropThread::OffMeshRoutes)
            .map_ok(|x| {
                x.into_iter()
                    .filter(|x| x.local)
                    .map(std::convert::Into::<fidl_fuchsia_lowpan_device::ExternalRoute>::into)
                    .collect::<Vec<_>>()
            })
            .await
    }

    async fn replace_mac_address_filter_settings(
        &self,
        settings: MacAddressFilterSettings,
    ) -> ZxResult<()> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("get_mac_address_filter_settings").await?;

        // Disbale allowlist/denylist
        self.frame_handler
            .send_request(CmdPropValueSet(PropMac::AllowListEnabled.into(), false).verify())
            .await
            .map_err(|err| {
                fx_log_err!("Error disable allowlist: {}", err);
                ZxStatus::INTERNAL
            })?;
        self.frame_handler
            .send_request(CmdPropValueSet(PropMac::DenyListEnabled.into(), false).verify())
            .await
            .map_err(|err| {
                fx_log_err!("Error disable denylist: {}", err);
                ZxStatus::INTERNAL
            })?;

        match settings.mode {
            Some(MacAddressFilterMode::Disabled) => Ok(()),
            Some(MacAddressFilterMode::Allow) => {
                let allow_list = settings
                    .items
                    .or(Some(vec![]))
                    .unwrap()
                    .into_iter()
                    .map(|x| {
                        Ok(AllowListEntry {
                            mac_addr: EUI64(
                                x.mac_address
                                    .unwrap_or(vec![])
                                    .try_into()
                                    .map_err(|_| Err(ZxStatus::INVALID_ARGS))?,
                            ),
                            rssi: x.rssi.unwrap_or(127),
                        })
                    })
                    .collect::<Result<Vec<AllowListEntry>, ZxStatus>>()?;
                // Set allowlist
                self.frame_handler
                    .send_request(CmdPropValueSet(PropMac::AllowList.into(), allow_list).verify())
                    .await
                    .map_err(|err| {
                        fx_log_err!("Error with CmdPropValueSet: {:?}", err);
                        ZxStatus::INTERNAL
                    })?;
                // Enable allowlist
                self.frame_handler
                    .send_request(CmdPropValueSet(PropMac::AllowListEnabled.into(), true).verify())
                    .await
                    .map_err(|err| {
                        fx_log_err!("Error enable allowlist: {}", err);
                        ZxStatus::INTERNAL
                    })
            }
            Some(MacAddressFilterMode::Deny) => {
                // Construct denylist
                let deny_list = settings
                    .items
                    .or(Some(vec![]))
                    .unwrap()
                    .into_iter()
                    .map(|x| {
                        x.mac_address.map_or(Err(ZxStatus::INVALID_ARGS), Ok).and_then(|x| {
                            // TODO (jiamingw): apply this change after the openthread library patch is in
                            // Ok(DenyListEntry {
                            //     mac_addr: EUI64(
                            //         x.try_into().map_err(|_| Err(ZxStatus::INVALID_ARGS))?,
                            //     ),
                            // })
                            Ok(AllowListEntry {
                                mac_addr: EUI64(
                                    x.try_into().map_err(|_| Err(ZxStatus::INVALID_ARGS))?,
                                ),
                                rssi: 127,
                            })
                        })
                    })
                    .collect::<Result<Vec<AllowListEntry>, ZxStatus>>()?;
                // Set denylist
                self.frame_handler
                    .send_request(CmdPropValueSet(PropMac::AllowList.into(), deny_list).verify())
                    .await
                    .map_err(|err| {
                        fx_log_err!("Error with CmdPropValueSet: {:?}", err);
                        ZxStatus::INTERNAL
                    })?;
                // Enable denylist
                self.frame_handler
                    .send_request(CmdPropValueSet(PropMac::DenyListEnabled.into(), true).verify())
                    .await
                    .map_err(|err| {
                        fx_log_err!("Error enable denylist: {}", err);
                        ZxStatus::INTERNAL
                    })
            }
            _ => Err(ZxStatus::NOT_SUPPORTED),
        }
    }

    async fn get_mac_address_filter_settings(&self) -> ZxResult<MacAddressFilterSettings> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        // Wait for our turn.
        let _lock = self.wait_for_api_task_lock("get_mac_address_filter_settings").await?;

        let allow_list_enabled =
            self.get_property_simple::<bool, _>(PropMac::AllowListEnabled).await?;

        let deny_list_enabled =
            self.get_property_simple::<bool, _>(PropMac::DenyListEnabled).await?;

        if allow_list_enabled && deny_list_enabled {
            fx_log_err!("get_mac_address_filter_settings: Both allow and deny list are enabled");
            self.ncp_is_misbehaving();
            return Err(ZxStatus::INTERNAL);
        }

        let mode = if allow_list_enabled == true {
            MacAddressFilterMode::Allow
        } else if deny_list_enabled == true {
            MacAddressFilterMode::Deny
        } else {
            MacAddressFilterMode::Disabled
        };

        let filter_item_vec = match mode {
            MacAddressFilterMode::Allow => self
                .get_property_simple::<AllowList, _>(PropMac::AllowList)
                .await?
                .into_iter()
                .map(|item| MacAddressFilterItem {
                    mac_address: Some(item.mac_addr.0.to_vec()),
                    rssi: Some(item.rssi),
                    ..MacAddressFilterItem::EMPTY
                })
                .collect::<Vec<_>>(),
            MacAddressFilterMode::Deny => self
                .get_property_simple::<DenyList, _>(PropMac::DenyList)
                .await?
                .into_iter()
                .map(|item| MacAddressFilterItem {
                    mac_address: Some(item.mac_addr.0.to_vec()),
                    rssi: None,
                    ..MacAddressFilterItem::EMPTY
                })
                .collect::<Vec<_>>(),
            _ => vec![],
        };

        match mode {
            MacAddressFilterMode::Disabled => Ok(MacAddressFilterSettings {
                mode: Some(MacAddressFilterMode::Disabled),
                ..MacAddressFilterSettings::EMPTY
            }),
            MacAddressFilterMode::Allow => Ok(MacAddressFilterSettings {
                mode: Some(MacAddressFilterMode::Allow),
                items: Some(filter_item_vec),
                ..MacAddressFilterSettings::EMPTY
            }),
            MacAddressFilterMode::Deny => Ok(MacAddressFilterSettings {
                mode: Some(MacAddressFilterMode::Deny),
                items: Some(filter_item_vec),
                ..MacAddressFilterSettings::EMPTY
            }),
        }
    }

    async fn make_joinable(&self, duration: fuchsia_zircon::Duration, port: u16) -> ZxResult<()> {
        fx_log_info!("make_joinable: duration: {} port: {}", duration.into_seconds(), port);

        // Convert the duration parameter into a `std::time::Duration`.
        let duration = std::time::Duration::from_nanos(
            duration.into_nanos().try_into().ok().ok_or(ZxStatus::INVALID_ARGS)?,
        );

        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        {
            let mut driver_state = self.driver_state.lock();

            if !driver_state.is_active_and_ready() {
                return Err(ZxStatus::BAD_STATE);
            }

            match (duration.as_nanos() > 0, NonZeroU16::new(port)) {
                (true, Some(port)) => {
                    driver_state.assisting_state.prepare_to_assist(duration, port)
                }
                (false, _) => driver_state.assisting_state.clear(),
                (true, None) => return Err(ZxStatus::INVALID_ARGS),
            }

            std::mem::drop(driver_state);
            self.driver_state_change.trigger();
        }

        let future = async move {
            if duration.as_nanos() > 0 {
                // Set the steering data to the all-devices mask if we have that capability.
                if self.driver_state.lock().has_cap(Cap::Ot(CapOt::OobSteeringData)) {
                    const STEERING_DATA_ALL_DEVICES_MASK: EUI64 = EUI64([0xFFu8; 8]);
                    match self
                        .frame_handler
                        .send_request(CmdPropValueSet(
                            PropThread::SteeringData.into(),
                            STEERING_DATA_ALL_DEVICES_MASK,
                        ))
                        .await
                    {
                        Ok(()) => (),
                        Err(err) => fx_log_warn!(
                            "make_joinable: Unable to set steering data to all devices: {:?}",
                            err
                        ),
                    }
                } else {
                    fx_log_warn!("make_joinable: CAP_OOB_STEERING_DATA not set");
                }

                // Specify the assisting ports.
                self.frame_handler
                    .send_request(CmdPropValueSet(PropThread::AssistingPorts.into(), port).verify())
                    .await
            } else {
                // Clear the steering data if we have that capability.
                if self.driver_state.lock().has_cap(Cap::Ot(CapOt::OobSteeringData)) {
                    const STEERING_DATA_NO_DEVICES_MASK: EUI64 = EUI64([0u8; 8]);
                    match self
                        .frame_handler
                        .send_request(CmdPropValueSet(
                            PropThread::SteeringData.into(),
                            STEERING_DATA_NO_DEVICES_MASK,
                        ))
                        .await
                    {
                        Ok(()) => (),
                        Err(err) => {
                            fx_log_warn!("make_joinable: Unable to clear steering data: {:?}", err)
                        }
                    }
                } else {
                    fx_log_warn!("make_joinable: CAP_OOB_STEERING_DATA not set");
                }

                // Clear the assisting ports.
                self.frame_handler
                    .send_request(CmdPropValueSet(PropThread::AssistingPorts.into(), ()).verify())
                    .await
            }
        };

        self.apply_standard_combinators(future.boxed()).await
    }

    async fn get_neighbor_table(&self) -> ZxResult<Vec<NeighborInfo>> {
        // Wait until we are ready.
        self.wait_for_state(DriverState::is_initialized).await;

        Ok(self
            .get_property_simple::<NeighborTable, _>(PropThread::NeighborTable)
            .await?
            .into_iter()
            .map(|item| NeighborInfo {
                mac_address: Some(item.extended_addr.0.to_vec()),
                short_address: Some(item.short_addr),
                age: Some(fuchsia_zircon::Duration::from_seconds(item.age.into()).into_nanos()),
                is_child: Some(item.is_child),
                link_frame_count: Some(item.link_frame_cnt),
                mgmt_frame_count: Some(item.mle_frame_cnt),
                last_rssi_in: Some(item.last_rssi.into()),
                avg_rssi_in: Some(item.avg_rssi),
                lqi_in: Some(item.link_quality),
                thread_mode: Some(item.mode),
                ..NeighborInfo::EMPTY
            })
            .collect::<Vec<_>>())
    }

    async fn get_counters(&self) -> ZxResult<AllCounters> {
        let res = self.get_property_simple::<AllMacCounters, _>(PropCntr::AllMacCounters).await?;

        if res.tx_counters.len() != 17 || res.rx_counters.len() != 17 {
            fx_log_err!(
                "get_counters: Unexpected counter length: {} tx counters and \
                        {} rx counters",
                res.tx_counters.len(),
                res.rx_counters.len()
            );
            return Err(ZxStatus::INTERNAL);
        }

        Ok(res.into())
    }

    async fn reset_counters(&self) -> ZxResult<AllCounters> {
        self.frame_handler
            .send_request(CmdPropValueSet(PropCntr::AllMacCounters.into(), 0u8))
            .await
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?;

        self.get_counters().await
    }
}

impl<DS: SpinelDeviceClient, NI: NetworkInterface> SpinelDriver<DS, NI> {
    fn device_state_snapshot(&self) -> DeviceState {
        let driver_state = self.driver_state.lock();
        DeviceState {
            connectivity_state: Some(driver_state.connectivity_state),
            role: Some(driver_state.role),
            ..DeviceState::EMPTY
        }
    }

    fn identity_snapshot(&self) -> Identity {
        let driver_state = self.driver_state.lock();
        if driver_state.is_ready() {
            driver_state.identity.clone()
        } else {
            Identity::EMPTY
        }
    }
}
