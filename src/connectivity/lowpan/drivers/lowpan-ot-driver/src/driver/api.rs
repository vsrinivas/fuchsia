// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use anyhow::Error;
use async_trait::async_trait;
use core::future::ready;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::*;
use fidl_fuchsia_lowpan_test::*;
use fuchsia_zircon::Duration;
use lowpan_driver_common::AsyncConditionWait;
use lowpan_driver_common::Driver as LowpanDriver;
use lowpan_driver_common::ZxResult;
use std::ffi::CString;

/// Helpers for API-related tasks.
impl<OT: Send, NI> OtDriver<OT, NI> {
    /// Helper function for methods that return streams. Allows you
    /// to have an initialization method that returns a lock which can be
    /// held while another stream is running.
    pub(super) fn start_ongoing_stream_process<'a, R, FInit, SStream, L>(
        &'a self,
        init_task: FInit,
        stream: SStream,
        timeout: fasync::Time,
    ) -> BoxStream<'a, ZxResult<R>>
    where
        R: Send + 'a,
        FInit: Send + Future<Output = Result<L, Error>> + 'a,
        SStream: Send + Stream<Item = Result<R, Error>> + 'a,
        L: Send + 'a,
    {
        enum InternalState<'a, R, L> {
            Init(BoxFuture<'a, ZxResult<L>>, BoxStream<'a, ZxResult<R>>),
            Running(L, BoxStream<'a, ZxResult<R>>),
            Done,
        }

        let init_task = init_task
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
            .on_timeout(fasync::Time::after(DEFAULT_TIMEOUT), || Err(ZxStatus::TIMED_OUT));

        let stream = stream.map_err(|e| ZxStatus::from(ErrorAdapter(e)));

        futures::stream::unfold(
            InternalState::Init(init_task.boxed(), stream.boxed()),
            move |mut last_state: InternalState<'_, R, L>| async move {
                last_state = match last_state {
                    InternalState::Init(init_task, stream) => {
                        debug!("ongoing_stream_process: Initializing. . .");
                        match init_task.await {
                            Ok(lock) => {
                                debug!("ongoing_stream_process: Initialized.");
                                InternalState::Running(lock, stream)
                            }
                            Err(err) => {
                                debug!("ongoing_stream_process: Initialization failed: {:?}", err);
                                return Some((Err(err), InternalState::Done));
                            }
                        }
                    }
                    last_state => last_state,
                };

                if let InternalState::Running(lock, mut stream) = last_state {
                    debug!("ongoing_stream_process: getting next");
                    if let Some(next) = stream
                        .next()
                        .on_timeout(timeout, move || {
                            error!("ongoing_stream_process: Timeout");
                            Some(Err(ZxStatus::TIMED_OUT))
                        })
                        .await
                    {
                        return Some((next, InternalState::Running(lock, stream)));
                    }
                }

                debug!("ongoing_stream_process: Done");

                None
            },
        )
        .boxed()
    }
}

