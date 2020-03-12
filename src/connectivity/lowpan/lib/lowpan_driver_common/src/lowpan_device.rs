// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use anyhow::Error;
use fidl_fuchsia_lowpan::*;
use fidl_fuchsia_lowpan_device::{
    DeviceExtraRequest, DeviceExtraRequestStream, DeviceRequest, DeviceRequestStream, DeviceState,
    EnergyScanParameters, EnergyScanResult, NetworkScanParameters, ProvisioningMonitorMarker,
};
use fidl_fuchsia_lowpan_test::*;
use futures::stream::BoxStream;

/// Trait for implementing a LoWPAN Device Driver.
#[async_trait]
pub trait Driver: Send + Sync {
    /// Provisions this device with a network.
    ///
    /// See [`fidl_fuchsia_lowpan_device::Device::provision_network`] for more information.
    async fn provision_network(&self, params: ProvisioningParams) -> ZxResult<()>;

    /// See [`fidl_fuchsia_lowpan_device::Device::leave_network`] for more information.
    async fn leave_network(&self) -> ZxResult<()>;

    /// Change the 'active' state for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_device::Device::set_active`] for more information.
    async fn set_active(&self, active: bool) -> ZxResult<()>;

    /// Returns a list of the supported network types.
    ///
    /// See [`fidl_fuchsia_lowpan_device::Device::get_supported_network_types`] for more
    /// information.
    async fn get_supported_network_types(&self) -> ZxResult<Vec<String>>;

    /// Returns a list of supported channels.
    ///
    /// See [`fidl_fuchsia_lowpan_device::Device::get_supported_channels`] for more
    /// information.
    async fn get_supported_channels(&self) -> ZxResult<Vec<ChannelInfo>>;

    /// Returns a stream object which, after emitting the initial DeviceState, will
    /// procede to emit deltas for the DeviceState as it changes.
    ///
    /// This is used to implement [`fidl_fuchsia_lowpan_device::Device::watch_device_state`].
    /// Only one instance of this stream is created per channel.
    fn watch_device_state(&self) -> BoxStream<'_, ZxResult<DeviceState>>;

    /// Returns a stream object which, after emitting the initial identity, will
    /// procede to emit additional values as the network identity for the device changes.
    ///
    /// This is used to implement [`fidl_fuchsia_lowpan_device::DeviceExtra::watch_identity`].
    /// Only one instance of this stream is created per channel.
    fn watch_identity(&self) -> BoxStream<'_, ZxResult<Identity>>;

    /// Forms a new network.
    ///
    /// See [`fidl_fuchsia_lowpan_device::DeviceExtra::form_network`] for more information.
    ///
    /// Note that the future returned from this method is not
    /// for the return value, rather it is used for handling
    /// the `progress` ServerEnd and making progress on the
    /// `form` operation. From a FIDL point of view, this method
    /// returns immediately.
    async fn form_network(
        &self,
        params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    );

    /// Joins an existing network.
    ///
    /// See [`fidl_fuchsia_lowpan_device::DeviceExtra::join_network`] for more information.
    ///
    /// Note that the future returned from this method is not
    /// for the return value, rather it is used for handling
    /// the `progress` ServerEnd and making progress on the
    /// `join` operation. From a FIDL point of view, this method
    /// returns immediately.
    async fn join_network(
        &self,
        params: ProvisioningParams,
        progress: fidl::endpoints::ServerEnd<ProvisioningMonitorMarker>,
    );

    /// Returns the value of the current credential, if one is available.
    ///
    /// See [`fidl_fuchsia_lowpan_device::DeviceExtra::get_credential`] for more information.
    async fn get_credential(&self) -> ZxResult<Option<fidl_fuchsia_lowpan::Credential>>;