/// API-related tasks. Implementation of [`lowpan_driver_common::Driver`].
#[async_trait]
impl<OT, NI> LowpanDriver for OtDriver<OT, NI>
where
    OT: Send + ot::InstanceInterface,
    NI: NetworkInterface,
{
    async fn provision_network(&self, params: ProvisioningParams) -> ZxResult<()> {
        debug!("Got provision command: {:?}", params);

        // Wait until we are not busy.
        self.wait_for_state(|x| !x.is_busy()).await;

        if params.identity.raw_name.is_none() {
            // We must at least have the network name specified.
            return Err(ZxStatus::INVALID_ARGS);
        }

        if let Some(ref net_type) = params.identity.net_type {
            if !self.is_net_type_supported(net_type.as_str()) {
                error!("Network type {:?} is not supported by this interface.", net_type);
                return Err(ZxStatus::NOT_SUPPORTED);
            }
        };

        let task = async {
            let mut dataset = ot::OperationalDataset::empty();
            let driver_state = self.driver_state.lock();
            let ot_instance = &driver_state.ot_instance;

            // Start with a new blank dataset.
            ot_instance.dataset_create_new_network(&mut dataset)?;

            // Update that dataset with the provisioning parameters.
            dataset.update_from(&params)?;

            // Update OpenThread with the new dataset.
            ot_instance.dataset_set_active(&dataset)?;

            if !ot_instance.is_commissioned() {
                return Err(format_err!(
                    "Set all of the parameters, but we aren't commissioned yet"
                ));
            }

            Ok(())
        };

        self.apply_standard_combinators(task.boxed()).await
    }

    async fn leave_network(&self) -> ZxResult<()> {
        fx_log_debug!("Got leave command");

        let task = async {
            let driver_state = self.driver_state.lock();
            let ot_instance = &driver_state.ot_instance;

            ot_instance.thread_set_enabled(false)?;
            ot_instance.ip6_set_enabled(false)?;
            ot_instance.erase_persistent_info()?;
            ot_instance.reset();

            if ot_instance.is_commissioned() {
                return Err(format_err!("Unable to fully clear dataset"));
            }

            Ok(())
        };

        self.apply_standard_combinators(task.boxed()).await
    }

    async fn set_active(&self, enabled: bool) -> ZxResult<()> {
        fx_log_info!("Got set active command: {:?}", enabled);

        // Wait until we are not busy.
        self.wait_for_state(|x| !x.is_busy()).await;

        self.apply_standard_combinators(self.net_if.set_enabled(enabled).boxed()).await?;

        self.wait_for_state(|x| x.is_active() == enabled).await;

        Ok(())
    }

    async fn get_supported_network_types(&self) -> ZxResult<Vec<String>> {
        // We only support Thread networks.
        Ok(vec![fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()])
    }

    async fn get_supported_channels(&self) -> ZxResult<Vec<ChannelInfo>> {
        let supported_channel_mask =
            self.driver_state.lock().ot_instance.get_supported_channel_mask();

        Ok(supported_channel_mask
            .into_iter()
            .map(|x| ChannelInfo {
                // TODO: Actually calculate all of the fields for channel info struct
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

                            snapshot = self.driver_state.lock().get_current_device_state();
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
                        snapshot = self.driver_state.lock().get_current_device_state();
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
                            snapshot = self.driver_state.lock().get_current_identity();
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
                        snapshot = self.driver_state.lock().get_current_identity();
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

        ready(Err(ZxStatus::NOT_SUPPORTED)).into_stream().boxed()
    }

    fn join_network(
        &self,
        params: JoinParams,
    ) -> BoxStream<'_, ZxResult<Result<ProvisioningProgress, ProvisionError>>> {
        fx_log_debug!("Got join command: {:?}", params);

        match params {
            JoinParams::JoinerParameter(joiner_params) => self.joiner_start(joiner_params),
            _ => {
                fx_log_err!("join network: provision params not yet supported");
                ready(Err(ZxStatus::INVALID_ARGS)).into_stream().boxed()
            }
        }
    }

    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>> {
        fx_log_info!("Got get credential command");
        let driver_state = self.driver_state.lock();
        let ot_instance = &driver_state.ot_instance;
        let mut operational_dataset = Default::default();

        ot_instance.dataset_get_active(&mut operational_dataset)?;

        Ok(operational_dataset
            .get_network_key()
            .map(ot::NetworkKey::to_vec)
            .map(fidl_fuchsia_lowpan::Credential::MasterKey))
    }

    fn start_energy_scan(
        &self,
        params: &EnergyScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<fidl_fuchsia_lowpan_device::EnergyScanResult>>> {
        fx_log_info!("Got energy scan command: {:?}", params);

        let driver_state = self.driver_state.lock();
        let ot_instance = &driver_state.ot_instance;

        let all_channels = ot_instance.get_supported_channel_mask();

        let channels = if let Some(channels) = params.channels.as_ref() {
            ot::ChannelMask::try_from(channels)
        } else {
            Ok(all_channels)
        };

        let dwell_time_ms: u64 = params.dwell_time_ms.unwrap_or(DEFAULT_SCAN_DWELL_TIME_MS).into();

        let dwell_time = std::time::Duration::from_millis(dwell_time_ms.into());

        let timeout = fasync::Time::after(
            Duration::from_millis((dwell_time_ms * all_channels.len() as u64).try_into().unwrap())
                + SCAN_EXTRA_TIMEOUT,
        );

        use futures::channel::mpsc;
        let (sender, receiver) = mpsc::unbounded();

        let init_task = async move {
            // Wait until we are not busy.
            self.wait_for_state(|x| !x.is_busy()).await;

            self.driver_state.lock().ot_instance.start_energy_scan(
                channels?,
                dwell_time,
                move |x| {
                    fx_log_trace!("energy_scan_callback: Got result {:?}", x);
                    if let Some(x) = x {
                        if let Err(_) = sender.unbounded_send(x.clone()) {
                            // If this is an error then that just means the
                            // other end has been dropped. We really don't care,
                            // not even worth logging.
                        }
                    } else {
                        fx_log_trace!("energy_scan_callback: Closing scan stream");
                        sender.close_channel();
                    }
                },
            )?;

            Ok(())
        };

        let stream = receiver.map(|x| {
            Ok(vec![EnergyScanResult {
                channel_index: Some(x.channel().into()),
                max_rssi: Some(x.max_rssi().into()),
                ..EnergyScanResult::EMPTY
            }])
        });

        self.start_ongoing_stream_process(init_task, stream, timeout)
    }

    fn start_network_scan(
        &self,
        params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>> {
        fx_log_info!("Got network scan command: {:?}", params);

        let driver_state = self.driver_state.lock();
        let ot_instance = &driver_state.ot_instance;

        let all_channels = ot_instance.get_supported_channel_mask();

        let channels = if let Some(channels) = params.channels.as_ref() {
            ot::ChannelMask::try_from(channels)
        } else {
            Ok(all_channels)
        };

        let dwell_time_ms: u64 = DEFAULT_SCAN_DWELL_TIME_MS.into();

        let dwell_time = std::time::Duration::from_millis(dwell_time_ms.into());

        let timeout = fasync::Time::after(
            Duration::from_millis((dwell_time_ms * all_channels.len() as u64).try_into().unwrap())
                + SCAN_EXTRA_TIMEOUT,
        );

        use futures::channel::mpsc;
        let (sender, receiver) = mpsc::unbounded();

        let init_task = async move {
            // Wait until we are not busy.
            self.wait_for_state(|x| !x.is_busy()).await;

            self.driver_state.lock().ot_instance.start_active_scan(
                channels?,
                dwell_time,
                move |x| {
                    fx_log_trace!("active_scan_callback: Got result {:?}", x);
                    if let Some(x) = x {
                        if let Err(_) = sender.unbounded_send(x.clone()) {
                            // If this is an error then that just means the
                            // other end has been dropped. We really don't care,
                            // not even worth logging.
                        }
                    } else {
                        fx_log_trace!("active_scan_callback: Closing scan stream");
                        sender.close_channel();
                    }
                },
            )?;

            Ok(())
        };

        let stream = receiver.map(|x| Ok(vec![x.into_ext()]));

        self.start_ongoing_stream_process(init_task, stream, timeout)
    }

    async fn reset(&self) -> ZxResult<()> {
        warn!("Got API request to reset");
        self.driver_state.lock().ot_instance.reset();
        Ok(())
    }

    async fn get_factory_mac_address(&self) -> ZxResult<Vec<u8>> {
        Ok(self.driver_state.lock().ot_instance.get_factory_assigned_ieee_eui_64().to_vec())
    }

    async fn get_current_mac_address(&self) -> ZxResult<Vec<u8>> {
        Ok(self.driver_state.lock().ot_instance.get_extended_address().to_vec())
    }

    async fn get_ncp_version(&self) -> ZxResult<String> {
        Ok(ot::get_version_string().to_string())
    }

    async fn get_current_channel(&self) -> ZxResult<u16> {
        Ok(self.driver_state.lock().ot_instance.get_channel() as u16)
    }

    async fn get_current_rssi(&self) -> ZxResult<i32> {
        Ok(self.driver_state.lock().ot_instance.get_rssi() as i32)
    }

    async fn get_partition_id(&self) -> ZxResult<u32> {
        Ok(self.driver_state.lock().ot_instance.get_partition_id())
    }

    async fn get_thread_rloc16(&self) -> ZxResult<u16> {
        Ok(self.driver_state.lock().ot_instance.get_rloc16())
    }

    async fn get_thread_router_id(&self) -> ZxResult<u8> {
        Err(ZxStatus::NOT_SUPPORTED)
    }

    async fn send_mfg_command(&self, command: &str) -> ZxResult<String> {
        // TODO(rquattle): For now we are sending manufacturing commands to the normal
        //                 OpenThread CLI interface to unblock testing efforts.
        //                 Long term we need to have this API connect to the manufacturing
        //                 commands and have the normal CLI commands plumbed via a different
        //                 route.
        const WAIT_FOR_RESPONSE_TIMEOUT: Duration = Duration::from_millis(500);

        // Locking this receiver also prevents multiple outstanding CLI commands from racing.
        let mut receiver = self.cli_output_receiver.lock().await;

        // Execute the command.
        self.driver_state.lock().ot_instance.cli_input_line(&CString::new(command).unwrap());

        // Collect the response, waiting up to WAIT_FOR_RESPONSE_TIMEOUT after the last bit.
        let mut output = String::new();
        while let Some(Some(next)) = receiver
            .next()
            .map(Option::Some)
            .on_timeout(fasync::Time::after(WAIT_FOR_RESPONSE_TIMEOUT), || None)
            .await
        {
            output.extend(next.chars());
        }

        Ok(output)
    }

    async fn replace_mac_address_filter_settings(
        &self,
        _settings: MacAddressFilterSettings,
    ) -> ZxResult<()> {
        return Err(ZxStatus::NOT_SUPPORTED);
    }

    async fn get_mac_address_filter_settings(&self) -> ZxResult<MacAddressFilterSettings> {
        return Err(ZxStatus::NOT_SUPPORTED);
    }

    async fn get_neighbor_table(&self) -> ZxResult<Vec<NeighborInfo>> {
        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .iter_neighbor_info()
            .map(|x| NeighborInfo {
                mac_address: Some(x.ext_address().to_vec()),
                short_address: Some(x.rloc16()),
                age: Some(
                    fuchsia_async::Duration::from_seconds(x.age().try_into().unwrap())
                        .into_nanos()
                        .try_into()
                        .unwrap(),
                ),
                is_child: Some(x.is_child()),
                link_frame_count: Some(x.link_frame_counter()),
                mgmt_frame_count: Some(x.mle_frame_counter()),
                last_rssi_in: Some(x.last_rssi() as i32),
                avg_rssi_in: Some(x.average_rssi()),
                lqi_in: Some(x.lqi_in()),
                ..NeighborInfo::EMPTY
            })
            .collect::<Vec<_>>())
    }

    async fn get_counters(&self) -> ZxResult<AllCounters> {
        return Err(ZxStatus::NOT_SUPPORTED);
    }

    async fn reset_counters(&self) -> ZxResult<AllCounters> {
        return Err(ZxStatus::NOT_SUPPORTED);
    }

    async fn register_on_mesh_prefix(&self, net: OnMeshPrefix) -> ZxResult<()> {
        let prefix = if let Some(subnet) = net.subnet {
            Ok(ot::Ip6Prefix::new(subnet.addr.addr, subnet.prefix_len))
        } else {
            Err(ZxStatus::INVALID_ARGS)
        }?;

        let mut omp = ot::BorderRouterConfig::from_prefix(prefix);

        omp.set_on_mesh(true);

        omp.set_default_route_preference(
            net.default_route_preference.map(ot::RoutePreference::from_ext),
        );

        if let Some(x) = net.slaac_preferred {
            omp.set_preferred(x);
        }

        if let Some(x) = net.slaac_valid {
            omp.set_slaac(x);
        }

        omp.set_stable(net.stable.unwrap_or(true));

        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .add_on_mesh_prefix(&omp)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?)
    }

    async fn unregister_on_mesh_prefix(
        &self,
        subnet: fidl_fuchsia_lowpan::Ipv6Subnet,
    ) -> ZxResult<()> {
        let prefix = ot::Ip6Prefix::new(subnet.addr.addr, subnet.prefix_len);

        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .remove_on_mesh_prefix(&prefix)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?)
    }

    async fn register_external_route(&self, net: ExternalRoute) -> ZxResult<()> {
        let prefix = if let Some(subnet) = net.subnet {
            Ok(ot::Ip6Prefix::new(subnet.addr.addr, subnet.prefix_len))
        } else {
            Err(ZxStatus::INVALID_ARGS)
        }?;

        let mut er = ot::ExternalRouteConfig::from_prefix(prefix);

        if let Some(route_preference) = net.route_preference {
            er.set_route_preference(route_preference.into_ext());
        }

        if let Some(stable) = net.stable {
            er.set_stable(stable);
        }

        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .add_external_route(&er)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?)
    }

    async fn unregister_external_route(
        &self,
        subnet: fidl_fuchsia_lowpan::Ipv6Subnet,
    ) -> ZxResult<()> {
        let prefix = ot::Ip6Prefix::new(subnet.addr.addr, subnet.prefix_len);

        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .remove_external_route(&prefix)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?)
    }

    async fn get_local_on_mesh_prefixes(
        &self,
    ) -> ZxResult<Vec<fidl_fuchsia_lowpan_device::OnMeshPrefix>> {
        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .iter_local_on_mesh_prefixes()
            .map(OnMeshPrefix::from_ext)
            .collect::<Vec<_>>())
    }

    async fn get_local_external_routes(
        &self,
    ) -> ZxResult<Vec<fidl_fuchsia_lowpan_device::ExternalRoute>> {
        Ok(self
            .driver_state
            .lock()
            .ot_instance
            .iter_local_external_routes()
            .map(ExternalRoute::from_ext)
            .collect::<Vec<_>>())
    }

    async fn make_joinable(&self, _duration: fuchsia_zircon::Duration, _port: u16) -> ZxResult<()> {
        return Err(ZxStatus::NOT_SUPPORTED);
    }

    async fn get_active_dataset_tlvs(&self) -> ZxResult<Vec<u8>> {
        self.driver_state
            .lock()
            .ot_instance
            .dataset_get_active_tlvs()
            .map(ot::OperationalDatasetTlvs::into)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
    }

    async fn set_active_dataset_tlvs(&self, dataset: &[u8]) -> ZxResult {
        let dataset = ot::OperationalDatasetTlvs::try_from_slice(dataset)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))?;

        self.driver_state
            .lock()
            .ot_instance
            .dataset_set_active_tlvs(&dataset)
            .map_err(|e| ZxStatus::from(ErrorAdapter(e)))
    }
}