    /// Starts a new energy scan operation and returns a stream for tracking the
    /// results.
    ///
    /// See [`fidl_fuchsia_lowpan_device::DeviceExtra::start_energy_scan`] for more information.
    fn start_energy_scan(
        &self,
        params: &EnergyScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<EnergyScanResult>>>;

    /// Starts a new network scan operation and returns a stream for tracking the
    /// results.
    ///
    /// See [`fidl_fuchsia_lowpan_device::DeviceExtra::start_network_scan`] for more information.
    fn start_network_scan(
        &self,
        params: &NetworkScanParameters,
    ) -> BoxStream<'_, ZxResult<Vec<BeaconInfo>>>;

    /// Performs a device reset.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::reset`] for more information.
    async fn reset(&self) -> ZxResult<()>;

    /// Returns the factory-assigned static MAC address for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_factory_mac_address`] for more information.
    async fn get_factory_mac_address(&self) -> ZxResult<Vec<u8>>;

    /// Returns the currently assigned MAC address for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_current_mac_address`] for more information.
    async fn get_current_mac_address(&self) -> ZxResult<Vec<u8>>;

    /// Returns the NCP/Stack version string for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_ncp_version`] for more information.
    async fn get_ncp_version(&self) -> ZxResult<String>;

    /// Returns the current channel for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_current_channel`] for more information.
    async fn get_current_channel(&self) -> ZxResult<u16>;

    /// Returns the immediate RSSI value for the radio on this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_rssi`] for more information.
    async fn get_current_rssi(&self) -> ZxResult<i32>;

    /// Returns the partition id that this device is a part of.
    /// Returns 0xFFFFFFFF if this device is not a member of a partition.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_partition_id`] for more information.
    async fn get_partition_id(&self) -> ZxResult<u32>;

    /// Returns the current Thread RLOC16 for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_thread_rloc16`] for more information.
    async fn get_thread_rloc16(&self) -> ZxResult<u16>;

    /// Returns the current Thread router id for this device.
    ///
    /// See [`fidl_fuchsia_lowpan_test::DeviceTest::get_thread_router_id`] for more information.
    async fn get_thread_router_id(&self) -> ZxResult<u8>;
}

#[async_trait(?Send)]
impl<T: Driver> ServeTo<DeviceRequestStream> for T {
    async fn serve_to(&self, request_stream: DeviceRequestStream) -> anyhow::Result<()> {
        use futures::lock::Mutex;
        let watcher = Mutex::new(self.watch_device_state());

        let closure = |command| async {
            match command {
                DeviceRequest::ProvisionNetwork { params, responder } => {
                    self.provision_network(params)
                        .err_into::<Error>()
                        .and_then(|_| ready(responder.send().map_err(Error::from)))
                        .await
                        .context("error in provision_network request")?;
                }

                DeviceRequest::LeaveNetwork { responder, .. } => {
                    self.leave_network()
                        .err_into::<Error>()
                        .and_then(|_| ready(responder.send().map_err(Error::from)))
                        .await
                        .context("error in leave_network request")?;
                }

                DeviceRequest::SetActive { active, responder, .. } => {
                    self.set_active(active)
                        .err_into::<Error>()
                        .and_then(|_| ready(responder.send().map_err(Error::from)))
                        .await
                        .context("error in set_active request")?;
                }

                DeviceRequest::GetSupportedNetworkTypes { responder } => {
                    self.get_supported_network_types()
                        .err_into::<Error>()
                        .and_then(|response| {
                            ready(
                                responder
                                    .send(&mut response.iter().map(String::as_str))
                                    .map_err(Error::from),
                            )
                        })
                        .await
                        .context("error in get_supported_network_types request")?;
                }

                DeviceRequest::GetSupportedChannels { responder } => {
                    self.get_supported_channels()
                        .err_into::<Error>()
                        .and_then(|response| {
                            ready(responder.send(&mut response.into_iter()).map_err(Error::from))
                        })
                        .await
                        .context("error in get_supported_channels request")?;
                }

                DeviceRequest::WatchDeviceState { responder } => {
                    watcher
                        .try_lock()
                        .ok_or(format_err!(
                            "No more than 1 outstanding call to watch_device_state is allowed"
                        ))?
                        .next()
                        .map(|x| match x {
                            Some(x) => x.map_err(Error::from),
                            None => Err(format_err!("Device combined state stream ended early")),
                        })
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_device_combined_state request")?;
                }
            }
            Result::<(), anyhow::Error>::Ok(())
        };

        if let Some(err) =
            request_stream.err_into::<Error>().try_for_each_concurrent(None, closure).await.err()
        {
            fx_log_err!("Error serving DeviceRequestStream: {:?}", err);

            // TODO: Properly route epitaph codes. This is tricky to do because
            //       `request_stream` is consumed by `try_for_each_concurrent`,
            //       which means that code like the code below will not work:
            //
            // ```
            // if let Some(epitaph) = err.downcast_ref::<ZxStatus>() {
            //     request_stream.into_inner().0
            //         .shutdown_with_epitaph(*epitaph);
            // }
            // ```

            Err(err)
        } else {
            Ok(())
        }
    }
}

#[async_trait(?Send)]
impl<T: Driver> ServeTo<DeviceExtraRequestStream> for T {
    async fn serve_to(&self, request_stream: DeviceExtraRequestStream) -> anyhow::Result<()> {
        use futures::lock::Mutex;
        let watcher = Mutex::new(self.watch_identity());

        let closure = |command| {
            async {
                match command {
                    DeviceExtraRequest::JoinNetwork { params, progress, .. } => {
                        self.join_network(params, progress).await;
                    }
                    DeviceExtraRequest::FormNetwork { params, progress, .. } => {
                        self.form_network(params, progress).await;
                    }
                    DeviceExtraRequest::GetCredential { responder, .. } => {
                        self.get_credential()
                            .err_into::<Error>()
                            .and_then(|mut response| {
                                ready(responder.send(response.as_mut()).map_err(Error::from))
                            })
                            .await
                            .context("error in get_credential request")?;
                    }
                    DeviceExtraRequest::WatchIdentity { responder, .. } => {
                        watcher
                            .try_lock()
                            .ok_or(format_err!(
                                "No more than 1 outstanding call to watch_identity is allowed"
                            ))?
                            .next()
                            .map(|x| match x {
                                Some(x) => x.map_err(Error::from),
                                None => {
                                    Err(format_err!("Device combined state stream ended early"))
                                }
                            })
                            .and_then(|response| {
                                ready(responder.send(response).map_err(Error::from))
                            })
                            .await
                            .context("error in watch_identity request")?;
                    }
                    x => {
                        // TODO: Implement everything in this match statement.
                        fx_log_err!("NOT YET IMPLEMENTED IN 'lowpan_driver_common': {:?}", x);
                    }
                }
                Result::<(), anyhow::Error>::Ok(())
            }
        };

        if let Some(err) =
            request_stream.err_into::<Error>().try_for_each_concurrent(None, closure).await.err()
        {
            fx_log_err!("Error serving DeviceExtraRequestStream: {:?}", err);

            // TODO: Properly route epitaph codes. This is tricky to do because
            //       `request_stream` is consumed by `try_for_each_concurrent`,
            //       which means that code like the code below will not work:
            //
            // ```
            // if let Some(epitaph) = err.downcast_ref::<ZxStatus>() {
            //     request_stream.into_inner().0
            //         .shutdown_with_epitaph(*epitaph);
            // }
            // ```

            Err(err)
        } else {
            Ok(())
        }
    }
}

#[async_trait(?Send)]
impl<T: Driver> ServeTo<DeviceTestRequestStream> for T {
    async fn serve_to(&self, request_stream: DeviceTestRequestStream) -> anyhow::Result<()> {
        let closure = |command| async {
            match command {
                DeviceTestRequest::Reset { responder, .. } => {
                    self.reset()
                        .err_into::<Error>()
                        .and_then(|_| ready(responder.send().map_err(Error::from)))
                        .await
                        .context("error in reset request")?;
                }

                DeviceTestRequest::GetNcpVersion { responder, .. } => {
                    self.get_ncp_version()
                        .err_into::<Error>()
                        .and_then(|response| {
                            ready(responder.send(response.as_str()).map_err(Error::from))
                        })
                        .await
                        .context("error in get_ncp_version request")?;
                }
                DeviceTestRequest::GetCurrentChannel { responder, .. } => {
                    self.get_current_channel()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_current_channel request")?;
                }
                DeviceTestRequest::GetCurrentRssi { responder, .. } => {
                    self.get_current_rssi()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_current_rssi request")?;
                }
                DeviceTestRequest::GetFactoryMacAddress { responder, .. } => {
                    self.get_factory_mac_address()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(&response).map_err(Error::from)))
                        .await
                        .context("error in get_factory_mac_address request")?;
                }
                DeviceTestRequest::GetCurrentMacAddress { responder, .. } => {
                    self.get_current_mac_address()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(&response).map_err(Error::from)))
                        .await
                        .context("error in get_current_mac_address request")?;
                }
                DeviceTestRequest::GetPartitionId { responder, .. } => {
                    self.get_partition_id()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_partition_id request")?;
                }
                DeviceTestRequest::GetThreadRloc16 { responder, .. } => {
                    self.get_thread_rloc16()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_thread_rloc16 request")?;
                }
                DeviceTestRequest::GetThreadRouterId { responder, .. } => {
                    self.get_thread_router_id()
                        .err_into::<Error>()
                        .and_then(|response| ready(responder.send(response).map_err(Error::from)))
                        .await
                        .context("error in get_thread_router_id request")?;
                }
            }
            Result::<(), Error>::Ok(())
        };

        if let Some(err) =
            request_stream.err_into::<Error>().try_for_each_concurrent(None, closure).await.err()
        {
            fx_log_err!("Error serving DeviceTestRequestStream: {:?}", err);

            // TODO: Properly route epitaph codes. This is tricky to do because
            //       `request_stream` is consumed by `try_for_each_concurrent`,
            //       which means that code like the code below will not work:
            //
            // ```
            // if let Some(epitaph) = err.downcast_ref::<ZxStatus>() {
            //     request_stream.into_inner().0
            //         .shutdown_with_epitaph(*epitaph);
            // }
            // ```

            Err(err)
        } else {
            Ok(())
        }
    }
}
